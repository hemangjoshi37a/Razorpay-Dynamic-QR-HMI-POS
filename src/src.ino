#include <WiFiManager.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <EEPROM.h>
#include <WebServer.h>
#include <SD.h>
#include <time.h>
// New global variables for admin settings
float base_product_mrp = 20.00;
String serverIP = "192.168.1.8";
int serverPort = 5000;
String storeName = "My Store";
String api_key = "YOUR RAZORPAY API KEY HERE";
String api_secret = "YOUR RAZORPAY API SECRET HERE";

#define TFT_BL 2
#define GFX_BL DF_GFX_BL

Arduino_ESP32RGBPanel *bus = new Arduino_ESP32RGBPanel(
  GFX_NOT_DEFINED /* CS */, GFX_NOT_DEFINED /* SCK */, GFX_NOT_DEFINED /* SDA */,
  40 /* DE */, 41 /* VSYNC */, 39 /* HSYNC */, 42 /* PCLK */,
  45 /* R0 */, 48 /* R1 */, 47 /* R2 */, 21 /* R3 */, 14 /* R4 */,
  5 /* G0 */, 6 /* G1 */, 7 /* G2 */, 15 /* G3 */, 16 /* G4 */, 4 /* G5 */,
  8 /* B0 */, 3 /* B1 */, 46 /* B2 */, 9 /* B3 */, 1 /* B4 */
);


Arduino_RPi_DPI_RGBPanel *gfx = new Arduino_RPi_DPI_RGBPanel(
  bus,
  480 /* width */, 0 /* hsync_polarity */, 8 /* hsync_front_porch */, 4 /* hsync_pulse_width */, 43 /* hsync_back_porch */,
  272 /* height */, 0 /* vsync_polarity */, 8 /* vsync_front_porch */, 4 /* vsync_pulse_width */, 12 /* vsync_back_porch */,
  1 /* pclk_active_neg */, 9000000 /* prefer_speed */, true /* auto_flush */);
#include "touch.h"


static uint32_t screenWidth;
static uint32_t screenHeight;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *disp_draw_buf;
static lv_disp_drv_t disp_drv;

String current_color = "Yellow";

// UI elements
lv_obj_t *quantityLabel;
lv_obj_t *noticeLabel;
lv_obj_t *qrCodeArea;
lv_obj_t *wifiStatusLabel;
lv_obj_t *productNameLabel;
lv_obj_t *priceLabel;  // Unused but may be intended for future use
lv_style_t style_btn;
lv_style_t style_background;

lv_timer_t *paymentStatusTimer;

lv_obj_t *qrCodeImage;
lv_obj_t *timerLabel;
lv_obj_t *progressBar;
lv_timer_t *countdownTimer;
int remainingSeconds = 180;  // 3 minutes

// Product details
int quantity = 1;
int last_qty = 1;

String qr_code_id;

// Add a new section for required variables and timer
#define DELAY_TIME 5000  // 5 seconds delay

unsigned long lastButtonPressTime = 0;
int timerSet = 0;

// WiFiManager
WiFiManager wifiManager;

// EEPROM size
#define EEPROM_SIZE 512

// SD card chip select pin
#define SD_CS 10

// Create a web server object
WebServer server(80);

// HTML for the admin portal (stored in PROGMEM to save RAM)
const char ADMIN_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>QR Display Admin Portal</title>
    <script src="https://unpkg.com/htmx.org@1.9.2"></script>
    <script src="https://cdn.tailwindcss.com"></script>
</head>
<body class="bg-gray-100 text-gray-800 font-sans">
    <div class="container mx-auto px-4 py-8 max-w-lg">
        <h1 class="text-2xl font-bold mb-6 text-center">QR Display Admin Portal</h1>
        <div class="bg-white shadow-md rounded-lg p-6">
            <form hx-post="/save-settings" hx-target="#result" class="space-y-4">
                <div class="grid grid-cols-2 gap-4">
                    <div>
                        <label class="block text-sm font-medium mb-1" for="base_product_mrp">Base Product MRP</label>
                        <input class="w-full px-3 py-2 border rounded-md text-sm" id="base_product_mrp" type="number" step="0.01" name="base_product_mrp" value="%BASE_PRODUCT_MRP%">
                    </div>
                    <div>
                        <label class="block text-sm font-medium mb-1" for="server_ip">Server IP</label>
                        <input class="w-full px-3 py-2 border rounded-md text-sm" id="server_ip" type="text" name="server_ip" value="%SERVER_IP%">
                    </div>
                    <div>
                        <label class="block text-sm font-medium mb-1" for="server_port">Server Port</label>
                        <input class="w-full px-3 py-2 border rounded-md text-sm" id="server_port" type="number" name="server_port" value="%SERVER_PORT%">
                    </div>
                    <div>
                        <label class="block text-sm font-medium mb-1" for="store_name">Store Name</label>
                        <input class="w-full px-3 py-2 border rounded-md text-sm" id="store_name" type="text" name="store_name" value="%STORE_NAME%">
                    </div>
                </div>
                <div>
                    <label class="block text-sm font-medium mb-1" for="api_key">API Key</label>
                    <input class="w-full px-3 py-2 border rounded-md text-sm" id="api_key" type="text" name="api_key" value="%API_KEY%">
                </div>
                <div>
                    <label class="block text-sm font-medium mb-1" for="api_secret">API Secret</label>
                    <input class="w-full px-3 py-2 border rounded-md text-sm" id="api_secret" type="text" name="api_secret" value="%API_SECRET%">
                </div>
                <div class="flex justify-between items-center mt-6">
                    <button class="bg-blue-500 hover:bg-blue-600 text-white font-semibold py-2 px-4 rounded-md text-sm transition duration-300 ease-in-out" type="submit">
                        Save Settings
                    </button>
                    <button hx-get="/download-logs" class="bg-green-500 hover:bg-green-600 text-white font-semibold py-2 px-4 rounded-md text-sm transition duration-300 ease-in-out">
                        Download Logs
                    </button>
                </div>
            </form>
        </div>
        <div id="result" class="mt-4 text-center text-sm"></div>
    </div>
