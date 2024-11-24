#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>

// WiFi credentials
const char* ssid = "";
const char* password = "";
const char* serverAddress = "PUT MAC ADDRESS HERE";

// Pin definitions
const int PLAY_PAUSE_BTN = 15;
const int PREV_BTN = 4;
const int NEXT_BTN = 14;
const int MENU_BTN = 5;
const int VOL_POT = 35;
const int SEEK_POT = 34;

// Define ButtonEvent enum before it's used
enum ButtonEvent {
    NONE,
    SINGLE_CLICK,
    DOUBLE_CLICK,
    HOLD,
    HOLD_RELEASE
};

// LCD setup
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Enhanced button state handling
struct Button {
    int pin;
    bool state;
    bool lastReading;
    unsigned long lastDebounceTime;
    unsigned long lastPressTime;
    bool waitingForDoubleClick;
    unsigned long holdStartTime;
    bool isHeld;
} playPauseBtn, prevBtn, nextBtn, menuBtn;

// Enhanced potentiometer smoothing using exponential moving average
struct Potentiometer {
    int pin;
    float smoothedValue;
    float alpha;
    int lastReportedValue;
} volumePot, seekPot;

// Improved menu and playback state
struct PlaybackState {
    String currentTrack;
    String artist;
    String album;
    int duration;
    int position;
    bool isPlaying;
    int volume;
    unsigned long lastUpdateTime;
} playback;

// Menu system
enum MenuState {
    NOW_PLAYING,
    VOLUME,
    SEEK,
    TRACK_INFO
};

MenuState currentMenu = NOW_PLAYING;

// Timing controls
const unsigned long STATUS_CHECK_INTERVAL = 1000;
const unsigned long DEBOUNCE_DELAY = 30;
const unsigned long DOUBLE_CLICK_TIME = 300;
const unsigned long HOLD_THRESHOLD = 800;
const unsigned long SCROLL_INTERVAL = 400;

// Display handling
struct ScrollingText {
    String text;
    int position;
    unsigned long lastScroll;
    bool needsScroll;
} trackScroll, artistScroll;

// Function declarations
void initializePotentiometer(Potentiometer* pot, int pin, float smooth_factor);
void initializeButton(Button* btn, int pin);
void connectToWiFi();
ButtonEvent handleButton(Button* btn);
void handlePotentiometers();
void updateDisplay();
void updateNowPlayingDisplay();
void handleNavigationButtons();
void updateSpotifyStatus();
void updateScrollState();
String formatTime(int milliseconds);
void sendSpotifyCommand(String command);
void updateVolumeDisplay();
void updateSeekDisplay();
void updateTrackInfoDisplay();

// Function to send commands to Spotify server
void sendSpotifyCommand(String command) {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        String url = "http://" + String(serverAddress) + "/spotify/" + command;
        http.begin(url);
        http.GET();
        http.end();
    }
}

// Display update functions
void updateVolumeDisplay() {
    lcd.setCursor(0, 0);
    lcd.print("Volume");
    lcd.setCursor(0, 1);
    lcd.print(map(volumePot.lastReportedValue, 0, 100, 0, 16), '>');
}

void updateSeekDisplay() {
    lcd.setCursor(0, 0);
    lcd.print("Seek");
    lcd.setCursor(0, 1);
    int progress = map(playback.position, 0, playback.duration, 0, 16);
    lcd.print(progress, '=');
}

void updateTrackInfoDisplay() {
    lcd.setCursor(0, 0);
    lcd.print(playback.artist.substring(0, 16));
    lcd.setCursor(0, 1);
    lcd.print(playback.album.substring(0, 16));
}


void setup() {
    Serial.begin(115200);
    
    // Initialize I2C and LCD with error checking
    if (!Wire.begin(21, 22)) {
        Serial.println("I2C initialization failed!");
        while (1) delay(100);  // Halt if I2C fails
    }
    
    lcd.init();
    lcd.backlight();
    lcd.clear();
    
    // Initialize buttons with enhanced features
    initializeButton(&playPauseBtn, PLAY_PAUSE_BTN);
    initializeButton(&prevBtn, PREV_BTN);
    initializeButton(&nextBtn, NEXT_BTN);
    initializeButton(&menuBtn, MENU_BTN);
    
    // Initialize potentiometers
    initializePotentiometer(&volumePot, VOL_POT, 0.15);  // Less smoothing for volume
    initializePotentiometer(&seekPot, SEEK_POT, 0.1);    // More smoothing for seeking
    
    // Configure ADC for better resolution
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);  // Increases input range
    
    // Initialize WiFi with improved connection handling
    connectToWiFi();
}

