#define HELIX_FEATURE_AUDIO_CODEC_AAC_SBR
#define AAC_ENABLE_SBR
#define JSON_CONFIG_FILE "/configuration.json"

#include <WiFi.h>
#include <FS.h>
#include <LittleFS.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include "AudioTools.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"

// Constants and Pins
const int AMP_CONTROL_PIN = 4;  // Amplifier control pin (HIGH = ON)
const int WIFI_LED_PIN = 3;     // WiFi status LED
const int MUSIC_LED_PIN = 10;   // Music playing LED
const int VOLUME_CONTROL_PIN = 1;
const int BUTTON_PIN = 9;       // Button GPIO pin
const unsigned long HOLD_TIME = 3000; // Time to hold button for formatting (ms)

// Globals
bool shouldSaveConfig = false;
char streamURL[128] = "http://hyades.shoutca.st:8072/stream.mp3";
WiFiManager wm;
WiFiManagerParameter customTextBox("key_text", "Enter stream URL", streamURL, 128);

// Button Variables
unsigned long buttonPressTime = 0;
bool isFormatting = false;

// Audio Components
URLStream url;
I2SStream i2s;
VolumeStream volume(i2s);
EncodedAudioStream dec(&volume, new MP3DecoderHelix());
StreamCopy copier(dec, url);

// Function Prototypes
bool initializeFileSystem();
void saveConfigFile();
bool loadConfigFile();
void saveConfigCallback();
void configModeCallback(WiFiManager *myWiFiManager);
void initializePins();
void initializeAudio();
void startConfigurationPortal();

// Pre-save callback function
void preSaveConfigCallback() {
    Serial.println("Pre-save callback triggered");
    // Perform any operations before saving, such as checking or modifying values
    shouldSaveConfig = true;
    // Optionally modify any parameters before saving (e.g., update URL)
    // Serial.println("Stream URL: ");
    // Serial.println(streamURL); // You can print or modify values
}

void setup() {
    Serial.begin(115200);

    // Initialize File System
    if (!initializeFileSystem()) {
        Serial.println("Failed to initialize or format the file system. Restarting...");
        ESP.restart();
    }

    // Load Configuration
    bool forceConfig = !loadConfigFile();
    if (forceConfig) {
        Serial.println("Forcing configuration mode due to missing or invalid config.");
    }

    // Initialize button pin
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    // Check if button is pressed at boot
    if (digitalRead(BUTTON_PIN) == LOW) {
        forceConfig = true;
    }

    // Audio Logger
    //AudioLogger::instance().begin(Serial, AudioLogger::Info);

    // Setup WiFi Manager
    WiFi.mode(WIFI_STA);
    wm.setSaveConfigCallback(saveConfigCallback);
    wm.setSaveParamsCallback(saveConfigFile);
    wm.setAPCallback(configModeCallback);
    wm.addParameter(&customTextBox);

    if (forceConfig) {
        if (!wm.startConfigPortal()) {
            Serial.println("Failed to connect and hit timeout. Restarting...");
            delay(3000);
            ESP.restart();
        }
    } else {
        if (!wm.autoConnect()) {
            Serial.println("Failed to connect and hit timeout. Restarting...");
            delay(3000);
            ESP.restart();
        }
    }

    // WiFi Connected
    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    // Save Configuration if needed
    if (shouldSaveConfig) {
        saveConfigFile();
    }

    // Initialize Pins
    initializePins();

    // Initialize I2S and Volume
    initializeAudio();

    // Start Streaming
    if (!url.begin(streamURL, "audio/mp3")) {
        Serial.println("Failed to start streaming. Check the URL.");
    }
}

void initializePins() {
    pinMode(WIFI_LED_PIN, OUTPUT);
    pinMode(AMP_CONTROL_PIN, OUTPUT);
    pinMode(MUSIC_LED_PIN, OUTPUT);
    pinMode(VOLUME_CONTROL_PIN, INPUT);
    digitalWrite(AMP_CONTROL_PIN, HIGH); // Ensure amplifier is on initially

    while (WiFi.status() != WL_CONNECTED) {
        digitalWrite(WIFI_LED_PIN, LOW);
        delay(500);
    }
    // If we exited the loop, it means we have successfully connected to WiFi
    digitalWrite(WIFI_LED_PIN, HIGH);
}

void initializeAudio() {
    auto config = i2s.defaultConfig(TX_MODE);
    config.pin_ws = 6;
    config.pin_bck = 5;
    config.pin_data = 7;
    i2s.begin(config);
    dec.begin();

    auto volumeConfig = volume.defaultConfig();
    volumeConfig.copyFrom(config);
    volume.begin(volumeConfig);
    volume.setVolume(0.5);
}

void startConfigurationPortal() {
    WiFiManager wm;
    wm.setAPCallback(configModeCallback);

    if (!wm.startConfigPortal("MyConfigPortal")) {
        Serial.println("Failed to connect. Restarting...");
        delay(3000);
        ESP.restart();
    }

    Serial.println("Configuration portal completed.");
}


void loop() {
    // Update Volume
    static float lastVolume = -1.0;
    float potValue = (1.0 - (analogRead(VOLUME_CONTROL_PIN) / 4095.0)) * 0.9;
    if (abs(potValue - lastVolume) > 0.01) {
        volume.setVolume(potValue);
        lastVolume = potValue;
        Serial.printf("Volume set to: %.2f\n", potValue);
    }

    // Stream Audio
    bool isPlaying = copier.copy();
    digitalWrite(MUSIC_LED_PIN, isPlaying ? HIGH : LOW);
}

bool initializeFileSystem() {
    if (!LittleFS.begin(false)) {
        Serial.println("File system not initialized. Formatting...");
        if (!LittleFS.format() || !LittleFS.begin()) {
            Serial.println("Failed to format or mount file system.");
            return false;
        }
    }
    if (LittleFS.exists(JSON_CONFIG_FILE)) {
        Serial.println("Configuration file found.");
        return true;
    } else {
        Serial.println("Configuration file not found. Creating default configuration...");
        saveConfigFile();
        return true;
    }
}

void saveConfigFile() {
    Serial.println("Saving configuration...");
    
    // Copy user-provided stream URL
    strncpy(streamURL, customTextBox.getValue(), sizeof(streamURL));
    Serial.print("Stream URL: ");
    Serial.println(streamURL);
    
    StaticJsonDocument<512> json;
    json["streamURL"] = streamURL;
    File configFile = LittleFS.open(JSON_CONFIG_FILE, "w");
    if (!configFile) {
        Serial.println("Failed to open config file for writing.");
    } else {
        if (serializeJson(json, configFile) == 0) {
            Serial.println("Failed to write to config file.");
        }
        configFile.close();
    }
}

bool loadConfigFile() {
    if (LittleFS.begin(false) || LittleFS.begin(true)) {
        if (LittleFS.exists(JSON_CONFIG_FILE)) {
            File configFile = LittleFS.open(JSON_CONFIG_FILE, "r");
            if (configFile) {
                StaticJsonDocument<512> json;
                if (!deserializeJson(json, configFile)) {
                    strcpy(streamURL, json["streamURL"]);
                    return true;
                }
            }
        }
    }
    return false;
}

void saveConfigCallback() {
    Serial.println("Should save config.");
    shouldSaveConfig = true;
}

void configModeCallback(WiFiManager *myWiFiManager) {
    Serial.println("Entered Configuration Mode.");
    Serial.print("Config SSID: ");
    Serial.println(myWiFiManager->getConfigPortalSSID());
    Serial.print("Config IP Address: ");
    Serial.println(WiFi.softAPIP());
}