</body>
</html>
)rawliteral";



const char LOG_MANAGEMENT_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Log Management</title>
    <script src="https://unpkg.com/htmx.org@1.9.2"></script>
    <script src="https://cdn.tailwindcss.com"></script>
</head>
<body class="bg-gray-100 text-gray-800 font-sans">
    <div class="container mx-auto px-4 py-8 max-w-lg">
        <h1 class="text-2xl font-bold mb-6 text-center">Log Management</h1>
        <div class="bg-white shadow-md rounded-lg p-6">
            <div id="log-files" class="space-y-4">
                <!-- Log files will be populated here -->
            </div>
            <div class="mt-6 flex justify-between items-center">
                <button hx-get="/clear-all-logs" hx-target="#log-files" class="bg-red-500 hover:bg-red-600 text-white font-semibold py-2 px-4 rounded-md text-sm transition duration-300 ease-in-out">
                    Clear All Logs
                </button>
                <span id="storage-info" class="text-sm text-gray-600">
                    <!-- Storage info will be populated here -->
                </span>
            </div>
        </div>
    </div>
</body>
</html>
)rawliteral";



void deleteOldestLogFile() {
  File root = SD.open("/logs");
  File oldestFile;
  unsigned long oldestTime = 0xFFFFFFFF;

  File file = root.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      if (file.getLastWrite() < oldestTime) {
        oldestTime = file.getLastWrite();
        if (oldestFile) oldestFile.close();
        oldestFile = file;
      } else {
        file.close();
      }
    }
    file = root.openNextFile();
  }

  if (oldestFile) {
    String fileName = String(oldestFile.name());
    oldestFile.close();
    SD.remove("/logs/" + fileName);
    Serial.println("Deleted oldest log file: " + fileName);
  }

  root.close();
}



void ensureLogSpace() {
  uint64_t cardSize = SD.cardSize();
  uint64_t usedSpace = SD.usedBytes();
  float usedPercentage = (float)usedSpace / cardSize * 100;

  while (usedPercentage > 95) {
    deleteOldestLogFile();  // Remove the dot (.) before the function name
    usedSpace = SD.usedBytes();
    usedPercentage = (float)usedSpace / cardSize * 100;
  }
}

void handleLogManagement() {
  String output = "<html><body><h1>Log Files</h1><ul>";
  File root = SD.open("/logs");
  File file = root.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      output += "<li><a href='/download-log?file=" + String(file.name()) + "'>" + String(file.name()) + "</a></li>";
    }
    file = root.openNextFile();
  }
  output += "</ul></body></html>";
  server.send(200, "text/html", output);
}

void handleGetLogFiles() {
  String output = "";
  File root = SD.open("/logs");
  File file = root.openNextFile();

  while (file) {
    if (!file.isDirectory()) {
      output += "<div class='flex justify-between items-center'>";
      output += "<span>" + String(file.name()) + "</span>";
      output += "<div>";
      output += "<button hx-get='/download-log?file=" + String(file.name()) + "' class='bg-blue-500 hover:bg-blue-600 text-white font-semibold py-1 px-2 rounded-md text-sm mr-2'>Download</button>";
      output += "<button hx-delete='/delete-log?file=" + String(file.name()) + "' hx-target='#log-files' class='bg-red-500 hover:bg-red-600 text-white font-semibold py-1 px-2 rounded-md text-sm'>Delete</button>";
      output += "</div>";
      output += "</div>";
    }
    file = root.openNextFile();
  }

  server.send(200, "text/html", output);
}

void handleDownloadLog() {
  String filename = server.arg("file");
  File logFile = SD.open("/logs/" + filename);
  if (logFile) {
    server.sendHeader("Content-Disposition", "attachment; filename=" + filename);
    server.streamFile(logFile, "text/csv");
    logFile.close();
  } else {
    server.send(404, "text/plain", "Log file not found");
  }
}
void handleDeleteLog() {
  String filename = server.arg("file");
  if (SD.remove("/logs/" + filename)) {
    handleGetLogFiles();
  } else {
    server.send(500, "text/plain", "Failed to delete log file");
  }
}

void handleClearAllLogs() {
  File root = SD.open("/logs");
  File file = root.openNextFile();

  while (file) {
    if (!file.isDirectory()) {
      SD.remove("/logs/" + String(file.name()));
    }
    file = root.openNextFile();
  }

  handleGetLogFiles();
}

