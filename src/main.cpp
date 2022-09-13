// -*- eval: (platformio-mode 1); -*-
#include <Arduino.h>
#include <Servo.h>

#include "./secret.h"
#include "./config.h"

/* macros */
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
typedef long long ll;
typedef unsigned long long ull;

/* globals */
Servo water_servo, door_servo;

short control_val = 0.5*(CONTROL_MAX+CONTROL_MIN);

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
    Serial.printf("control:%d\n", control_val);
  }
}

void setup() {
  pinMode(AO_FAN_PIN, OUTPUT);
  pinMode(AO_WATER_PIN, OUTPUT);

  analogWriteRange(PWM_MAX);
  analogWriteFreq(1000);

  Serial.begin(115200);

  water_servo.attach(AO_WATER_PIN);
  door_servo.attach(AO_DOOR_PIN);
}

void loop() {
  while(Serial.available()) {
    control(Serial.read(), &control_val, CONTROL_MAX, CONTROL_MIN, CONTROL_INTERVAL);
  }

  // analogWrite(AO_CONTROL_PIN, control_val);
  water_servo.write(control_val);
}