void initializePotentiometer(Potentiometer* pot, int pin, float smooth_factor) {
    pot->pin = pin;
    pot->smoothedValue = analogRead(pin);
    pot->alpha = smooth_factor;
    pot->lastReportedValue = -1;
}

void initializeButton(Button* btn, int pin) {
    btn->pin = pin;
    btn->state = HIGH;
    btn->lastReading = HIGH;
    btn->lastDebounceTime = 0;
    btn->lastPressTime = 0;
    btn->waitingForDoubleClick = false;
    btn->holdStartTime = 0;
    btn->isHeld = false;
    pinMode(pin, INPUT_PULLUP);
}

void connectToWiFi() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Connecting WiFi");
    
    WiFi.mode(WIFI_STA);  // Explicitly set station mode
    WiFi.begin(ssid, password);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        lcd.setCursor(attempts % 16, 1);
        lcd.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        lcd.clear();
        lcd.print("Connected: ");
        lcd.setCursor(0, 1);
        lcd.print(WiFi.localIP().toString());
        delay(1000);
    } else {
        lcd.clear();
        lcd.print("WiFi Failed!");
        lcd.setCursor(0, 1);
        lcd.print("Retrying...");
        delay(2000);
        ESP.restart();
    }
}

ButtonEvent handleButton(Button* btn) {
    ButtonEvent event = NONE;
    int reading = digitalRead(btn->pin);
    unsigned long now = millis();
    
    if (reading != btn->lastReading) {
        btn->lastDebounceTime = now;
    }
    
    if ((now - btn->lastDebounceTime) > DEBOUNCE_DELAY) {
        if (reading != btn->state) {
            btn->state = reading;
            
            if (btn->state == LOW) {  // Button pressed
                btn->holdStartTime = now;
                
                if (btn->waitingForDoubleClick) {
                    event = DOUBLE_CLICK;
                    btn->waitingForDoubleClick = false;
                } else {
                    btn->waitingForDoubleClick = true;
                    btn->lastPressTime = now;
                }
            } else {  // Button released
                if (btn->isHeld) {
                    event = HOLD_RELEASE;
                    btn->isHeld = false;
                }
            }
        } else if (btn->state == LOW && !btn->isHeld && 
                   (now - btn->holdStartTime) > HOLD_THRESHOLD) {
            event = HOLD;
            btn->isHeld = true;
        }
    }
    
    // Check for single click timeout
    if (btn->waitingForDoubleClick && 
        (now - btn->lastPressTime) > DOUBLE_CLICK_TIME) {
        event = SINGLE_CLICK;
        btn->waitingForDoubleClick = false;
    }
    
    btn->lastReading = reading;
    return event;
}

void handlePotentiometers() {
    // Volume control with exponential smoothing
    float rawVolume = analogRead(volumePot.pin);
    volumePot.smoothedValue = volumePot.alpha * rawVolume + 
                             (1 - volumePot.alpha) * volumePot.smoothedValue;
    
    int currentVolume = map(round(volumePot.smoothedValue), 0, 4095, 0, 100);
    
    if (abs(currentVolume - volumePot.lastReportedValue) >= 2) {
        volumePot.lastReportedValue = currentVolume;
        sendSpotifyCommand("volume/" + String(currentVolume));
        playback.volume = currentVolume;
    }
    
    // Seek control (only when in seek menu)
    if (currentMenu == SEEK) {
        float rawSeek = analogRead(seekPot.pin);
        seekPot.smoothedValue = seekPot.alpha * rawSeek + 
                               (1 - seekPot.alpha) * seekPot.smoothedValue;
        
        int currentSeek = map(round(seekPot.smoothedValue), 0, 4095, 0, 100);
        
        if (abs(currentSeek - seekPot.lastReportedValue) >= 2) {
            seekPot.lastReportedValue = currentSeek;
            int seekMs = (currentSeek * playback.duration) / 100;
            sendSpotifyCommand("seek/" + String(seekMs));
            playback.position = seekMs;
        }
    }
}

void updateDisplay() {
    static unsigned long lastScrollTime = 0;
    unsigned long now = millis();
    
    if (now - lastScrollTime >= SCROLL_INTERVAL) {
        lastScrollTime = now;
        lcd.clear();
        
        switch (currentMenu) {
            case NOW_PLAYING:
                updateNowPlayingDisplay();
                break;
            case VOLUME:
                updateVolumeDisplay();
                break;
            case SEEK:
                updateSeekDisplay();
                break;
            case TRACK_INFO:
                updateTrackInfoDisplay();
                break;
        }
    }
}

