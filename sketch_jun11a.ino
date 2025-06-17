#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>
#include <time.h>

// WiFi credentials
const char* ssid = "Livansh";
const char* password = "12345678";

// NTP Server settings
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 19800;  // UTC+5:30 for India
const int daylightOffset_sec = 0;

// Motor control pin
const int MOTOR_PIN = 13;  // Change this to your motor control pin
const int ROTATION_DEGREES = 90;  // Changed from 180 to 90 degrees
const int ROTATION_SPEED = 1000;   // Speed in microseconds (lower = faster)

// Create servo object
Servo feederMotor;

// Create web server object
WebServer server(80);

// Variables to store scheduled feed time
int scheduledHour = -1;
int scheduledMinute = -1;
int scheduledSecond = -1;
bool feedScheduled = false;

// Function to add CORS headers
void addCorsHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

// Function to initialize time
void initTime() {
  struct tm timeinfo;  // Declare timeinfo struct
  Serial.println("Initializing time...");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  
  // Wait for time to be set
  int retry = 0;
  while (!getLocalTime(&timeinfo) && retry < 10) {
    Serial.print(".");
    delay(500);
    retry++;
  }
  
  if (retry >= 10) {
    Serial.println("Failed to get time from NTP server!");
  } else {
    Serial.println("Time initialized successfully!");
    getLocalTime(&timeinfo);
    Serial.print("Current time: ");
    Serial.print(timeinfo.tm_hour);
    Serial.print(":");
    Serial.print(timeinfo.tm_min);
    Serial.print(":");
    Serial.println(timeinfo.tm_sec);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\nStarting Pet Feeder...");
  Serial.println("------------------------");
  Serial.println("Testing Serial communication...");
  Serial.println("If you can see this message, Serial is working!");
  Serial.println("------------------------");
  
  // Initialize servo motor
  Serial.println("Initializing servo motor...");
  feederMotor.attach(MOTOR_PIN);
  feederMotor.write(0);  // Start position
  Serial.println("Servo motor initialized");
  
  // Connect to WiFi
  Serial.println("\nConnecting to WiFi...");
  Serial.print("SSID: ");
  Serial.println(ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  unsigned long startAttemptTime = millis();
  
  while (WiFi.status() != WL_CONNECTED && 
         millis() - startAttemptTime < 20000) {
    delay(500);
    Serial.print(".");
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nFailed to connect to WiFi!");
    Serial.println("Please check:");
    Serial.println("1. WiFi credentials are correct");
    Serial.println("2. WiFi router is in range");
    Serial.println("3. WiFi router is powered on");
    Serial.println("\nRestarting in 5 seconds...");
    delay(5000);
    ESP.restart();
  }
  
  Serial.println("\nWiFi connected successfully!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Signal strength (RSSI): ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");

  // Initialize time
  initTime();
  
  // Setup server routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/feed", HTTP_GET, handleFeed);
  server.on("/setSchedule", HTTP_GET, handleSchedule);
  
  // Add OPTIONS handler for CORS preflight requests
  server.on("/", HTTP_OPTIONS, handleOptions);
  server.on("/feed", HTTP_OPTIONS, handleOptions);
  server.on("/setSchedule", HTTP_OPTIONS, handleOptions);
  
  server.begin();
  Serial.println("HTTP server started");
  Serial.println("------------------------");
}

void handleOptions() {
  addCorsHeaders();
  server.send(200, "text/plain", "");
}

void handleRoot() {
  addCorsHeaders();
  String html = "<html><body><h1>Pet Feeder Control</h1>";
  html += "<p>Current IP: " + WiFi.localIP().toString() + "</p>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleFeed() {
  addCorsHeaders();
  // Get parameters from request
  String grams = server.arg("grams");
  String pet = server.arg("pet");
  String animal = server.arg("animal");
  
  // Rotate motor with amount-based delay
  rotateMotor(grams.toInt());
  
  // Send response
  String response = "Feeding " + grams + "g to " + pet + " (" + animal + ")";
  server.send(200, "text/plain", response);
}

void handleSchedule() {
  addCorsHeaders();
  // Get time parameters
  String hhStr = server.arg("hh");
  String mmStr = server.arg("mm");
  String ssStr = server.arg("ss");
  String pet = server.arg("pet");
  String animal = server.arg("animal");
  
  // Debug print
  Serial.println("\nReceived schedule request:");
  Serial.print("Time: ");
  Serial.print(hhStr);
  Serial.print(":");
  Serial.print(mmStr);
  Serial.print(":");
  Serial.println(ssStr);
  Serial.print("Pet: ");
  Serial.println(pet);
  Serial.print("Animal: ");
  Serial.println(animal);
  
  // Convert to integers
  scheduledHour = hhStr.toInt();
  scheduledMinute = mmStr.toInt();
  scheduledSecond = ssStr.toInt();
  
  // Validate time
  if (scheduledHour >= 0 && scheduledHour <= 23 &&
      scheduledMinute >= 0 && scheduledMinute <= 59 &&
      scheduledSecond >= 0 && scheduledSecond <= 59) {
    
    feedScheduled = true;
    String response = "Scheduled feed for " + pet + " (" + animal + ") at " +
                     String(scheduledHour) + ":" + String(scheduledMinute) + ":" + String(scheduledSecond);
    Serial.println("Schedule successful: " + response);
    server.send(200, "text/plain", response);
  } else {
    String errorMsg = "Invalid time parameters: " + hhStr + ":" + mmStr + ":" + ssStr;
    Serial.println("Schedule failed: " + errorMsg);
    server.send(400, "text/plain", errorMsg);
  }
}

void loop() {
  // Check WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connection lost! Reconnecting...");
    WiFi.reconnect();
    delay(5000);
  }
  
  server.handleClient();
  
  // Check if it's time to feed
  if (feedScheduled) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      // Print current time and scheduled time for debugging
      Serial.println("\nTime Check:");
      Serial.print("Current time: ");
      Serial.print(timeinfo.tm_hour);
      Serial.print(":");
      Serial.print(timeinfo.tm_min);
      Serial.print(":");
      Serial.println(timeinfo.tm_sec);
      
      Serial.print("Scheduled time: ");
      Serial.print(scheduledHour);
      Serial.print(":");
      Serial.print(scheduledMinute);
      Serial.print(":");
      Serial.println(scheduledSecond);
      
      // Check each component
      bool hourMatch = timeinfo.tm_hour == scheduledHour;
      bool minuteMatch = timeinfo.tm_min == scheduledMinute;
      bool secondMatch = timeinfo.tm_sec == scheduledSecond;
      
      Serial.print("Hour match: ");
      Serial.println(hourMatch ? "Yes" : "No");
      Serial.print("Minute match: ");
      Serial.println(minuteMatch ? "Yes" : "No");
      Serial.print("Second match: ");
      Serial.println(secondMatch ? "Yes" : "No");
      
      if (hourMatch && minuteMatch && secondMatch) {
        Serial.println("Time to feed! Rotating motor...");
        rotateMotor(10);  // Default amount for scheduled feeds
        feedScheduled = false;  // Reset after feeding
        Serial.println("Feed completed");
      }
    } else {
      Serial.println("Failed to get local time! Retrying time sync...");
      initTime();  // Try to reinitialize time
    }
  }
}

void rotateMotor(int amount) {
  // Calculate delay based on amount
  // 10g = 0.1s, 20g = 0.2s, 30g = 0.3s, etc. up to 500g = 5s
  int delayTime = (amount * 10); // 10ms per 10g
  if (delayTime > 5000) delayTime = 5000; // Maximum 5 seconds for 500g
  
  Serial.printf("Rotating motor for %.1f seconds (amount: %dg)\n", delayTime/1000.0, amount);
  
  // Rotate from 90 to 0 degrees
  for (int pos = 90; pos >= 0; pos -= 1) {
    feederMotor.write(pos);
    delay(15);
  }
  
  // Wait for the calculated time
  delay(delayTime);
  
  // Return to 90 degrees
  for (int pos = 0; pos <= 90; pos += 1) {
    feederMotor.write(pos);
    delay(15);
  }
  
  Serial.println("Motor rotation complete");
} 