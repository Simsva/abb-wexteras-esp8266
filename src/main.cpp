// -*- eval: (platformio-mode 1); -*-
#include <Arduino.h>
#include <Servo.h>
#include <AM2320.h>
#include <ESP8266WiFi.h>

#define ARDUINOJSON_USE_DOUBLE 0
#define ARDUINOJSON_USE_LONG_LONG 0
#include <ArduinoJson.h>

#include "./secret.h"
#include "./config.h"

/* macros */
#ifdef DEBUG_LOG
# define LOG(s)             Serial.print((s));
# define LOGLN(s)           Serial.println((s));
# define LOGF(fmt, args...) Serial.printf((fmt), args);
#else
# define LOG(s)
# define LOGLN(s)
# define LOGF(f, ...)
#endif

#define CLAMP(x, max, min) (((x) > (max)) \
                            ? (max) \
                            : (((x) < (min)) \
                            ? (min) \
                            : (x)))

#define PWM_MAX (1023)
#define PWM_MIN (0)

#define CONTROL_MAX (180)
#define CONTROL_MIN (0)
#define CONTROL_INTERVAL (1)

/* types */
typedef struct {
  unsigned short rpm, water;
  unsigned char door;
  bool master;
} config_t;

/* globals */
Servo door_servo;
AM2320 temp_sensor;
#ifdef API_HTTPS
WiFiClientSecure client;
#else
WiFiClient client;
#endif

config_t config = {.rpm = 0, .water = 120, .door = 0, .master = false};

#ifdef API_HTTPS
const char FINGERPRINT[] PROGMEM = API_FINGERPRINT;
#endif

/* data */
float temp = -1024.f, humid = -1024.f;

time_t last_post = 0, last_config = 0,
       last_water = 0, stop_water = 0;

/* code */
bool api_connect(WiFiClient *c) {
  if(!c->connected()) {
    LOG("Connecting to API");
    int r = 0;
    while(!c->connect(API_HOST, API_PORT) && r++ < API_RETRIES) {
      delay(100);
      LOG('.');
    }
    LOGLN("");

    if(r > API_RETRIES) {
      LOGLN("Connection failed");
      return false;
    }
    LOGF("Connected in %d tries!\n", r);
  }
  return true;
}

bool post_data(float temp, float humid) {
  char data[50 + sizeof API_TOKEN + sizeof API_ID];
  int length;

  sprintf(data, "temp=%.3f&humidity=%.3f&id=%s&token=%s%n",
          temp, humid, API_ID, API_TOKEN, &length);
  LOGF("data: %s\nlength: %d\n", data, length);

  if(!api_connect(&client)) return false;

  /* flush response */
  while(client.available()) client.read();

  client.println("POST " API_BASEPATH "/data HTTP/1.1");
  client.println("Host: " API_HOST);
  client.printf("Content-Length: %d\n", length);
  client.println("Content-Type: application/x-www-form-urlencoded");
  client.println();
  if(!client.println(data)) {
    LOGLN("Failed to send /data request");
    client.stop();
    return false;
  }

  /* ignore response */
  while(client.available()) client.read();

  return true;
}