void handleGetStorageInfo() {
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  uint64_t usedSpace = SD.usedBytes() / (1024 * 1024);
  uint64_t freeSpace = cardSize - usedSpace;

  String info = "Total: " + String(cardSize) + " MB, Used: " + String(usedSpace) + " MB, Free: " + String(freeSpace) + " MB";
  server.send(200, "text/plain", info);
}


enum QRPaymentStatus {
  QR_PENDING,
  QR_COMPLETED,
  QR_EXPIRED
};


String getCurrentDate() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "unknown_date";
  }
  char dateStr[11];
  strftime(dateStr, sizeof(dateStr), "%Y-%m-%d", &timeinfo);
  return String(dateStr);
}


String getCurrentDateTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "unknown_datetime";
  }
  char dateTimeStr[20];
  strftime(dateTimeStr, sizeof(dateTimeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(dateTimeStr);
}


void logEvent(const char *event) {
  if (!SD.begin(SD_CS)) {
    Serial.println("SD card initialization failed!");
    return;
  }

  if (!SD.exists("/logs")) {
    if (!SD.mkdir("/logs")) {
      Serial.println("Failed to create logs directory");
      return;
    }
  }

  String filename = "/logs/" + getCurrentDate() + ".csv";
  File logFile = SD.open(filename, FILE_WRITE);
  if (logFile) {
    if (logFile.size() == 0) {
      // If the file is new, add a header
      logFile.println("Timestamp,Event");
    }
    logFile.print(getCurrentDateTime());
    logFile.print(",");
    logFile.println(event);
    logFile.close();
    Serial.println("Event logged successfully");
  } else {
    Serial.println("Error opening log file: " + filename);
  }
}

void displayWhiteOverlay() {
  lv_obj_t *whiteOverlay = lv_obj_create(qrCodeArea);
  lv_obj_set_size(whiteOverlay, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(whiteOverlay, lv_color_white(), 0);
  lv_obj_set_style_border_width(whiteOverlay, 0, 0);
}

void handleCompletedPayment() {
  lv_timer_del(paymentStatusTimer);
  lv_timer_del(countdownTimer);
  displayWhiteOverlay();
  // Log payment completion
  logEvent("Payment completed");
}

void handleExpiredPayment() {
  lv_timer_del(paymentStatusTimer);
  lv_timer_del(countdownTimer);
  displayWhiteOverlay();
  // Log payment expiration
  logEvent("Payment expired");
}

void handleCancelledPayment() {
  // lv_timer_del(paymentStatusTimer);
  displayWhiteOverlay();
  // Log payment cancellation
  logEvent("Payment cancelled");
}

String base64Encode(const String &input) {
  const char base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

  String encoded;
  int i = 0;
  int j = 0;
  uint8_t char_array_3[3];
  uint8_t char_array_4[4];
  int in_len = input.length();
  const char *in = input.c_str();

  while (in_len--) {
    char_array_3[i++] = *(in++);
    if (i == 3) {
      char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
      char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
      char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
      char_array_4[3] = char_array_3[2] & 0x3f;

      for (i = 0; i < 4; i++)
        encoded += base64_chars[char_array_4[i]];
      i = 0;
    }
  }

  if (i) {
    for (j = i; j < 3; j++)
      char_array_3[j] = '\0';

    char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
    char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
    char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
    char_array_4[3] = char_array_3[2] & 0x3f;

    for (j = 0; j < i + 1; j++)
      encoded += base64_chars[char_array_4[j]];

    while (i++ < 3)
      encoded += '=';
  }

  return encoded;
}

QRPaymentStatus checkPaymentStatus() {
  const char *host = "api.razorpay.com";
  const int httpsPort = 443;

  WiFiClientSecure client;
  client.setInsecure();

  if (!client.connect(host, httpsPort)) {
    Serial.println("Connection failed");
    return QR_PENDING;
  }

  String url = "/v1/payments/qr_codes/" + qr_code_id;
  String auth = base64Encode(String(api_key) + ":" + String(api_secret));
  String request = String("GET ") + url + " HTTP/1.1\r\n" + "Host: " + host + "\r\n" + "Authorization: Basic " + auth + "\r\n" + "Connection: close\r\n\r\n";

  client.print(request);

  String response = client.readString();
  int jsonStart = response.indexOf("{");
  String jsonResponse = response.substring(jsonStart);

  DynamicJsonDocument doc(2048);
  deserializeJson(doc, jsonResponse);

  String status = doc["status"];
  String closeReason = doc["close_reason"];

  Serial.print("Payment Status: ");
  Serial.print(status);
  Serial.print(", Close Reason: ");
  Serial.println(closeReason);

  if (status == "closed" && closeReason == "paid") {
    return QR_COMPLETED;
  } else if (status == "closed" && closeReason == "expired") {
    return QR_EXPIRED;
  } else {
    return QR_PENDING;
  }
}


void checkPaymentStatusCallback(lv_timer_t *timer) {
  QRPaymentStatus status = checkPaymentStatus();

  switch (status) {
    case QR_COMPLETED:
      Serial.println("Payment completed");
      handleCompletedPayment();
      break;
    case QR_EXPIRED:
      Serial.println("Payment expired");
      handleExpiredPayment();
      break;
    case QR_PENDING:
      Serial.println("Payment still pending");
      break;
  }
}


void startPaymentStatusMonitoring() {
  // Check payment status every 5 seconds (5000 ms)
  paymentStatusTimer = lv_timer_create(checkPaymentStatusCallback, 5000, NULL);
}

void updateCountdown(lv_timer_t *timer) {
  remainingSeconds--;
  lv_label_set_text_fmt(timerLabel, "%02d:%02d", remainingSeconds / 60, remainingSeconds % 60);
  lv_bar_set_value(progressBar, remainingSeconds, LV_ANIM_ON);

  if (remainingSeconds <= 0) {
    lv_timer_del(timer);
    handleExpiredPayment();
  }
}

void cancelPayment(lv_event_t *e) {
  lv_timer_del(countdownTimer);
  lv_timer_del(paymentStatusTimer);
  handleCancelledPayment();
}


void logPayment(float amount, int quantity) {
  String event = "Payment," + String(amount, 2) + "," + String(quantity);
  logEvent(event.c_str());
}


void displayQRCode(uint8_t *byte_array, int width, int height) {

  // Create a new canvas to draw the QR code
  lv_obj_t *canvas = lv_canvas_create(qrCodeArea);

  // Calculate the size of the canvas buffer
  uint32_t bufferSize = LV_CANVAS_BUF_SIZE_TRUE_COLOR(width, height);
  lv_color_t *buf = (lv_color_t *)malloc(bufferSize);

  if (buf == NULL) {
    Serial.println("Failed to allocate memory for canvas buffer");
    return;
  }

  lv_canvas_set_buffer(canvas, buf, width, height, LV_IMG_CF_TRUE_COLOR);

  // Draw the QR code on the canvas
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      int byte_index = (y * width + x) / 8;
      int bit_index = 7 - ((y * width + x) % 8);
      bool is_black = (byte_array[byte_index] & (1 << bit_index)) != 0;
      lv_canvas_set_px(canvas, x, y, is_black ? lv_color_black() : lv_color_white());
    }
  }

  // Center the canvas in the QR code area
  lv_obj_center(canvas);

  // Set the size of the canvas (adjust as needed)
  lv_obj_set_size(canvas, width, height);

  // Force a redraw of the screen
  lv_obj_invalidate(lv_scr_act());

  // Create timer label
  timerLabel = lv_label_create(qrCodeArea);
  lv_obj_align(timerLabel, LV_ALIGN_TOP_MID, 0, 10);
  lv_obj_set_style_text_font(timerLabel, &lv_font_montserrat_14, 0);

  // Create progress bar
  progressBar = lv_bar_create(qrCodeArea);
  lv_obj_set_size(progressBar, width, 10);
  lv_obj_align(progressBar, LV_ALIGN_TOP_MID, 0, 0);
  lv_bar_set_range(progressBar, 0, 180);
  lv_bar_set_value(progressBar, 180, LV_ANIM_OFF);

  // Start countdown timer
  countdownTimer = lv_timer_create(updateCountdown, 1000, NULL);

  // Create cancel button
  lv_obj_t *cancelBtn = lv_btn_create(qrCodeArea);
  // lv_obj_set_size(cancelBtn, 100, 40);
  lv_obj_align(cancelBtn, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_event_cb(cancelBtn, cancelPayment, LV_EVENT_CLICKED, NULL);
  lv_obj_t *cancelLabel = lv_label_create(cancelBtn);
  lv_obj_set_style_text_font(cancelLabel, &lv_font_montserrat_14, 0);
  lv_label_set_text(cancelLabel, "Cancel");
  lv_obj_center(cancelLabel);

  // Display payment info
  lv_obj_t *paymentInfo = lv_label_create(qrCodeArea);
  lv_obj_align(paymentInfo, LV_ALIGN_TOP_MID, 0, 20);
  lv_obj_set_style_text_font(paymentInfo, &lv_font_montserrat_14, 0);
  int total_amount_now = int(quantity * base_product_mrp);
  lv_label_set_text_fmt(paymentInfo, "Total Amount: %d", total_amount_now);

  // // Display payment info
  // lv_obj_t *paymentInfo_qty = lv_label_create(qrCodeArea);
  // lv_obj_align(paymentInfo_qty, LV_ALIGN_TOP_MID, 0, 30);
  // lv_obj_set_style_text_font(paymentInfo_qty, &lv_font_montserrat_14, 0);
  // lv_label_set_text_fmt(paymentInfo_qty, "Quantity: %d", quantity);

  // // Display payment info
  // lv_obj_t *paymentInfo_color = lv_label_create(qrCodeArea);
  // lv_obj_align(paymentInfo_color, LV_ALIGN_TOP_MID, 0, 40);
  // lv_obj_set_style_text_font(paymentInfo_color, &lv_font_montserrat_14, 0);
  // lv_label_set_text_fmt(paymentInfo_color, "Color: %s", current_color);


  // lv_label_set_text_fmt(paymentInfo, "Amount: ₹%.2f\nQuantity: %d\nColor: %s",
  //                       quantity * base_product_mrp, quantity);


  // Free the buffer after setting it to the canvas
  free(buf);
}


void sendQRCodeURLToBackend(const char *qr_image_url) {
  HTTPClient http;
  WiFiClient client;

  IPAddress espIP = WiFi.localIP();
  String serverIP = "192.168.1.8";  // Make sure this matches your server's IP

  Serial.print("ESP32 IP: ");
  Serial.println(espIP.toString());
  Serial.print("Attempting to connect to server at: http://");
  Serial.println(serverIP);

  // Use String concatenation for the URL
  String url = "http://" + serverIP + ":5000/convert";
  http.begin(client, url);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String postData = "image_url=" + String(qr_image_url);

  Serial.println("Sending POST request to server...");
  int httpResponseCode = http.POST(postData);

  if (httpResponseCode > 0) {
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);

    String payload = http.getString();
    Serial.println("Received payload:");
    Serial.println(payload);

    // Use the streaming API for JSON parsing
    DynamicJsonDocument filter(64);
    filter["width"] = true;
    filter["height"] = true;
    filter["byte_array"] = true;

    DynamicJsonDocument doc(16384);

    DeserializationError error = deserializeJson(doc, payload, DeserializationOption::Filter(filter));

    if (error) {
      Serial.print("deserializeJson() failed: ");
      Serial.println(error.c_str());
      http.end();
      return;
    }

    int width = doc["width"];
    int height = doc["height"];
    String byteArrayStr = doc["byte_array"].as<String>();

    // Convert the byte array string to actual bytes
    uint8_t *qrCodeData = (uint8_t *)malloc(width * height / 8);
    if (!qrCodeData) {
      Serial.println("Failed to allocate memory for QR code data");
      http.end();
      return;
    }

    int index = 0;
    char *token = strtok((char *)byteArrayStr.c_str(), ", ");
    while (token != NULL && index < width * height / 8) {
      qrCodeData[index++] = strtol(token, NULL, 16);
      token = strtok(NULL, ", ");
    }

    // Display the QR code
    displayQRCode(qrCodeData, width, height);

    free(qrCodeData);
  } else {
    Serial.print("Error on sending POST: ");
    Serial.println(httpResponseCode);
    Serial.println(http.errorToString(httpResponseCode));
  }

  http.end();
}



