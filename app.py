from dotenv import load_dotenv, set_key
from flask import Flask, request, render_template, jsonify
from flask_cors import CORS
from PIL import Image
import io
import base64
import numpy as np
import requests
import os
import logging

app = Flask(__name__)
CORS(app)

load_dotenv()

logging.basicConfig(level=logging.DEBUG)

def load_crop_settings():
    load_dotenv()
    settings = {
        'top': int(os.getenv('CROP_TOP', 0)),
        'right': int(os.getenv('CROP_RIGHT', 0)),
        'bottom': int(os.getenv('CROP_BOTTOM', 0)),
        'left': int(os.getenv('CROP_LEFT', 0))
    }
    logging.debug(f"Loaded crop settings: {settings}")
    return settings

def load_compression_factor():
    load_dotenv()
    compression_factor = int(os.getenv('COMPRESSION_FACTOR', 100))
    logging.debug(f"Loaded compression factor: {compression_factor}%")
    return compression_factor

def save_crop_settings(settings):
    logging.debug(f"Saving crop settings: {settings}")
    for key, value in settings.items():
        set_key('.env', f'CROP_{key.upper()}', str(value))
    load_dotenv()  # Reload environment variables

def apply_crop(image, crop_settings):
    width, height = image.size
    left = crop_settings['left']
    top = crop_settings['top']
    right = width - crop_settings['right']
    bottom = height - crop_settings['bottom']
    logging.debug(f"Applying crop: left={left}, top={top}, right={right}, bottom={bottom}")
    return image.crop((left, top, right, bottom))

def compress_image(image, compression_factor):
    if compression_factor < 100:
        width, height = image.size
        new_width = int(width * (compression_factor / 100))
        new_height = int(height * (compression_factor / 100))
        logging.debug(f"Compressing image: new dimensions=({new_width}, {new_height})")
        return image.resize((new_width, new_height), Image.LANCZOS)
    return image

def image_to_byte_array(image, threshold=128, mode='horizontal'):
    width, height = image.size
    pixels = list(image.getdata())
    byte_array = []
    byte = 0
    bit_count = 0

    for y in range(height):
        for x in range(width):
            if mode == 'horizontal':
                pixel = pixels[y * width + x]
            else:  # vertical
                pixel = pixels[x * height + y]

            if isinstance(pixel, tuple):
                pixel = sum(pixel[:3]) // 3  # Convert RGB to grayscale

            if pixel > threshold:
                byte |= (1 << (7 - bit_count))

            bit_count += 1
            if bit_count == 8:
                byte_array.append(byte)
                byte = 0
                bit_count = 0

    # Add any remaining bits
    if bit_count > 0:
        byte_array.append(byte)

    return byte_array

def byte_array_to_image(byte_array, width, height, mode='horizontal'):
    img = Image.new('1', (width, height))
    pixels = img.load()

    byte_index = 0
    bit_index = 0

    for y in range(height):
        for x in range(width):
            if mode == 'horizontal':
                if byte_array[byte_index] & (1 << (7 - bit_index)):
                    pixels[x, y] = 1
                else:
                    pixels[x, y] = 0
            else:  # vertical
                if byte_array[byte_index] & (1 << (7 - bit_index)):
                    pixels[y, x] = 1
                else:
                    pixels[y, x] = 0

            bit_index += 1
            if bit_index == 8:
                byte_index += 1
                bit_index = 0

    return img


@app.route('/')
def index():
    crop_settings = load_crop_settings()
    return render_template('index.html', initial_crop_settings=crop_settings)

@app.route('/save_crop', methods=['POST'])
def save_crop():
    crop_settings = request.json
    logging.debug(f"Received crop settings: {crop_settings}")
    save_crop_settings(crop_settings)
    return jsonify({'status': 'success', 'crop_settings': crop_settings})

@app.route('/convert', methods=['POST'])
def convert_image():
    threshold = int(request.form.get('threshold', 128))
    mode = request.form.get('mode', 'horizontal')
    crop_settings = load_crop_settings()
    compression_factor = load_compression_factor()

    try:
        if 'image' in request.files:
            image_file = request.files['image']
            image = Image.open(image_file).convert('L')
        elif 'image_url' in request.form:
            image_url = request.form['image_url']
            response = requests.get(image_url)
            image = Image.open(io.BytesIO(response.content)).convert('L')
        else:
            return jsonify({"error": "No image file or URL provided"}), 400

        logging.debug(f"Original image size: {image.size}")
        image = apply_crop(image, crop_settings)
        logging.debug(f"Cropped image size: {image.size}")

        image = compress_image(image, compression_factor)
        logging.debug(f"Compressed image size: {image.size}")

        byte_array = image_to_byte_array(image, threshold, mode)
        formatted_array = ', '.join(f'0x{byte:02X}' for byte in byte_array)

        preview_buffer = io.BytesIO()
        image.save(preview_buffer, format='PNG')
        preview_base64 = base64.b64encode(preview_buffer.getvalue()).decode('utf-8')

        return jsonify({
            "byte_array": formatted_array,
            "width": image.width,
            "height": image.height,
            "preview_image": preview_base64,
            "crop_settings": crop_settings
        })
    except Exception as e:
        logging.error(f"Error in convert_image: {str(e)}")
        return jsonify({"error": str(e)}), 500


if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=True)
