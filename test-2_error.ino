#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// WiFi credentials
const char* ssid = "";
const char* password = "";
const char* serverAddress = "PUT MAC ADDRESS HERE";

// Pin definitions
const int PLAY_PAUSE_BTN = 15;
const int PREV_BTN = 4;
const int NEXT_BTN = 14;  // Changed to GPIO14
const int MENU_BTN = 5;
const int VOL_POT = 35;
const int SEEK_POT = 34;

// LCD setup
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Button state handling with debounce
struct Button {
    int pin;
    bool state;
    bool lastReading;
    unsigned long lastDebounceTime;
} playPauseBtn, prevBtn, nextBtn, menuBtn;

// Potentiometer smoothing
const int NUM_READINGS = 5;
int volumeReadings[NUM_READINGS] = {0};
int seekReadings[NUM_READINGS] = {0};
int readIndex = 0;
int lastVolume = -1;
int lastSeekPosition = -1;

// Menu and playback state
int menuState = 0;  // 0: Now Playing, 1: Volume, 2: Seek
String currentTrack = "";
String currentArtist = "";
bool isPlaying = false;

// Timing controls
unsigned long lastStatusCheck = 0;
const unsigned long STATUS_CHECK_INTERVAL = 3000;
const unsigned long DEBOUNCE_DELAY = 50;

void setup() {
    Serial.begin(115200);
    
    // Initialize I2C and LCD
    Wire.begin(21, 22);
    lcd.init();
    lcd.backlight();
    lcd.clear();
    
    // Initialize buttons
    initializeButton(&playPauseBtn, PLAY_PAUSE_BTN);
    initializeButton(&prevBtn, PREV_BTN);
    initializeButton(&nextBtn, NEXT_BTN);
    initializeButton(&menuBtn, MENU_BTN);
    
    // Configure ADC
    analogReadResolution(12);
    
    // Connect to WiFi
    connectToWiFi();
}

void initializeButton(Button* btn, int pin) {
    btn->pin = pin;
    btn->state = HIGH;
    btn->lastReading = HIGH;
    btn->lastDebounceTime = 0;
    pinMode(pin, INPUT_PULLUP);
}

void connectToWiFi() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Connecting WiFi");
    
    WiFi.begin(ssid, password);
    int dots = 0;
    
    while (WiFi.status() != WL_CONNECTED && dots < 20) {  // Added timeout
        delay(500);
        lcd.setCursor(dots % 16, 1);
        lcd.print(".");
        dots++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        lcd.clear();
        lcd.print("WiFi Connected!");
        delay(1000);
    } else {
        lcd.clear();
        lcd.print("WiFi Failed!");
        delay(2000);
        ESP.restart();  // Restart if WiFi fails
    }
}

bool handleButton(Button* btn, const char* command) {
    bool triggered = false;
    int reading = digitalRead(btn->pin);
    
    if (reading != btn->lastReading) {
        btn->lastDebounceTime = millis();
    }
    
    if ((millis() - btn->lastDebounceTime) > DEBOUNCE_DELAY) {
        if (reading != btn->state) {
            btn->state = reading;
            if (btn->state == LOW) {  // Button pressed
                sendSpotifyCommand(command);
                triggered = true;
            }
        }
    }
    
    btn->lastReading = reading;
    return triggered;
}

int smoothAnalogRead(int pin, int* readings) {
    readings[readIndex] = analogRead(pin);
    int total = 0;
    for (int i = 0; i < NUM_READINGS; i++) {
        total += readings[i];
    }
    readIndex = (readIndex + 1) % NUM_READINGS;
    return total / NUM_READINGS;
}

void handlePotentiometers() {
    // Smooth volume reading
    int currentVolume = map(smoothAnalogRead(VOL_POT, volumeReadings), 0, 4095, 0, 100);
    if (abs(currentVolume - lastVolume) > 2) {
        lastVolume = currentVolume;
        String volumeCommand = "volume/" + String(currentVolume);
        sendSpotifyCommand(volumeCommand);
    }
    
    // Smooth seek reading
    if (menuState == 2) {  // Only process seek when in seek menu
        int currentSeek = map(smoothAnalogRead(SEEK_POT, seekReadings), 0, 4095, 0, 100);
        if (abs(currentSeek - lastSeekPosition) > 2) {
            lastSeekPosition = currentSeek;
            String seekCommand = "seek/" + String(currentSeek * 300000);
            sendSpotifyCommand(seekCommand);
        }
    }
}

void updateDisplay() {
    static unsigned long lastDisplayUpdate = 0;
    const unsigned long DISPLAY_UPDATE_INTERVAL = 200;  // Limit display updates
    
    if (millis() - lastDisplayUpdate < DISPLAY_UPDATE_INTERVAL) {
        return;
    }
    
    lastDisplayUpdate = millis();
    lcd.clear();
    
    switch (menuState) {
        case 0:  // Now Playing
            lcd.setCursor(0, 0);
            lcd.print(currentTrack.length() > 0 ? currentTrack.substring(0, 16) : "No Track");
            lcd.setCursor(0, 1);
            lcd.print(isPlaying ? "Playing" : "Paused");
            break;
            
        case 1:  // Volume
            lcd.setCursor(0, 0);
            lcd.print("Volume: " + String(lastVolume) + "%");
            lcd.setCursor(0, 1);
            for (int i = 0; i < 16; i++) {
                lcd.print(i < (lastVolume * 16 / 100) ? "#" : "-");
            }
            break;
            
        case 2:  // Seek
            lcd.setCursor(0, 0);
            lcd.print("Seek");
            lcd.setCursor(0, 1);
            for (int i = 0; i < 16; i++) {
                lcd.print(i < (lastSeekPosition * 16 / 100) ? "#" : "-");
            }
            break;
    }
}

void loop() {
    if (WiFi.status() != WL_CONNECTED) {
        connectToWiFi();
        return;
    }

    // Update Spotify status periodically
    if (millis() - lastStatusCheck > STATUS_CHECK_INTERVAL) {
        updateSpotifyStatus();
        lastStatusCheck = millis();
    }

    // Handle buttons with improved debouncing
    if (handleButton(&playPauseBtn, isPlaying ? "pause" : "play")) {
        isPlaying = !isPlaying;
    }
    handleButton(&prevBtn, "previous");
    handleButton(&nextBtn, "next");
    
    // Handle menu button
    if (handleButton(&menuBtn, "")) {
        menuState = (menuState + 1) % 3;
    }
    
    handlePotentiometers();
    updateDisplay();
    
    delay(10);  // Small delay for stability
}

void updateSpotifyStatus() {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        String url = "http://" + String(serverAddress) + "/spotify/status";
        
        http.begin(url);
        int httpResponseCode = http.GET();
        
        if (httpResponseCode == 200) {
            String response = http.getString();
            if (response.indexOf("playing") != -1) {
                isPlaying = (response.indexOf("true") != -1);
                int trackStart = response.indexOf("track") + 8;
                int trackEnd = response.indexOf("\"", trackStart);
                currentTrack = response.substring(trackStart, trackEnd);
            }
        }
        http.end();
    }
}

void sendSpotifyCommand(String command) {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        String url = "http://" + String(serverAddress) + "/spotify/" + command;
        
        Serial.print("Sending command: ");
        Serial.println(url);
        
        http.begin(url);
        int httpResponseCode = http.GET();
        
        if (httpResponseCode > 0) {
            Serial.print("Success, code: ");
            Serial.println(httpResponseCode);
        } else {
            Serial.print("Error, code: ");
            Serial.println(httpResponseCode);
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Error!");
            delay(1000);
        }
        
        http.end();
    }
}