void updateWiFiStatus() {
  return;

  // if (WiFi.status() == WL_CONNECTED) {
  //   lv_label_set_text_fmt(wifiStatusLabel, "IP: %s", WiFi.localIP().toString().c_str());
  // } else {
  //   lv_label_set_text(wifiStatusLabel, "WiFi Not Connected");
  // }
}

void writeString(int addr, String data) {
  for (int i = 0; i < data.length(); i++) {
    EEPROM.write(addr + i, data[i]);
  }
  EEPROM.write(addr + data.length(), '\0');
}
String readString(int addr) {
  String data = "";
  char c = EEPROM.read(addr);
  int i = 0;
  while (c != '\0' && i < 100) {  // Limit to prevent infinite loop
    data += c;
    i++;
    c = EEPROM.read(addr + i);
  }
  return data;
}


void generateQRCode() {
  const char *host = "api.razorpay.com";
  const int httpsPort = 443;
  const char *api_key = "YOUR RAZORPAY API KEY";
  const char *api_secret = "YOUR RAZORPAY API SECRET";

  WiFiClientSecure client;
  client.setInsecure();

  if (!client.connect(host, httpsPort)) {
    Serial.println("Connection failed");
    return;
  }

  configTime(19800, 0, "pool.ntp.org");

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }

  time_t now;
  time(&now);
  time_t close_by = now + 180;

  DynamicJsonDocument doc(32768);
  doc["type"] = "upi_qr";
  doc["usage"] = "single_use";
  doc["fixed_amount"] = true;
  doc["payment_amount"] = static_cast<int>(quantity * base_product_mrp * 100);  // Update line for total amount
  doc["close_by"] = close_by;
  JsonObject notes = doc.createNestedObject("notes");
  notes["notes_key_1"] = " ";
  notes["notes_key_2"] = " ";

  String jsonPayload;
  serializeJson(doc, jsonPayload);

  String auth = base64Encode(String(api_key) + ":" + String(api_secret));
  String request = String("POST /v1/payments/qr_codes HTTP/1.1\r\n") + "Host: " + host + "\r\n" + "Authorization: Basic " + auth + "\r\n" + "Content-Type: application/json\r\n" + "Content-Length: " + jsonPayload.length() + "\r\n\r\n" + jsonPayload;

  Serial.println("Sending request to Razorpay API...");
  client.print(request);

  String response = "";
  unsigned long timeout = millis();
  while (client.connected() && millis() - timeout < 10000) {
    if (client.available()) {
      char c = client.read();
      response += c;
    }
  }

  // Serial.println("Full Response:");
  // Serial.println(response);

  // Find the start of the JSON data
  int jsonStart = response.indexOf("{");
  if (jsonStart == -1) {
    Serial.println("Error: No JSON data found in response");
    return;
  }

  // Extract the JSON part of the response
  String jsonResponse = response.substring(jsonStart);

  Serial.println("JSON Response:");
  Serial.println(jsonResponse);

  DynamicJsonDocument responseDoc(2048);
  DeserializationError error = deserializeJson(responseDoc, jsonResponse);

  if (error) {
    Serial.print("Failed to parse JSON response: ");
    Serial.println(error.c_str());
    return;
  }

  // Check for error in the response
  if (responseDoc.containsKey("error")) {
    Serial.println("Error in API response:");
    serializeJsonPretty(responseDoc["error"], Serial);
    return;
  }


  if (responseDoc.containsKey("id")) {
    qr_code_id = responseDoc["id"].as<String>();
    Serial.println("QR Code ID: " + qr_code_id);

    // Start monitoring payment status immediately after generating QR code
    startPaymentStatusMonitoring();
  }

  const char *qr_image_url = responseDoc["image_url"];
  if (qr_image_url) {
    Serial.println("QR Code URL: " + String(qr_image_url));
    sendQRCodeURLToBackend(qr_image_url);
  } else {
    Serial.println("QR code image URL not found in the response");
  }

  client.stop();
  // Replace the existing logging code with:
  float totalAmount = quantity * base_product_mrp;
  logPayment(totalAmount, quantity);
}


