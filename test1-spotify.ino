#include <WiFi.h>
#include <HTTPClient.h>
#include <LiquidCrystal_PCF8574.h>

const char* ssid = "";
const char* password = "";

const int buttonPin = 15;
bool buttonState = HIGH;

// LCD setup
LiquidCrystal_PCF8574 lcd(0x27); // Adjust if needed

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");

  lcd.begin(16, 2);
  lcd.setBacklight(1);
  lcd.setCursor(0, 0);
  lcd.print("WiFi Connected");

  pinMode(buttonPin, INPUT_PULLUP);
}

void loop() {
  if (digitalRead(buttonPin) == LOW && buttonState == HIGH) {  
    buttonState = LOW;
    sendSpotifyCommand("play");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Playing Music");
    delay(2000);  // Debounce
  } 
  else if (digitalRead(buttonPin) == HIGH && buttonState == LOW) {
    buttonState = HIGH;
    sendSpotifyCommand("pause");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Music Paused");
    delay(2000);  // Debounce
  }
}

void sendSpotifyCommand(String command) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = "http://PUT MAC ADDRESS HERE/spotify/" + command;
    
    Serial.print("Sending command to: ");
    Serial.println(url);

    http.begin(url);
    int httpResponseCode = http.GET();

    if (httpResponseCode > 0) {
      Serial.print("Command sent successfully, response code: ");
      Serial.println(httpResponseCode);
      lcd.setCursor(0, 1);
      lcd.print("Command sent");
    } else {
      Serial.print("Error in sending command, code: ");
      Serial.println(httpResponseCode);
      lcd.setCursor(0, 1);
      lcd.print("Send Error");
    }
    http.end();
  } else {
    Serial.println("WiFi not connected!");
    lcd.setCursor(0, 1);
    lcd.print("No WiFi!");
  }
}
