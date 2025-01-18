#define TINY_GSM_MODEM_SIM800
#include <Wire.h>
#include <TinyGsmClient.h>
#include <ArduinoHttpClient.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <time.h>
// GPRS credentials
const char apn[]      = "airtelgprs.com"; // APN
const char gprsUser[] = "";               // GPRS User
const char gprsPass[] = "";               // GPRS Password
// SIM card PIN
const char simPIN[]   = "";

// Server details
const char server[] = "62.72.58.116";  // Public HTTP server
const int port = 80;                   // HTTP port

// TTGO T-Call pins
#define MODEM_RST            5
#define MODEM_PWKEY          4
#define MODEM_POWER_ON       23
#define MODEM_TX             27
#define MODEM_RX             26
#define I2C_SDA              21
#define I2C_SCL              22
#define SerialMon Serial
#define SerialAT Serial1
#define TINY_GSM_RX_BUFFER   1024  // Set RX buffer to 1KB

TinyGsm modem(SerialAT);
TinyGsmClient client(modem);
HttpClient http(client, server, port);

// RFID and Buzzer Pins
int buzzer = 19;
int rled = 14;
int gled = 12;

unsigned long lastUploadTime = 0;
const unsigned long uploadInterval = 21600000;  // Upload every 6 hours

void setup() {
  SerialMon.begin(115200);  // Serial monitor
  Serial.begin(9600);       // RFID reader

  // Initialize SPIFFS
  if (!SPIFFS.begin(true)) {
    SerialMon.println("Failed to mount SPIFFS");
    return;
  }

  // Initialize buzzer and LEDs
  pinMode(buzzer, OUTPUT);
  pinMode(rled, OUTPUT);
  pinMode(gled, OUTPUT);

  // Power management setup
  pinMode(MODEM_PWKEY, OUTPUT);
  pinMode(MODEM_RST, OUTPUT);
  pinMode(MODEM_POWER_ON, OUTPUT);
  digitalWrite(MODEM_PWKEY, LOW);
  digitalWrite(MODEM_RST, HIGH);
  digitalWrite(MODEM_POWER_ON, HIGH);

  // SIM800 setup
  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(1500);

  // Initialize the modem
  SerialMon.println("Initializing modem...");
  if (!modem.restart()) {
    SerialMon.println("Failed to restart modem");
    return;
  }

  // Unlock SIM card if needed
  if (strlen(simPIN) && modem.getSimStatus() != 3) {
    if (!modem.simUnlock(simPIN)) {
      SerialMon.println("Failed to unlock SIM");
      return;
    }
  }

  // Fetch and save JSON data
  fetchAndSaveJson();
}

void loop() {
  String rfidData = "4A00A51922D4";

  digitalWrite(buzzer, HIGH);
  delay(500);
  digitalWrite(buzzer, LOW);
  Serial.print("RFID Tag (Test): ");
  Serial.println(rfidData);

  validateRFIDTag(rfidData);

  // Periodically upload scanned tags
  unsigned long currentTime = millis();
  if (currentTime - lastUploadTime >= uploadInterval) {
    uploadScannedTags();
    lastUploadTime = currentTime;
  }

  delay(5000);  // Wait 5 seconds before repeating
}

// Function to fetch JSON data and save it to SPIFFS
void fetchAndSaveJson() {
  SerialMon.println("Connecting to APN...");
  if (!modem.gprsConnect(apn, gprsUser, gprsPass)) {
    SerialMon.println("Failed to connect to GPRS");
    return;
  }
  SerialMon.println("Connected to GPRS");

  SerialMon.println("Requesting URL: /findStudentsByBus.php?bus_no=8398");
  http.get("/findStudentsByBus.php?bus_no=8398");

  int statusCode = http.responseStatusCode();
  SerialMon.print("Response status code: ");
  SerialMon.println(statusCode);

  if (statusCode != 200) {
    SerialMon.println("Failed to get valid response");
    modem.gprsDisconnect();
    return;
  }

  String response = http.responseBody();
  SerialMon.println("JSON Response:");
  SerialMon.println(response);

  // Save JSON data to SPIFFS
  File file = SPIFFS.open("/data.json", FILE_WRITE);
  if (!file) {
    SerialMon.println("Failed to open file for writing");
    modem.gprsDisconnect();
    return;
  }

  file.print(response);
  file.close();
  SerialMon.println("JSON data saved to SPIFFS");

  modem.gprsDisconnect();
}

