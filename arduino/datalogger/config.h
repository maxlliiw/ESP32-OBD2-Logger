#define MEMS_DISABLED 0
#define MEMS_ACC 1
#define MEMS_9DOF 2
#define MEMS_DMP 3

#define STORAGE_NONE 1
#define STORAGE_SD 0
#define STORAGE_SPIFFS 0

/**************************************
* Data logging
**************************************/
#ifndef HAVE_CONFIG
// enable(1)/disable(0) serial data output
#define ENABLE_SERIAL_OUT 0
// specify storage type
#define STORAGE STORAGE_SD
#endif

#define WIFI_JOIN_TIMEOUT 30000
#define ENABLE_NMEA_SERVER 0
#define NMEA_TCP_PORT 4000

/**************************************
* Hardware setup
**************************************/
#ifndef HAVE_CONFIG
// enable(1)/disable(0) OBD-II reading
#define USE_OBD 1
// GNSS option: 0:disable 1:standalone 2:SIM5360/7600 3:SIM7070
#define USE_GNSS 0
// enable(1)/disable(0) MEMS motion sensor
#define USE_MEMS 0
#endif

// enable(1)/disable(0) BLE SPP server (for Freematics Controller App).
#define ENABLE_BLE 0

/**************************************
* Parameters
**************************************/
// stats update interval
#define STATS_INTERVAL 500
// OBD retry interval
#define OBD_RETRY_INTERVAL 3000
// GPS parameters
#define GPS_SERIAL_BAUDRATE 115200L
// motion detection
#define WAKEUP_MOTION_THRESHOLD 0.3 /* G */