void updateQuantityLabel() {
  lv_label_set_text_fmt(quantityLabel, "%d", quantity);
  // Start the delay timer
  lastButtonPressTime = millis();
  timerSet = 1;
}


void btn_event_handler(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  intptr_t btn_id = (intptr_t)lv_event_get_user_data(e);

  if (code == LV_EVENT_CLICKED) {
    switch (btn_id) {
      case -1:  // Minus button
        if (quantity > 1) quantity--;
        updateQuantityLabel();
        break;
      case 1:  // Plus button
        if (quantity < 99) quantity++;
        updateQuantityLabel();
        break;
      case 0:  // Yellow button
        current_color = "Yellow";
        generateQRCode();  // Regenerate QR code when color changes
        break;
      case 2:  // White button
        current_color = "White";
        generateQRCode();  // Regenerate QR code when color changes
        break;
    }
  }
}

void saveSettings() {
  int addr = 0;
  EEPROM.put(addr, base_product_mrp);
  addr += sizeof(float);
  writeString(addr, serverIP);
  addr += serverIP.length() + 1;
  EEPROM.put(addr, serverPort);
  addr += sizeof(int);
  writeString(addr, storeName);
  addr += storeName.length() + 1;
  writeString(addr, api_key);
  addr += api_key.length() + 1;
  writeString(addr, api_secret);
  addr += api_secret.length() + 1;
  EEPROM.commit();
}
void handleRoot() {
  String html = FPSTR(ADMIN_HTML);
  html.replace("%BASE_PRODUCT_MRP%", String(base_product_mrp));
  html.replace("%SERVER_IP%", serverIP);
  html.replace("%SERVER_PORT%", String(serverPort));
  html.replace("%STORE_NAME%", storeName);
  html.replace("%API_KEY%", api_key);
  html.replace("%API_SECRET%", api_secret);
  server.send(200, "text/html", html);
}

