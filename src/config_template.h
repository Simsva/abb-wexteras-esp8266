// -*- eval: (platformio-mode 1); -*-
#ifndef CONFIG_H_
#define CONFIG_H_

/* debug */
//#define DEBUG_LOG
//#define DEBUG_REQUEST

/* pins */
#define AO_FAN_PIN    D4
#define AO_DOOR_PIN   D5
#define AO_WATER_PIN  D6
#define TEMP_SCL      D7
#define TEMP_SDA      D8

/* intervals (ms) */
#define POST_INTERVAL   1000
#define CONFIG_INTERVAL 1000

/* api */
#define API_HOST        "api.simsva.se"
#define API_BASEPATH    "/wexteras"
#define API_PORT        443
/* SHA-1 fingerprint of HTTPS certificate */
#define API_FINGERPRINT "CE 11 C9 02 AF 21 4F 7E DA 4E A4 94 42 17 7A B5 82 65 B3 DA"
#define API_RETRIES     60
/* define if HTTPS is used */
#define API_HTTPS

#endif // CONFIG_H_