bool update_config(config_t *config) {
  if(!api_connect(&client)) return false;

  /* flush response */
  while(client.available()) client.read();

  client.println("GET " API_BASEPATH "/settings?id=" API_ID "&fields=rpm,door,master,water HTTP/1.1");
  client.println("Host: " API_HOST);
  if(!client.println()) {
    LOGLN("Failed to send /settings request");
    client.stop();
    return false;
  }

  /* NOTE: readBytesUntil does not seem to add a NULL byte */
  char status[32] = {0};
  client.readBytesUntil('\r', status, sizeof status);
  if(strcmp("HTTP/1.1 200 OK", status)) {
    LOGF("Unexpected response from /settings: %s\n", status);
    client.stop();
    return false;
  }

  if(!client.find("\r\n\r\n")) {
    LOGLN("Invalid response from /settings");
    client.stop();
    return false;
  }

  /* extract JSON from response */
  char len_buf[6], json_buf[128];
  int len = 1, i = 0;
  while(client.available()) {
    client.readBytesUntil('\n', len_buf, sizeof len_buf);
    len = (int)strtol(len_buf, NULL, 16);

    if(len == 0) break;
    ++len;
    while(--len > 0 && i < 127 && client.available())
      json_buf[i++]= (char)client.read();

    json_buf[i] = '\0';
    LOGF("partial json: %s\n", json_buf);

    /* discard CRLF */
    client.read();
    client.read();
  }
  /* flush response to be sure */
  while(client.available()) client.read();

  json_buf[i] = '\0';
  LOGF("json: %s\n", json_buf);

  StaticJsonDocument<96> json;
  DeserializationError err = deserializeJson(json, json_buf);
  if(err) {
    LOGF("Failed to parse json: %s\n", err.c_str());
    client.stop();
    return false;
  }

  config->master = json["master"].as<bool>();

  config->door = json["door"].as<unsigned char>();
  config->door = CLAMP(config->door, 180, 0);

  config->rpm = json["rpm"].as<unsigned short>();
  config->rpm = CLAMP(config->rpm, 1023, 0);

  config->water = json["water"].as<unsigned short>();
  config->water = CLAMP(config->water, INT16_MAX, 0);
  return true;
}

void setup() {
#ifdef DEBUG_LOG
  Serial.begin(115200);
  while(!Serial) delay(10);
#endif

  pinMode(AO_FAN_PIN, OUTPUT);
  pinMode(AO_WATER_PIN, OUTPUT);
  pinMode(AO_DOOR_PIN, OUTPUT);
  pinMode(TEMP_SDA, INPUT);
  pinMode(TEMP_SCL, OUTPUT);

  analogWriteRange(PWM_MAX);
  analogWriteFreq(1000);

  door_servo.attach(AO_DOOR_PIN);
  temp_sensor.begin(TEMP_SDA, TEMP_SCL);

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while(WiFi.status() != WL_CONNECTED) {
    delay(500);
    LOG('.');
  }
  LOGLN("");

  LOGF("Connected! IP address: %s\n", WiFi.localIP().toString().c_str());
  LOGF("MAC address: %s\n", WiFi.macAddress().c_str());

#ifdef API_HTTPS
  LOGF("Fingerprint: %s\n", FINGERPRINT);
  client.setFingerprint(FINGERPRINT);
#endif
  client.setTimeout(15000);
}

void loop() {
  analogWrite(AO_WATER_PIN, config.water);

  if(config.master) {
    door_servo.write(config.door);
    analogWrite(AO_FAN_PIN, config.rpm);
  } else {
    /* TODO: automatic control */
    door_servo.write(0);
    analogWrite(AO_FAN_PIN, 512);
  }

  if(millis() > last_config + CONFIG_INTERVAL) {
    last_config = millis();

    if(update_config(&config)) {
      LOGLN("Updated config");
    } else {
      LOGLN("Failed to update config");
    }

    LOGF("cfg: master=%d door=%d rpm=%d water=%d\n",
         config.master, config.door, config.rpm, config.water);
  }

  if(millis() > last_water + config.water*1000) {
    LOGLN("start water");
    analogWrite(AO_WATER_PIN, WATER_POWER);
    stop_water = millis() + WATER_DELAY;
    last_water = INT64_MAX>>1;
  }

  if(millis() > stop_water) {
    LOGLN("stop water");
    analogWrite(AO_WATER_PIN, 0);
    stop_water = INT64_MAX;
    last_water = millis();
  }

  if(millis() > last_post + POST_INTERVAL) {
    last_post = millis();

    if(!temp_sensor.measure()) {
      switch(temp_sensor.getErrorCode()) {
      case 1: LOGLN("[ERR] temp_sensor is offline"); break;
      case 2: LOGLN("[ERR] temp_sensor CRC validation failed"); break;
      }
    } else {
      temp = temp_sensor.getTemperature();
      humid = temp_sensor.getHumidity();
      LOGF("temp: %f\nhumid: %f\n", temp, humid);

#ifdef DEBUG_LOG
      time_t start = millis();
#endif
      if(post_data(temp, humid)) {
        LOGF("Posted data in %llims\n", (time_t)millis() - start);
      } else {
        LOGLN("Failed to post data");
      }
    }
  }
}