void handleSaveSettings() {
  if (server.hasArg("base_product_mrp")) base_product_mrp = server.arg("base_product_mrp").toFloat();
  if (server.hasArg("server_ip")) serverIP = server.arg("server_ip");
  if (server.hasArg("server_port")) serverPort = server.arg("server_port").toInt();
  if (server.hasArg("store_name")) storeName = server.arg("store_name");
  if (server.hasArg("api_key")) api_key = server.arg("api_key");
  if (server.hasArg("api_secret")) api_secret = server.arg("api_secret");

  saveSettings();

  server.send(200, "text/plain", "Settings saved successfully!");
}

void handleDownloadLogs() {
  File logFile = SD.open("/payment_log.txt");
  if (logFile) {
    server.streamFile(logFile, "text/plain");
    logFile.close();
  } else {
    server.send(404, "text/plain", "Log file not found");
  }
}

void loadSettings() {
  int addr = 0;
  EEPROM.get(addr, base_product_mrp);
  addr += sizeof(float);
  serverIP = readString(addr);
  addr += serverIP.length() + 1;
  EEPROM.get(addr, serverPort);
  addr += sizeof(int);
  storeName = readString(addr);
  addr += storeName.length() + 1;
  api_key = readString(addr);
  addr += api_key.length() + 1;
  api_secret = readString(addr);
  addr += api_secret.length() + 1;
}

