#define TINY_GSM_MODEM_SIM800
#include <Wire.h>
#include <TinyGsmClient.h>
#include <ArduinoHttpClient.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>

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
  modem.restart();

  // Unlock SIM card if needed
  if (strlen(simPIN) && modem.getSimStatus() != 3) {
    modem.simUnlock(simPIN);
  }

  // Fetch and save JSON data
  fetchAndSaveJson();
}

void loop() {
  // Check if RFID data is available
  if (Serial.available()) {
    String rfidData = "";
    while (Serial.available()) {
      char c = Serial.read();
      rfidData += c;
      delay(10);
    }
    // Indicate RFID read success
    digitalWrite(buzzer, HIGH);
    delay(500);
    digitalWrite(buzzer, LOW);
    Serial.print("RFID Tag: ");
    Serial.println(rfidData);

    // Validate RFID tag
    validateRFIDTag(rfidData);
  }
}

// Function to fetch JSON data and save it to SPIFFS
void fetchAndSaveJson() {
  SerialMon.println("Connecting to APN...");
  if (!modem.gprsConnect(apn, gprsUser, gprsPass)) {
    SerialMon.println("Failed to connect to GPRS");
    return;
  }

  String resource = "/findStudentsByBus.php?bus_no=8398";
  SerialMon.print("Requesting URL: ");
  SerialMon.println(resource);

  int err = http.get(resource.c_str());
  if (err != 0) {
    SerialMon.println("HTTP GET failed");
    return;
  }

  int status = http.responseStatusCode();
  SerialMon.print("Response status code: ");
  SerialMon.println(status);

  if (status == 200) {
    String response = http.responseBody();
    SerialMon.println("JSON Response:");
    SerialMon.println(response);

    // Save JSON to SPIFFS
    File file = SPIFFS.open("/bus_data.json", FILE_WRITE);
    if (!file) {
      SerialMon.println("Failed to open file for writing");
      return;
    }
    file.print(response);
    file.close();
    SerialMon.println("JSON data saved to SPIFFS");
  } else {
    SerialMon.println("Failed to fetch JSON");
  }
  http.stop();
}

void validateRFIDTag(String rfidTag) {
  File file = SPIFFS.open("/bus_data.json", FILE_READ);
  if (!file) {
    SerialMon.println("Failed to open file for reading");
    return;
  }

  // Read JSON data
  String jsonString = file.readString();
  file.close();

  // Parse JSON
  StaticJsonDocument<8192> doc;
  DeserializationError error = deserializeJson(doc, jsonString);
  if (error) {
    SerialMon.print("Failed to parse JSON: ");
    SerialMon.println(error.c_str());
    return;
  }

  JsonArray data = doc["data"];
  for (JsonObject student : data) {
    String cardTag = student["card_tag"];
    if (cardTag == rfidTag) {
      SerialMon.println("RFID Tag Matched!");

      // Get the balance_amount
      float balanceAmount = student["balance_amount"].as<float>();
      SerialMon.print("Balance Amount: ");
      SerialMon.println(balanceAmount);

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
  SerialMon.println("RFID Tag Not Found!");
  digitalWrite(rled, HIGH);
  delay(2000);
  digitalWrite(rled, LOW);
}

