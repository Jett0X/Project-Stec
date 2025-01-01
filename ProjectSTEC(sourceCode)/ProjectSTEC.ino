#include <math.h>

// Start Wifi
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <Adafruit_MLX90614.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

#define WIFI_SSID "Adrian"
#define WIFI_PASSWORD "12345678"
// End Wifi

// Start Firebase
#include "addons/TokenHelper.h"                                                                              // Provide the token generation process info.
#include "addons/RTDBHelper.h"                                                                               // Provide the RTDB payload printing info and other helper functions.
#define API_KEY "AIzaSyCYoNcQcuPnTVuq1I5faBYpHKIM6jo_0oU"                                                    // Insert Firebase project API Key
#define DATABASE_URL "https://esp32-firebase-demo-19474-default-rtdb.asia-southeast1.firebasedatabase.app/"  // Insert RTDB URL

FirebaseData FirebaseData;  // Firebase data object
FirebaseAuth auth;          // Firebase authentication object
FirebaseConfig config;      // Firebase configuration object

TaskHandle_t PostToFirebase;
bool signupOK = false;
// End Firebase

// Start Function Declaration
void SendReadingsToFirebase();
void InitializeWifi();
void SignUpToFirebase();
void InitializePOX();
// End Function Declaration

// Start Pulse Oximeter
#include <Wire.h>
#include "MAX30100_PulseOximeter.h"
#define POX_REPORTING_PERIOD_MS 1000

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 28800, 60000);  // 28800 = UTC+8 for Philippine time (8 * 3600)

PulseOximeter pox;  // Create a PulseOximeter object

TaskHandle_t GetReadings;
uint8_t _spo2;
uint8_t _heartRate;

uint32_t poxLastReport = 0;
uint32_t prevMillis = 0;
// End Pulse Oximeter

#define BUTTON_PIN 12  // Define the button pin
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDRESS 0x3C

Adafruit_MLX90614 temperatureSensor = Adafruit_MLX90614();
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

void setup() {
  Serial.begin(115200);  // Begin serial communication

  pinMode(BUTTON_PIN, INPUT_PULLUP);  // Configure pin 12 as an input with pull-up resistor

  InitializeWifi();

  SignUpToFirebase();

  InitializePOX();

  initializeTemperatureSensor();

  initializeOledDisplay();

  timeClient.begin();

  xTaskCreatePinnedToCore(SensorReadings, "GetReadings", 1724, NULL, 0, &GetReadings, 0);

  xTaskCreatePinnedToCore(SendReadingsToFirebase, "PostToFirebase", 6268, NULL, 0, &PostToFirebase, 1);
}

void SensorReadings(void* parameter) {
  for (;;) {
    // Read from the sensor
    pox.update();

    double objectTemp = temperatureSensor.readObjectTempC();
    double ambientTemp = temperatureSensor.readAmbientTempC();


    if (millis() - poxLastReport > POX_REPORTING_PERIOD_MS) {
      _heartRate = round(pox.getHeartRate());
      _spo2 = round(pox.getSpO2());

      Serial.print("Heart rate:");
      Serial.print(_heartRate);
      Serial.print("bpm / SpO2:");
      Serial.print(_spo2);
      Serial.println("%");

      display.clearDisplay();
      display.setCursor(0, 0);
      display.print("Ambient Temp: ");
      display.print(ambientTemp);
      display.println(" C");
      display.print("Object Temp: ");
      display.print(objectTemp);
      display.println(" C");
      display.print("Heart rate:");
      display.print(_heartRate);
      display.println("bpm");
      display.print("SpO2: ");
      display.print(_spo2);
      display.println("%");

      display.display();

      poxLastReport = millis();
    }
  }
}

void SendReadingsToFirebase(void* parameter) {
  int count = 1;
  String databasePath = "Data";

  for (;;) {

    // Check if the button is pressed (LOW indicates pressed for INPUT_PULLUP)
    if (digitalRead(BUTTON_PIN) == HIGH) {

      double objectTemp = temperatureSensor.readObjectTempC();
      double ambientTemp = temperatureSensor.readAmbientTempC();

      timeClient.update();
      String formattedTime = timeClient.getFormattedTime();

      if (Firebase.ready() && signupOK) {

        count++;

        // Create an object and push it to Firebase
        FirebaseJson json;  // Create a FirebaseJson object
        json.add("ID", count);
        json.add("timestamp", formattedTime);
        json.add("Temperature", String(objectTemp) + "°C");
        json.add("Heart_Rate", String(_heartRate) + "bpm");
        json.add("Oxygen_Saturation", String(_spo2) + "%");
        json.add("Blood_Pressure", random(0, 100));


        // Push the object to the path "test/objects"
        if (Firebase.RTDB.pushJSON(&FirebaseData, databasePath, &json)) {
          Serial.println("Object Pushed Successfully");
          Serial.println("PATH: " + FirebaseData.dataPath());
          Serial.println("TYPE: " + FirebaseData.dataType());
          Serial.println("Pushed Object: " + FirebaseData.jsonString());
        } else {
          Serial.println("FAILED");
          Serial.println("REASON: " + FirebaseData.errorReason());
        }
      }
    }
    delay(100);  // Small delay to debounce button presses
  }
}

void InitializeWifi() {
  // Wifi Initialize ...
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
}

void SignUpToFirebase() {
  /* Assign the api key (required) */
  config.api_key = API_KEY;

  /* Assign the RTDB URL (required) */
  config.database_url = DATABASE_URL;

  /* Sign up */
  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("ok");
    signupOK = true;
  } else {
    Serial.printf("%s\n", config.signer.signupError.message.c_str());
  }

  /* Assign the callback function for the long running token generation task */
  config.token_status_callback = tokenStatusCallback;  // See addons/TokenHelper.h

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

void InitializePOX() {
  Serial.print("Initializing pulse oximeter..");

  // Initialize sensor
  if (!pox.begin()) {
    Serial.println("FAILED");
    for (;;)
      ;
  } else {
    Serial.println("SUCCESS");
  }

  // Configure sensor to use 7.6mA for LED drive
  // pox.setIRLedCurrent(MAX30100_LED_CURR_7_6MA);
}

void initializeTemperatureSensor() {
  if (!temperatureSensor.begin()) {
    Serial.println("Error connecting to MLX90614 sensor. Check wiring!");
    while (1)
      ;
  }
}

void initializeOledDisplay() {
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ;
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
}

void loop() {
  delay(1);
}
