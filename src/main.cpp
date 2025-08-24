#include <Arduino.h>
#include <FastLED.h>
#include <WiFiManager.h>
#include <Wire.h>
#ifdef BH1750_ENABLED
#include <BH1750.h>
#endif
#include "Ticker.h"

// BH1750 light sensor support
// To enable: add -D BH1750_ENABLED=1 to build_flags in platformio.ini
// or change the 0 to 1 in the line below
#ifndef BH1750_ENABLED
#define BH1750_ENABLED 0
#endif

// Timezone config
/* 
  Enter your time zone (https://remotemonitoringsystems.ca/time-zone-abbreviations.php)
  See https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv for Timezone codes for your region
  based on https://github.com/SensorsIot/NTP-time-for-ESP8266-and-ESP32/blob/master/NTP_Example/NTP_Example.ino
*/
const char* NTP_SERVER = "ch.pool.ntp.org";
const char* TZ_INFO    = "CET-1CEST-2,M3.5.0/02:00:00,M10.5.0/03:00:00";  // Switzerland

Ticker timer;
#define TIMER_PERIOD 5

// Wifi
WiFiManager wm;   // looking for credentials? don't need em! ... google "ESP32 WiFiManager"
bool wifi_connected = false;

#define NUM_DIGITS 4
#define LEDS_PER_DIGIT 3*7
#define NUM_LEDS NUM_DIGITS*LEDS_PER_DIGIT  // 6x4 LEDs per character, 2 characters per display
#define MAX_BRIGHTNESS 200
#define MIN_BRIGHTNESS 80

#ifndef DOUT_PIN  // see platformio.ini for pin assignments by board type
#define DOUT_PIN 5
#endif

CRGB leds[NUM_LEDS];
int global_brightness = (MAX_BRIGHTNESS-MIN_BRIGHTNESS)/2;
int max_brightness = MAX_BRIGHTNESS;
int min_brightness = MIN_BRIGHTNESS;
int brightness;
float lux = 150;
float lux_adjustment = 1.0;
bool night_mode = false;
int count = 0;
int fade = 0;

#if BH1750_ENABLED
BH1750 lightMeter;
#endif

static const uint8_t digits[] = {
  0b01111101, // 0
  0b00001001, // 1
  0b01111010, // 2
  0b00111011, // 3
  0b00001111, // 4
  0b00110111, // 5
  0b01110111, // 6
  0b00011001, // 7
  0b01111111, // 8
  0b00111111, // 9
};

// Additional characters for display
static const uint8_t letters[] = {
  0b01111111, // A (index 0)
  0b01100111, // P (index 1)
  0b00000000, // SPACE (index 2)
};

// Time 
tm timeinfo;
time_t now;
int hour = 0;
int minute = 0;
int second = 0;

// Time, date, and tracking state
int last_minute=0;

// Button handling for WiFiManager
#define BOOT_BUTTON_PIN 0
#define BUTTON_HOLD_TIME 5000  // 5 seconds in milliseconds
bool config_mode_active = false;
unsigned long button_press_start = 0;
bool button_was_pressed = false;



String getFormattedDate(){
  char time_output[30];
  strftime(time_output, 30, "%a  %d-%m-%y", &timeinfo);
  return String(time_output);
}

String getFormattedTime(){
  char time_output[30];
  strftime(time_output, 30, "%H:%M:%S", &timeinfo);
  return String(time_output);
}

bool getNTPtime(int sec) {
  if (WiFi.isConnected()) {
    bool timeout = false;
    bool date_is_valid = false;
    long start = millis();

    Serial.println(" updating:");
    configTime(0, 0, NTP_SERVER);
    setenv("TZ", TZ_INFO, 1);

    do {
      timeout = (millis() - start) > (1000 * sec);
      time(&now);
      localtime_r(&now, &timeinfo);
      Serial.print(" . ");
      date_is_valid = timeinfo.tm_year > (2016 - 1900);
      delay(100);
      leds[random(4)*LEDS_PER_DIGIT+5] = CRGB(random(255),random(255),random(255));
      FastLED.show();
      
      // show animation
      static int dp_pos = 0;
      dp_pos %= 12;

    } while (!timeout && !date_is_valid);
    
    FastLED.clear();
    FastLED.show();

    if (!date_is_valid){
      Serial.println("Error: Invalid date received!");
      Serial.println(timeinfo.tm_year);
      return false;  // the NTP call was not successful
    } else if (timeout) {
      Serial.println("Error: Timeout while trying to update the current time with NTP");
      return false;
    } else {
      Serial.println("\n[ok] time updated: ");
      Serial.print("System time is now:");
      Serial.println(getFormattedTime());
      Serial.println(getFormattedDate());
      return true;
    }
  } else {
    Serial.println("Error: Update time failed, no WiFi connection!");
    return false;
  }
}

