#include <Arduino.h>
#include <FastLED.h>
#include <WiFiManager.h>
#include <Wire.h>
#include <BH1750.h>
#include "Ticker.h"

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

#define NUM_DIGITS 4
#define LEDS_PER_DIGIT 3*7
#define NUM_LEDS NUM_DIGITS*LEDS_PER_DIGIT  // 6x4 LEDs per character, 2 characters per display
#define MAX_BRIGHTNESS 200
#define MIN_BRIGHTNESS 80

CRGB leds[NUM_LEDS];
int global_brightness = (MAX_BRIGHTNESS-MIN_BRIGHTNESS)/2;
int max_brightness = MAX_BRIGHTNESS;
int min_brightness = MIN_BRIGHTNESS;
int brightness;
float lux_adjustment = 1.0;
bool night_mode = false;
int count = 0;
int fade = 0;

BH1750 lightMeter;

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

// Time 
tm timeinfo;
time_t now;
int hour = 0;
int minute = 0;
int second = 0;

// Time, date, and tracking state
int last_minute=0;



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

void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  Serial.println(wm.getConfigPortalSSID());
}

void ConnectToWifi(){
  Serial.print("Connecting to WiFi");
  WiFi.mode(WIFI_STA);
  wm.setAPCallback(configModeCallback);

  //wm.resetSettings();   // uncomment to force a reset
  bool wifi_connected = wm.autoConnect("Mini Clock RGB");
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
  } else {
    Serial.println("ERROR: WiFi connect failure");
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

  float lux = lightMeter.readLightLevel();
  Serial.print("Light: ");
  Serial.print(lux);
  Serial.println(" lx");
}

void setup() {
  // set up fastled
  Serial.begin(115200);
  FastLED.addLeds<WS2812B, 5, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(180);

  pinMode(0, INPUT_PULLUP);

  delay(300);
  Serial.println("Start");
  ConnectToWifi();
  delay(200);

  // Initialize the I2C bus (BH1750 library doesn't do this automatically)
  // On esp8266 devices you can select SCL and SDA pins using Wire.begin(D4, D3);
  Wire.begin();
  lightMeter.begin();

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


void loop() {
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

  FastLED.show();  

  // check for night mode 
  float lux = lightMeter.readLightLevel();
  lux = constrain(lux, 0, 300);
  if (lux < 10){
    night_mode = true;
  } else {
    night_mode = false;
  }

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