// Function to validate RFID tag against JSON data
void validateRFIDTag(const String &tag) {
  File file = SPIFFS.open("/data.json", FILE_READ);
  if (!file) {
    SerialMon.println("Failed to open file for reading");
    return;
  }

  size_t fileSize = file.size();
  if (fileSize == 0) {
    SerialMon.println("File is empty");
    file.close();
    return;
  }

  // Read file content into memory
  std::unique_ptr<char[]> jsonBuffer(new char[fileSize + 1]);
  file.readBytes(jsonBuffer.get(), fileSize);
  jsonBuffer[fileSize] = '\0';
  file.close();

  // Parse JSON
  DynamicJsonDocument doc(8192);
  DeserializationError error = deserializeJson(doc, jsonBuffer.get());
  if (error) {
    SerialMon.print("Failed to parse JSON: ");
    SerialMon.println(error.c_str());
    return;
  }

  // Search for RFID tag in data
  JsonArray dataArray = doc["data"].as<JsonArray>();
  for (JsonObject student : dataArray) {
    if (student["card_tag"] == tag) {
      SerialMon.println("RFID Tag found in data!");

      // Read the balance amount
      float balanceAmount = student["balance_amount"].as<float>();
      SerialMon.print("Balance Amount: ");
      SerialMon.println(balanceAmount);

      saveScannedTag(tag, balanceAmount);

      // Check balance and blink appropriate LED
      if (balanceAmount > 5000) {
        // Blink red LED for "balance > 5000"
        SerialMon.println("Balance is greater than 5000. Blinking Red LED.");
        for (int i = 0; i < 5; i++) { // Blink 5 times
          digitalWrite(rled, HIGH);
          delay(200);
          digitalWrite(rled, LOW);
          delay(200);
        }
      } else {
        // Blink green LED for "balance <= 5000"
        SerialMon.println("Balance is less than or equal to 5000. Blinking Green LED.");
        for (int i = 0; i < 5; i++) { // Blink 5 times
          digitalWrite(gled, HIGH);
          delay(200);
          digitalWrite(gled, LOW);
          delay(200);
        }
      }
      return; // Exit after finding the RFID tag
    }
  }

  // If RFID tag is not found
  SerialMon.println("RFID Tag not found");
  digitalWrite(rled, HIGH);  // Red LED ON
  delay(1000);
  digitalWrite(rled, LOW);   // Red LED OFF
}
void saveScannedTag(const String &tag, float balance) {
  // Open the scanned tags file
  File file = SPIFFS.open("/scanned_tags.json", FILE_READ);
  DynamicJsonDocument doc(8192);

  if (!file) {
    SerialMon.println("Creating new scanned tags file...");
  } else {
    // Read existing data
    size_t fileSize = file.size();
    if (fileSize > 0) {
      std::unique_ptr<char[]> jsonBuffer(new char[fileSize + 1]);
      file.readBytes(jsonBuffer.get(), fileSize);
      jsonBuffer[fileSize] = '\0';
      deserializeJson(doc, jsonBuffer.get());
    }
    file.close();
  }

  // Ensure the root is an array
  JsonArray tagsArray = doc.to<JsonArray>();

  // Add the new scanned tag
  JsonObject newTag = tagsArray.createNestedObject();
  newTag["rfid_tag"] = tag;
  newTag["balance"] = balance;

  // Add timestamp if RTC is available
  time_t now = time(nullptr);
  if (now > 0) {  // Valid timestamp
    newTag["timestamp"] = String(ctime(&now));
  } else {
    newTag["timestamp"] = "unknown";
  }

  // Save the updated array back to the file
  file = SPIFFS.open("/scanned_tags.json", FILE_WRITE);
  if (!file) {
    SerialMon.println("Failed to open file for writing");
    return;
  }
  serializeJson(doc, file);
  file.close();
  SerialMon.println("Scanned tag saved.");
}

void uploadScannedTags() {
  // Open the scanned tags file
  File file = SPIFFS.open("/scanned_tags.json", FILE_READ);
  if (!file || file.size() == 0) {
    SerialMon.println("No scanned tags to upload.");
    return;
  }

  // Read the file contents
  String jsonPayload;
  while (file.available()) {
    jsonPayload += char(file.read());
  }
  file.close();

  // Connect to GPRS
  if (!modem.gprsConnect(apn, gprsUser, gprsPass)) {
    SerialMon.println("Failed to connect to GPRS for uploading");
    return;
  }

  // Make HTTP POST request to upload scanned tags
  SerialMon.println("Uploading scanned tags...");
  http.beginRequest();
  http.post("/uploadScannedTags.php", "application/json", jsonPayload);
  http.endRequest();

  // Read response
  int statusCode = http.responseStatusCode();
  String responseBody = http.responseBody();
  SerialMon.print("Status code: ");
  SerialMon.println(statusCode);
  SerialMon.println("Response: ");
  SerialMon.println(responseBody);

  if (statusCode == 200) {
    SerialMon.println("Scanned tags uploaded successfully. Clearing local data...");
    SPIFFS.remove("/scanned_tags.json");  // Clear the local file after successful upload
  } else {
    SerialMon.println("Failed to upload scanned tags. Keeping local data.");
  }

  modem.gprsDisconnect();
}