void updateNowPlayingDisplay() {
    // Scroll long track names
    if (trackScroll.needsScroll) {
        String displayText = trackScroll.text + "    " + trackScroll.text;
        int startPos = trackScroll.position % (trackScroll.text.length() + 4);
        lcd.setCursor(0, 0);
        lcd.print(displayText.substring(startPos, startPos + 16));
        trackScroll.position++;
    } else {
        lcd.setCursor(0, 0);
        lcd.print(playback.currentTrack.substring(0, 16));
    }
    
    // Show playback status and time
    lcd.setCursor(0, 1);
    String timeStr = formatTime(playback.position) + "/" + formatTime(playback.duration);
    lcd.print(playback.isPlaying ? ">" : "||");
    lcd.print(" " + timeStr);
}

void loop() {
    static unsigned long lastWifiCheck = 0;
    unsigned long now = millis();
    
    // Regular WiFi connection check
    if (now - lastWifiCheck > 5000) {
        if (WiFi.status() != WL_CONNECTED) {
            connectToWiFi();
        }
        lastWifiCheck = now;
    }

    // Process button events
    ButtonEvent ppEvent = handleButton(&playPauseBtn);
    if (ppEvent != NONE) {
        switch (ppEvent) {
            case SINGLE_CLICK:
                sendSpotifyCommand(playback.isPlaying ? "pause" : "play");
                break;
            case DOUBLE_CLICK:
                currentMenu = (currentMenu == TRACK_INFO) ? NOW_PLAYING : TRACK_INFO;
                break;
            case HOLD:
                // Could implement additional features for hold
                break;
        }
    }

    // Handle other buttons
    handleNavigationButtons();
    
    // Update potentiometers with improved smoothing
    handlePotentiometers();
    
    // Regular status updates
    if (now - playback.lastUpdateTime > STATUS_CHECK_INTERVAL) {
        updateSpotifyStatus();
        playback.lastUpdateTime = now;
    }
    
    // Update display with scrolling and animations
    updateDisplay();
    
    // Small delay for stability
    delay(5);
}

void handleNavigationButtons() {
    ButtonEvent prevEvent = handleButton(&prevBtn);
    ButtonEvent nextEvent = handleButton(&nextBtn);
    ButtonEvent menuEvent = handleButton(&menuBtn);
    
    if (prevEvent == SINGLE_CLICK) {
        sendSpotifyCommand("previous");
    } else if (prevEvent == HOLD) {
        // Rewind functionality
        sendSpotifyCommand("seek/-10000");
    }
    
    if (nextEvent == SINGLE_CLICK) {
        sendSpotifyCommand("next");
    } else if (nextEvent == HOLD) {
        // Fast forward functionality
        sendSpotifyCommand("seek/+10000");
    }
    
    if (menuEvent == SINGLE_CLICK) {
        currentMenu = static_cast<MenuState>((static_cast<int>(currentMenu) + 1) % 4);
    }
}

void updateSpotifyStatus() {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        String url = "http://" + String(serverAddress) + "/spotify/status";
        
        http.begin(url);
        int httpResponseCode = http.GET();
        
        if (httpResponseCode == 200) {
            String response = http.getString();
            
            // Use ArduinoJson to parse the response
            StaticJsonDocument<1024> doc;
            DeserializationError error = deserializeJson(doc, response);
            
            if (!error) {
                playback.isPlaying = doc["is_playing"].as<bool>();
                playback.currentTrack = doc["track"].as<String>();
                playback.artist = doc["artist"].as<String>();
                playback.duration = doc["duration"].as<int>();
                playback.position = doc["position"].as<int>();
                
                // Update scroll state for long text
                updateScrollState();
            }
        }
        http.end();
    }
}

void updateScrollState() {
    // Check if track name needs scrolling
    trackScroll.text = playback.currentTrack;
    trackScroll.needsScroll = playback.currentTrack.length() > 16;
    if (!trackScroll.needsScroll) {
        trackScroll.position = 0;
    }
    
    // Check if artist name needs scrolling
    artistScroll.text = playback.artist;
    artistScroll.needsScroll = playback.artist.length() > 16;
    if (!artistScroll.needsScroll) {
        artistScroll.position = 0;
    }
}

String formatTime(int milliseconds) {
    int seconds = milliseconds / 1000;
    int minutes = seconds / 60;
    seconds %= 60;
    char buffer[6];
    sprintf(buffer, "%d:%02d", minutes, seconds);
    return String(buffer);
}