void show_letter(uint8_t digit, uint8_t letter_index){
  int offset = digit * LEDS_PER_DIGIT;
  uint16_t color = 120; // Blue color for AP mode
  
  for (int i=0; i<7; i++){
    for (int pos=0; pos<3; pos++){
      int led_num = offset + i*3 + pos;
      brightness = global_brightness;
      
      if (letters[letter_index] & (1 << i)){
        if (pos == 1){
          leds[led_num] = CHSV(color, 240, brightness);
        } else {
          leds[led_num] = CHSV(color, 240, brightness * 0.6);
        }
      } else {
        leds[led_num] = CRGB::Black;
      }
    }
  }
}

void display_ap_mode(){
  // Clear all LEDs first
  FastLED.clear();
  
  // Display "AP" - A on digit 3, P on digit 2, spaces on digits 1 and 0
  show_letter(3, 0); // A
  show_letter(2, 1); // P
  show_letter(1, 2); // SPACE
  show_letter(0, 2); // SPACE
}

void check_button_for_config_mode() {
  bool button_pressed = (digitalRead(BOOT_BUTTON_PIN) == LOW);
  
  if (button_pressed && !button_was_pressed) {
    // Button just pressed
    button_press_start = millis();
    button_was_pressed = true;
    Serial.println("Boot button pressed");
  } else if (!button_pressed && button_was_pressed) {
    // Button just released
    button_was_pressed = false;
    Serial.println("Boot button released");
  } else if (button_pressed && button_was_pressed) {
    // Button is being held
    unsigned long hold_time = millis() - button_press_start;
    if (hold_time >= BUTTON_HOLD_TIME && !config_mode_active) {
      // Button held for 5 seconds - enter config mode
      Serial.println("Boot button held for 5 seconds - entering WiFi config mode");
      config_mode_active = true;
      
      // Reset WiFiManager settings and start config portal
      wm.resetSettings();
      wm.setConfigPortalTimeout(0); // No timeout for manual config mode
      
      // Start config portal
      if (!wm.startConfigPortal("Mini Clock RGB Config")) {
        Serial.println("Failed to start config portal");
        config_mode_active = false;
      }
    }
  }
}

void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  Serial.println(wm.getConfigPortalSSID());
  config_mode_active = true;
}

void saveWifiCallback() {
  Serial.println("WiFi credentials saved, exiting config mode");
  config_mode_active = false;
  wifi_connected = true;
}

void ConnectToWifi(){
  Serial.print("Connecting to WiFi");
  WiFi.mode(WIFI_STA);
  wm.setAPCallback(configModeCallback);
  wm.setSaveConfigCallback(saveWifiCallback);

  // Set timeout for WiFiManager to prevent indefinite blocking (only for auto-connect)
  if (!config_mode_active) {
    wm.setConfigPortalTimeout(30); // 30 seconds timeout for auto-connect
  } else {
    wm.setConfigPortalTimeout(0); // No timeout for manual config mode
  }
  
  //wm.resetSettings();   // uncomment to force a reset
  wifi_connected = wm.autoConnect("Mini Clock RGB");
  int t=0;
  if (wifi_connected){
    Serial.println();
    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("MAC address: ");
    Serial.println(WiFi.macAddress());
    Serial.print("RSSI: ");
    Serial.print(WiFi.RSSI());
    Serial.println("db");

    delay(1000);

    Serial.println("getting current time...");
    
    if (getNTPtime(10)) {  // wait up to 10sec to sync
      Serial.println("Time sync complete");
    } else {
      Serial.println("Error: NTP time update failed!");
    }
    
    // If we were in config mode and successfully connected, exit config mode
    if (config_mode_active) {
      config_mode_active = false;
      Serial.println("Exiting config mode - WiFi connected successfully");
    }
  } else {
    Serial.println("ERROR: WiFi connect failure");
    Serial.println("Entering random number mode...");
    // update fastled display with error message
  }
}