void createUI() {
  // Create a background style
  lv_style_init(&style_background);
  lv_style_set_bg_color(&style_background, lv_color_hex(0xE0E0E0));
  lv_style_set_pad_all(&style_background, 5);
  lv_obj_add_style(lv_scr_act(), &style_background, 0);

  // Create a button style
  lv_style_init(&style_btn);
  lv_style_set_radius(&style_btn, 5);
  lv_style_set_bg_color(&style_btn, lv_color_hex(0x2196F3));
  lv_style_set_bg_grad_color(&style_btn, lv_color_hex(0x21CCF3));
  lv_style_set_bg_grad_dir(&style_btn, LV_GRAD_DIR_VER);
  lv_style_set_bg_opa(&style_btn, 255);
  lv_style_set_border_color(&style_btn, lv_color_hex(0x1976D2));
  lv_style_set_border_width(&style_btn, 1);
  lv_style_set_text_color(&style_btn, lv_color_white());
  lv_style_set_text_font(&style_btn, &lv_font_montserrat_14);

  // Left side container (1/2 of the screen width)
  lv_obj_t *left_container = lv_obj_create(lv_scr_act());
  lv_obj_set_size(left_container, screenWidth / 2 - 10, screenHeight - 10);
  lv_obj_align(left_container, LV_ALIGN_LEFT_MID, 2.5, 0);
  lv_obj_set_style_bg_color(left_container, lv_color_hex(0xF5F5F5), 0);
  lv_obj_set_style_pad_all(left_container, 5, 0);

  // Product image area
  lv_obj_t *imgArea = lv_obj_create(left_container);
  lv_obj_set_size(imgArea, 160, 120);
  lv_obj_align(imgArea, LV_ALIGN_TOP_MID, 0, 5);
  lv_obj_set_style_bg_color(imgArea, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_color(imgArea, lv_color_hex(0xBDBDBD), 0);
  lv_obj_set_style_border_width(imgArea, 1, 0);

  // // Create an image descriptor
  // static lv_img_dsc_t img_dsc;
  // img_dsc.header.w = 100;
  // img_dsc.header.h = 100;
  // img_dsc.header.cf = LV_IMG_CF_INDEXED_1BIT;
  // img_dsc.data = epd_bitmap_mavo;

  // // Create an image object
  // lv_obj_t *img = lv_img_create(imgArea);
  // lv_img_set_src(img, &img_dsc);
  // lv_obj_center(img);

  // Product name
  productNameLabel = lv_label_create(left_container);
  lv_obj_align(productNameLabel, LV_ALIGN_TOP_MID, 0, 130);
  lv_obj_set_style_text_font(productNameLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(productNameLabel, lv_color_hex(0x212121), 0);
  lv_label_set_text(productNameLabel, "135 Mavo");

  // Quantity selection area
  lv_obj_t *quantityArea = lv_obj_create(left_container);
  lv_obj_set_size(quantityArea, 200, 40);
  lv_obj_align(quantityArea, LV_ALIGN_TOP_MID, 0, 150);
  lv_obj_set_flex_flow(quantityArea, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(quantityArea, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_bg_color(quantityArea, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_color(quantityArea, lv_color_hex(0xBDBDBD), 0);
  lv_obj_set_style_border_width(quantityArea, 1, 0);
  lv_obj_set_style_pad_all(quantityArea, 2, 0);

  lv_obj_t *minusBtn = lv_btn_create(quantityArea);
  lv_obj_set_size(minusBtn, 30, 30);
  lv_obj_add_style(minusBtn, &style_btn, 0);
  lv_obj_add_event_cb(minusBtn, btn_event_handler, LV_EVENT_CLICKED, (void *)-1);
  lv_obj_t *minusLabel = lv_label_create(minusBtn);
  lv_label_set_text(minusLabel, "-");
  lv_obj_center(minusLabel);

  quantityLabel = lv_label_create(quantityArea);
  lv_obj_set_style_text_font(quantityLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(quantityLabel, lv_color_hex(0x212121), 0);
  updateQuantityLabel();

  lv_obj_t *plusBtn = lv_btn_create(quantityArea);
  lv_obj_set_size(plusBtn, 30, 30);
  lv_obj_add_style(plusBtn, &style_btn, 0);
  lv_obj_add_event_cb(plusBtn, btn_event_handler, LV_EVENT_CLICKED, (void *)1);
  lv_obj_t *plusLabel = lv_label_create(plusBtn);
  lv_label_set_text(plusLabel, "+");
  lv_obj_center(plusLabel);

  // Color selection area
  lv_obj_t *colorArea = lv_obj_create(left_container);
  lv_obj_set_size(colorArea, 200, 42);
  lv_obj_align(colorArea, LV_ALIGN_TOP_MID, 0, 200);
  lv_obj_set_flex_flow(colorArea, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(colorArea, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_bg_color(colorArea, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_color(colorArea, lv_color_hex(0xBDBDBD), 0);
  lv_obj_set_style_border_width(colorArea, 1, 0);
  lv_obj_set_style_pad_all(colorArea, 2, 0);

  lv_obj_t *yellowBtn = lv_btn_create(colorArea);
  lv_obj_set_size(yellowBtn, 90, 30);
  lv_obj_add_style(yellowBtn, &style_btn, 0);
  lv_obj_set_style_bg_color(yellowBtn, lv_color_hex(0xFFC107), 0);
  lv_obj_set_style_bg_grad_color(yellowBtn, lv_color_hex(0xFFEB3B), 0);
  lv_obj_add_event_cb(yellowBtn, btn_event_handler, LV_EVENT_CLICKED, (void *)0);
  lv_obj_t *yellowLabel = lv_label_create(yellowBtn);
  lv_label_set_text(yellowLabel, "Yellow");
  lv_obj_center(yellowLabel);

  lv_obj_t *whiteBtn = lv_btn_create(colorArea);
  lv_obj_set_size(whiteBtn, 90, 30);
  lv_obj_add_style(whiteBtn, &style_btn, 0);
  lv_obj_set_style_bg_color(whiteBtn, lv_color_hex(0x4CAF50), 0);
  lv_obj_set_style_bg_grad_color(whiteBtn, lv_color_hex(0x8BC34A), 0);
  lv_obj_add_event_cb(whiteBtn, btn_event_handler, LV_EVENT_CLICKED, (void *)2);
  lv_obj_t *whiteLabel = lv_label_create(whiteBtn);
  lv_label_set_text(whiteLabel, "White");
  lv_obj_center(whiteLabel);

  // QR Code display area (right 1/2 of the screen)
  qrCodeArea = lv_obj_create(lv_scr_act());
  lv_obj_set_size(qrCodeArea, screenWidth / 2 - 10, screenHeight - 10);
  lv_obj_align(qrCodeArea, LV_ALIGN_RIGHT_MID, -2.5, 0);
  lv_obj_set_style_bg_color(qrCodeArea, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_color(qrCodeArea, lv_color_hex(0xBDBDBD), 0);
  lv_obj_set_style_border_width(qrCodeArea, 1, 0);
  // // WiFi status (small text in corner)
  // wifiStatusLabel = lv_label_create(qrCodeArea);
  // lv_obj_align(wifiStatusLabel, LV_ALIGN_TOP_MID, 0, 0);
  // lv_obj_set_style_text_font(wifiStatusLabel, &lv_font_montserrat_14, 0);
  // lv_obj_set_style_text_color(wifiStatusLabel, lv_color_hex(0x212121), 0);
  // updateWiFiStatus();
}

void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);

#if (LV_COLOR_16_SWAP != 0)
  gfx->draw16bitBeRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
#else
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
#endif

  lv_disp_flush_ready(disp);
}


void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
  if (touch_has_signal()) {
    if (touch_touched()) {
      data->state = LV_INDEV_STATE_PR;
      /*Set the coordinates*/
      data->point.x = touch_last_x;
      data->point.y = touch_last_y;
    } else if (touch_released()) {
      data->state = LV_INDEV_STATE_REL;
    }
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}


void reconfigureWiFi() {
  wifiManager.startConfigPortal("QRDisplayAP");
  updateWiFiStatus();
}

void setup() {
  Serial.begin(115200);
  Serial.println("QR Code Display Demo");


  // Initialize EEPROM
  EEPROM.begin(EEPROM_SIZE);

  // Initialize SD card
  if (!SD.begin(SD_CS)) {
    Serial.println("SD card initialization failed!");
  } else {
    Serial.println("SD card initialized successfully.");
    if (!SD.exists("/logs")) {
      if (SD.mkdir("/logs")) {
        Serial.println("Logs directory created successfully.");
      } else {
        Serial.println("Failed to create logs directory.");
      }
    }
  }

  // Load settings from EEPROM
  loadSettings();

  // Initialize display
  gfx->begin();
  gfx->fillScreen(BLACK);

  // Init touch device
  touch_init();

#ifdef TFT_BL
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
#endif

  screenWidth = gfx->width();
  screenHeight = gfx->height();

  lv_init();

  disp_draw_buf = (lv_color_t *)heap_caps_malloc(sizeof(lv_color_t) * screenWidth * 60, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!disp_draw_buf) {
    Serial.println("LVGL disp_draw_buf allocate failed!");
  } else {
    lv_disp_draw_buf_init(&draw_buf, disp_draw_buf, NULL, screenWidth * 60);

    /* Initialize the display */
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = screenWidth;
    disp_drv.ver_res = screenHeight;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    /* Initialize the input device driver */
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);

    // Initialize WiFi
    WiFi.mode(WIFI_STA);  // Set WiFi to station mode
    wifiManager.autoConnect("QRDisplayAP");

    // Set up web server routes
    server.on("/", HTTP_GET, handleRoot);
    server.on("/save-settings", HTTP_POST, handleSaveSettings);
    // server.on("/download-logs", HTTP_GET, handleDownloadLogs);

    // Set up web server routes
    server.on("/", HTTP_GET, handleRoot);
    server.on("/save-settings", HTTP_POST, handleSaveSettings);
    server.on("/download-logs", HTTP_GET, handleLogManagement);  // Changed this line
    server.on("/get-log-files", HTTP_GET, handleGetLogFiles);
    server.on("/download-log", HTTP_GET, handleDownloadLog);
    server.on("/delete-log", HTTP_DELETE, handleDeleteLog);
    server.on("/clear-all-logs", HTTP_GET, handleClearAllLogs);
    server.on("/get-storage-info", HTTP_GET, handleGetStorageInfo);

    // Start web server
    server.begin();
    Serial.println("HTTP server started");

    // Create UI
    createUI();

    updateWiFiStatus();

    Serial.println("Setup done");
  }
}

void loop() {
  lv_timer_handler(); /* let the GUI do its work */
  lv_task_handler();  // Let LVGL do its work

  // Handle web server client requests
  server.handleClient();

  // Check WiFi status periodically and update UI
  static unsigned long lastWiFiCheck = 0;
  if (millis() - lastWiFiCheck > 5000) {  // Check every 5 seconds
    updateWiFiStatus();
    lastWiFiCheck = millis();
  }

  // Check if it's time to generate a new QR code
  if (timerSet && (millis() - lastButtonPressTime > DELAY_TIME) && (last_qty != quantity)) {
    generateQRCode();
    timerSet = 0;
    last_qty = quantity;
  }

  delay(5);
}


