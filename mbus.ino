#define TINY_GSM_MODEM_SIM800
#include <Wire.h>
#include <TinyGsmClient.h>
#include <ArduinoHttpClient.h>
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
#define TINY_GSM_MODEM_SIM800
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
  SerialMon.println("RFID Reader Ready on UART0...");
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
    // Send the RFID data to the server
    sendRFIDDataToServer(rfidData);
  }
}
// Function to send RFID data to the server
void sendRFIDDataToServer(String cardTag) {
  SerialMon.print("Connecting to APN: ");
  SerialMon.println(apn);
  if (!modem.gprsConnect(apn, gprsUser, gprsPass)) {
    SerialMon.println("Failed to connect to GPRS");
    delay(1000);
    return;
  }
  // Construct the HTTP request URL with the RFID tag
  String resource = "/scanTag.php?card_tag=" + cardTag;
  SerialMon.print("Requesting URL: ");
  SerialMon.println(resource);
  // Perform the HTTP GET request
  SerialMon.print("Performing HTTP GET request... ");
  int err = http.get(resource.c_str());
  if (err != 0) {
    SerialMon.println("Failed to connect");
    delay(10000);
    return;
  }
  int status = http.responseStatusCode();
  SerialMon.print("Response status code: ");
  SerialMon.println(status);
  // Process the response if the status is OK (200)
  if (status == 200) {
    String body = http.responseBody();
    SerialMon.println("Response Body:");
    SerialMon.println(body);
    // Parse the JSON response
    if (body.indexOf("\"response\":\"yes\"") >= 0) {
      Serial.println("Response: Yes");
      // Green LED for "yes" response
      digitalWrite(gled, HIGH);
      delay(2000);
      digitalWrite(gled, LOW);
    } else {
      // Red LED for any other response
      digitalWrite(rled, HIGH);
      delay(2000);
      digitalWrite(rled, LOW);
    }
  } else {
    SerialMon.println("Error: Non-200 response status code");
    // Indicate error with red LED
    digitalWrite(rled, HIGH);
    delay(2000);
    digitalWrite(rled, LOW);
  }
  http.stop();
  SerialMon.println("Server disconnected");
}