void timerStatusMessage(){
  Serial.printf("FPS: %d\n", FastLED.getFPS());
  Serial.printf("brightness: %d  global_brightness: %d\n", brightness, global_brightness);
  Serial.printf("lux_adjustment: %f\n", lux_adjustment);
  Serial.printf("night_mode: %s\n", night_mode ? "true" : "false");
  Serial.printf("fade: %d\n", fade);
  int avg_brightness = 0;
  int lit_count = 0;
  for (int i=0; i<NUM_LEDS; i++){
    if (leds[i].getAverageLight() > 0){
      lit_count++;
      avg_brightness += leds[i].getAverageLight();
    }
  }
  avg_brightness = lit_count > 0 ? avg_brightness / lit_count : 0;
  Serial.printf("avg_brightness: %d\n", avg_brightness);

#if BH1750_ENABLED
  float sensor_lux = lightMeter.readLightLevel();
  Serial.print("Light: ");
  Serial.print(sensor_lux);
  Serial.println(" lx");
#endif
}

void setup() {
  // set up fastled
  Serial.begin(115200);
  FastLED.addLeds<WS2812B, DOUT_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(180);

  pinMode(0, INPUT_PULLUP);

  delay(300);
  Serial.println("Start");
  ConnectToWifi();
  delay(200);

#if BH1750_ENABLED
  // Initialize the I2C bus (BH1750 library doesn't do this automatically)
  // On esp8266 devices you can select SCL and SDA pins using Wire.begin(D4, D3);
  Wire.begin();
  lightMeter.begin();
  Serial.println("BH1750 light sensor initialized");
#endif

  timer.attach(TIMER_PERIOD, timerStatusMessage);
}


void show_number(uint8_t digit, uint8_t num){
  int offset = digit * LEDS_PER_DIGIT;
  uint16_t color = ((timeinfo.tm_hour+7)%24)*(65535/24);
  //Serial.printf("%d ",num);
  for (int i=0; i<7; i++){
    for (int pos=0; pos<3; pos++){
      int led_num = offset + i*3 + pos;
      float angle = sin(millis()/1000.0+(led_num/30.0));
      float angle2 = cos(millis()/15000.0+led_num/30.0);
      brightness = 0;
      uint8_t c;
      if (night_mode) {
        brightness = global_brightness;
        c = color;
      } else {
        brightness = map(angle*100, -100, 100, global_brightness*0.7, global_brightness*1.2);;
        c = (color+(int)(angle2*25))%255;
      }
      
      brightness = constrain(brightness, min_brightness, max_brightness);
      // Serial.printf("color: %d ", color);
      // Serial.println();

      if (digits[num] & (1 << i)){
        if (night_mode){
          if (pos == 1){
            leds[led_num] = CHSV(c, 240, brightness);
          } else {
            leds[led_num] = CHSV(c, 240, brightness*0.6);
          }
        } else {
          leds[led_num] = CHSV(c, 240, brightness * lux_adjustment);
        } 
        
      } else {
        leds[led_num] = CRGB::Black;
      }
    }
  }
}

void display_time(){
  show_number(3, timeinfo.tm_hour / 10);
  show_number(2, timeinfo.tm_hour % 10);
  show_number(1, timeinfo.tm_min / 10);
  show_number(0, timeinfo.tm_min % 10);
}

