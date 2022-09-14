// -*- eval: (platformio-mode 1); -*-
#include <Arduino.h>
#include <Servo.h>
#include <AM2320.h>
#include <ESP8266WiFi.h>

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
  unsigned short rpm;
  unsigned char door;
  bool master;
} config_t;

/* globals */
Servo water_servo, door_servo;
AM2320 temp_sensor;
#ifdef API_HTTPS
WiFiClientSecure client;
#else
WiFiClient client;
#endif

short control_val = 0;
config_t config = {.rpm = 0, .door = 0, .master = false};

#ifdef API_HTTPS
const char FINGERPRINT[] PROGMEM = API_FINGERPRINT;
#endif

/* data */
float temp = -1024.f, humid = -1024.f;

time_t last_post, last_config;

/* code */
void control(char c, short *val, short max, short min, short interval) {
  int p = 0;
  switch(c) {
  case '+':
    *val += interval;
    p = 1;
    break;

  case '-':
    *val -= interval;
    p = 1;
    break;

  case '1':
    *val = max;
    p = 1;
    break;

  case '0':
    *val = min;
    p = 1;
    break;

  case '=':
    *val = 0.5*(max+min);

  case 'p':
    p = 1;
    break;
  }
  *val = CLAMP(*val, max, min);
  if(p) {
    LOGF("control:%d\n", control_val);
  }
}

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

void post_data(float temp, float humid) {
  char data[50 + sizeof API_TOKEN + sizeof API_ID];
  int length;

  sprintf(data, "temp=%.3f&humidity=%.3f&id=%s&token=%s%n",
          temp, humid, API_ID, API_TOKEN, &length);
  LOGF("data: %s\nlength: %d\n", data, length);

  if(!api_connect(&client)) return;

  client.printf("POST %s HTTP/1.1\n", API_BASEPATH "/data");
  client.printf("Host: %s\n", API_HOST);
  client.printf("Content-Length: %d\n", length);
  client.println("Content-Type: application/x-www-form-urlencoded");
  client.println();
  client.println(data);

  /* wait for response */
#ifdef DEBUG_REQUEST
  String line;
  while(client.connected()) {
    line = client.readStringUntil('\n');
    LOGLN(line);
    if(line == "\r") break;
  }
  while(client.available())
    LOG((char)client.read())
#else
  while(client.available())
    client.read();
#endif
}

void update_config(config_t *config) {
  /* NYI */
}

void setup() {
  Serial.begin(115200);
  while(!Serial) delay(10);

  pinMode(AO_FAN_PIN, OUTPUT);
  pinMode(AO_WATER_PIN, OUTPUT);
  pinMode(AO_DOOR_PIN, OUTPUT);
  pinMode(TEMP_SDA, INPUT);
  pinMode(TEMP_SCL, OUTPUT);

  analogWriteRange(PWM_MAX);
  analogWriteFreq(1000);

  water_servo.attach(AO_WATER_PIN);
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
  while(Serial.available())
    control(Serial.read(), &control_val, CONTROL_MAX, CONTROL_MIN, CONTROL_INTERVAL);

  // analogWrite(AO_CONTROL_PIN, control_val);
  water_servo.write(control_val);

  if(millis() > last_config + CONFIG_INTERVAL) {
    last_config = millis();

    update_config(&config);
    LOGLN("Updated config");
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

      time_t start = millis();
      post_data(temp, humid);
      LOGF("Done: %llims\n", (time_t)millis() - start);
    }
  }
}
