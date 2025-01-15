#include <ESP8266WiFi.h>
#include <Firebase.h>
#include <MFRC522.h>
#include <Servo.h>
#include "secrets.h"
#include <SPI.h>
#include <time.h>

// Allowed UID
String allowedUIDs[] = {"53bf4f6"};

// Firebase instance
Firebase fb(REFERENCE_URL);  // Use FIREBASE_AUTH if required (e.g., Firebase fb(FIREBASE_URL, FIREBASE_AUTH);)

// Servo motor pin
Servo myServo;
int servoPin = D4;  // GPIO2 for servo signal

// RC522 RFID module pins
#define SS_PIN D2    // SDA
#define RST_PIN D1   // RST

MFRC522 mfrc522(SS_PIN, RST_PIN); // Create MFRC522 instance

bool isOpen = false;

void setup() {
  // Start serial communication for debugging
  Serial.begin(115200);

  // Connect to Wi-Fi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");

  
  // Configuring the time
  configTime(3600, 0, "pool.ntp.org"); // UTC time; adjust with time zone offset if needed
  Serial.println("Waiting for NTP time sync...");
  while (time(nullptr) < 100000) {  // Wait until time is synced
    delay(500);
    Serial.print(".");
  }
  Serial.println("Time synced!");

  // Initialize RC522
  SPI.begin();
  mfrc522.PCD_Init();

  // Initialize servo
  myServo.attach(servoPin);
  myServo.write(0);  // Servo at initial position (closed)
  isOpen = false;
  delay(1000);

  // Test Firebase connection
  if (fb.setString("testConnection", "success") == 200) {
    Serial.println("Connected to Firebase");
  } else {
    Serial.println("Failed to connect to Firebase");
  }
}

unsigned long lastFetchTime = 0;  // Store the last time fetchIsOpen was called
const unsigned long fetchInterval = 4000;  // Set the fetch interval (e.g., 5000ms = 5 seconds)

void loop() {
    unsigned long currentMillis = millis();

    // Call fetchIsOpen only if enough time has passed
    if (currentMillis - lastFetchTime >= fetchInterval) {
        fetchIsOpen();
        lastFetchTime = currentMillis;  // Update the last fetch time
    }

    // RFID card detection runs continuously
    cardDetection();
}

void fetchIsOpen(){
  String openState = fb.getString("/isOpen");
  int currentPos = myServo.read();

  if(openState == "true"){
    isOpen = true;

    if (currentPos == 0) { // If closed
      myServo.write(180); // Open the lock
      isOpen = true;
      logStateToFirebase("opened", "system");
      delay(2000);
    }
  }
  else if(openState == "false"){
    isOpen = false;

    if (currentPos == 180) { // If opened
      myServo.write(0); // Close the lock
      isOpen = false;
      logStateToFirebase("closed", "system");
      delay(2000);
    }
  }
  else{
    Serial.println("Failed to fetch open/closed state from Firebase!");
  }
}

void cardDetection(){
  // Look for new RFID cards
  if (mfrc522.PICC_IsNewCardPresent()) {
    if (mfrc522.PICC_ReadCardSerial()) {
      String uid = "";
      for (byte i = 0; i < mfrc522.uid.size; i++) {
        uid += String(mfrc522.uid.uidByte[i], HEX);
      }
      //uid.toLowerCase(); // Ensure consistent case
      Serial.print("UID of the card: ");
      Serial.println(uid);

      // Check if the card is authorized (you can add more IDs here)
      if (isAuthorized(uid)) {
        // Open or close the lock based on current state
        int currentPos = myServo.read();
        if (currentPos == 0) { // If closed
          myServo.write(180); // Open the lock
          isOpen = true;
          logStateToFirebase("opened", "card(" + uid + ")");
        } else { // If opened
          myServo.write(0); // Close the lock
          isOpen = false;
          logStateToFirebase("closed", "card(" + uid + ")");
        }
        delay(2000); // Wait before scanning again
      } else {
        Serial.println("Unauthorized card.");
      }
    }
  }
}

bool isAuthorized(String uid){
    for (int i = 0; i < sizeof(allowedUIDs) / sizeof(allowedUIDs[0]); i++) {
        if (uid == allowedUIDs[i]) {
            return true; // UID is authorized
        }
    }
    return false; // UID is not authorized
}

String getFormattedTime() {
  time_t now = time(nullptr);
  struct tm *timeinfo = localtime(&now);

  char buffer[30];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
  return String(buffer);
}

void logStateToFirebase(String state, String operatedBy) {
    // Get the current timestamp
    String timestamp = getFormattedTime();

    // Create a log entry
    String logEntry = timestamp + " - Gate state: " + state + " - By: " + operatedBy;

    // Push the log entry to Firebase
    if (fb.pushString("/gateLogs", logEntry)) {
        fb.setBool("/isOpen", isOpen);
        Serial.println("Log added successfully: " + logEntry);
    } else {
        Serial.println("Failed to log state!");
    }
}