void show_random_number(uint8_t digit, uint8_t num, uint16_t color){
  int offset = digit * LEDS_PER_DIGIT;
  
  for (int i=0; i<7; i++){
    for (int pos=0; pos<3; pos++){
      int led_num = offset + i*3 + pos;
      float angle = sin(millis()/1000.0+(led_num/30.0));
      brightness = map(angle*100, -100, 100, global_brightness*0.7, global_brightness*1.2);
      brightness = constrain(brightness, min_brightness, max_brightness);

      if (digits[num] & (1 << i)){
        leds[led_num] = CHSV(color, 240, brightness);
      } else {
        leds[led_num] = CRGB::Black;
      }
    }
  }
}

void display_random_numbers(){
  static unsigned long last_update = 0;
  static int random_num = 0;
  static uint16_t random_colors[4];
  
  // Update random number and colors every second
  if (millis() - last_update >= 1000) {
    random_num = random(10000);  // 0000-9999
    for (int i = 0; i < 4; i++) {
      random_colors[i] = random(256);  // Random hue for each digit
    }
    last_update = millis();
    Serial.printf("Random number: %04d\n", random_num);
  }
  
  // Display the 4-digit number with individual colors for each digit
  show_random_number(3, (random_num / 1000) % 10, random_colors[3]);
  show_random_number(2, (random_num / 100) % 10, random_colors[2]);
  show_random_number(1, (random_num / 10) % 10, random_colors[1]);
  show_random_number(0, random_num % 10, random_colors[0]);
}


void loop() {
  // Check button for config mode (only when not already in config mode)
  if (!config_mode_active) {
    check_button_for_config_mode();
  }
  
  // Handle WiFiManager process if in config mode
  if (config_mode_active) {
    wm.process(); // Handle captive portal
    display_ap_mode();
    FastLED.show();
    delay(100); // Reduce CPU usage in config mode
    return; // Don't execute normal clock logic
  }
  
  // Check if WiFi is still connected (in case it drops after initial connection)
  if (wifi_connected && !WiFi.isConnected()) {
    wifi_connected = false;
    Serial.println("WiFi connection lost! Switching to random number mode...");
  }
  
  if (wifi_connected) {
    // update time
    time(&now);
    localtime_r(&now, &timeinfo);

    if (last_minute != timeinfo.tm_min){
      last_minute = timeinfo.tm_min;
      Serial.println(getFormattedTime());
      fade = -1;
      count = 0;
    }

    if (fade != 0){
      // fade in or out
      if (fade == 1) {
        int amt = constrain(count,min_brightness/2,max_brightness);
        global_brightness = amt;
        display_time();
      }
      for (int i=0; i<NUM_LEDS; i++){
        leds[i].fadeToBlackBy(random(1));
      }
      if (count >= MAX_BRIGHTNESS){
        if (fade == 1){
          fade = 0;  // stop fading
          global_brightness = max_brightness;
        }
        if (fade == -1) fade = 1; // start fading in
        count = 0;
      }
    } else {
      // display time
      display_time();
    }
  } else {
    // WiFi not connected - display random numbers
    display_random_numbers();
    
    // Attempt to reconnect every 60 seconds
    static unsigned long last_reconnect_attempt = 0;
    if (millis() - last_reconnect_attempt >= 60000) {
      Serial.println("Attempting WiFi reconnection...");
      if (WiFi.isConnected()) {
        wifi_connected = true;
        Serial.println("WiFi reconnected! Switching back to time mode...");
        // Get current time
        if (getNTPtime(5)) {
          Serial.println("Time sync complete after reconnection");
        }
      }
      last_reconnect_attempt = millis();
    }
  }

  FastLED.show();  

#if BH1750_ENABLED
  // check for night mode 
  float sensor_lux = lightMeter.readLightLevel();
  lux = constrain(sensor_lux, 0, 300);
  if (lux < 10){
    night_mode = true;
  } else {
    night_mode = false;
  }
#endif

  if (night_mode){
    max_brightness = 35;
    min_brightness = 30;
  } else {
    lux_adjustment = map(lux, 0, 130, 50, 100)/100.0;
    lux_adjustment = constrain(lux_adjustment, 0.5, 1.0);
    max_brightness = MAX_BRIGHTNESS * lux_adjustment;
    min_brightness = MIN_BRIGHTNESS * lux_adjustment;  
  }
  
  delay(10);

  count++;
}
