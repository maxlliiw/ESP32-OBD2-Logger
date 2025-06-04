/*************************************************************************
 * Vehicle Telemetry Data Logger for Freematics ONE+
 *
 * Developed by Stanley Huang <stanley@freematics.com.au>
 * Distributed under BSD license
 * Visit https://freematics.com/products/freematics-one-plus for more info
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *************************************************************************/

#include "config.h"
#include "secrets.h"
#include <ArduinoWebsockets.h>
#include <FS.h>
#include <FreematicsPlus.h>
#include <WiFi.h>
#include <httpd.h>

// states
#define STATE_STORE_READY 0x1
#define STATE_OBD_READY 0x2
#define STATE_GPS_FOUND 0x4
#define STATE_GPS_READY 0x8
#define STATE_MEMS_READY 0x20
#define STATE_STANDBY 0x80

uint32_t startTime = 0;
uint32_t pidErrors = 0;
uint32_t fileid = 0;
uint32_t lastOBDCheckTime = 0;
uint32_t lastStatsTime = 0;

bool WIFI_CONNECTED = false;
bool WS_CONNECTED = false;
bool OBD2_CONNECTED = false;

using namespace websockets;
WebsocketsClient client;
String ws_url = "ws://" + String(WS_HOST) + String(WS_ENDPOINT) +
                "?api_key=" + String(WS_KEY);

const char *ntpServer = "pool.ntp.org";
int epochTime = 0;

// live data
char vin[18] = {0};
int16_t batteryVoltage = 0;

typedef struct {
    byte pid;
    byte tier;
    int value;
    uint32_t ts;
} PID_POLLING_INFO;
PID_POLLING_INFO obdData[] = {{PID_ENGINE_LOAD, 1},
                              {PID_COOLANT_TEMP, 1},
                              {PID_RPM, 1},
                              {PID_SPEED, 1},
                              {PID_THROTTLE, 1},
                              {PID_FUEL_PRESSURE, 2},
                              {PID_INTAKE_MAP, 2},
                              {PID_INTAKE_TEMP, 2},
                              {PID_MAF_FLOW, 2},
                              {PID_AIR_FUEL_EQUIV_RATIO, 2},
                              {PID_TIMING_ADVANCE, 2},
                              {PID_SHORT_TERM_FUEL_TRIM_1, 3},
                              {PID_SHORT_TERM_FUEL_TRIM_2, 3},
                              {PID_BAROMETRIC, 3},
                              {PID_LONG_TERM_FUEL_TRIM_1, 3},
                              {PID_LONG_TERM_FUEL_TRIM_2, 3}};

#if USE_MEMS
float acc[3] = {0};
float gyr[3] = {0};
float mag[3] = {0};
float accBias[3];
float temp = 0;
ORIENTATION ori = {0};
#endif

#if USE_GNSS
GPS_DATA *gd = 0;
uint32_t lastGPStime = 0;
#endif

FreematicsESP32 sys;

COBD obd;

void initTime() {
    configTime(0, 0, ntpServer);
    while (epochTime == 0) {
        time_t now;
        struct tm timeinfo;
        if (!getLocalTime(&timeinfo)) {
            epochTime = 0;
        } else {
			time(&now);
			epochTime = now;
		}
    }
}

void initWiFi() {
    Serial.println("Initializing WiFi connection");
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.println("\nConnecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        delay(100);
    }
    Serial.println("\nConnected to the WiFi network");
    Serial.print("Local ESP32 IP: ");
    Serial.println(WiFi.localIP());
    WIFI_CONNECTED = true;
    initTime();
}

void connectWiFi() {
    if (WiFi.status() == WL_CONNECTION_LOST) {
        WIFI_CONNECTED = false;
        client.end();
        WS_CONNECTED = false;
        Serial.println("WiFi connection lost!");
        Serial.println("Attempting to Reconnect");
        delay(1000);
        while (WiFi.status() != WL_CONNECTED) {
            Serial.print(".");
            delay(100);
        }
        Serial.println("\nConnected to the WiFi network");
        Serial.print("Local ESP32 IP: ");
        Serial.println(WiFi.localIP());
        WIFI_CONNECTED = true;
        initTime();
        connectWS();
    }
}

void connectWS() {
    while (!WS_CONNECTED) {
        if (client.connect(ws_url)) {
            Serial.println("[WebSocket] Connected to server");
            WS_CONNECTED = true;
        } else {
            Serial.println("[WebSocket] Connection failed");
            WS_CONNECTED = false;
        }
    }
}

void onMessageCallback(WebsocketsMessage message) {
    Serial.print("[WebSocket] Got Message: ");
    Serial.println(message.data());
}

void onEventsCallback(WebsocketsEvent event, String data) {
    if (event == WebsocketsEvent::ConnectionOpened) {
        Serial.println("[WebSocket] Connnection Opened");
    } else if (event == WebsocketsEvent::ConnectionClosed) {
        Serial.println("[WebSocket] Connnection Closed");
    } else if (event == WebsocketsEvent::GotPing) {
        Serial.println("[WebSocket] Got a Ping!");
    } else if (event == WebsocketsEvent::GotPong) {
        Serial.println("[WebSocket] Got a Pong!");
    }
}

#if USE_MEMS
MEMS_I2C *mems = 0;
void calibrateMEMS() {
    // MEMS data collected while sleeping
    accBias[0] = 0;
    accBias[1] = 0;
    accBias[2] = 0;
    int n;
    for (n = 0; n < 100; n++) {
        mems->read(acc);
        accBias[0] += acc[0];
        accBias[1] += acc[1];
        accBias[2] += acc[2];
        delay(10);
    }
    accBias[0] /= n;
    accBias[1] /= n;
    accBias[2] /= n;
    Serial.print("ACC Bias:");
    Serial.print(accBias[0]);
    Serial.print('/');
    Serial.print(accBias[1]);
    Serial.print('/');
    Serial.println(accBias[2]);
}
#endif

void handleLiveData() {
    int obdbuffersize = 2048;
    char obdbuffer[obdbuffersize] = {0};
    uint32_t ts_now = millis();
    int n = snprintf(obdbuffer, obdbuffersize,
                     "{\"st\":%u,\"ts\":%u,\"obd\":{\"vin\":\"%s\",\"battery\":%d,\"pids\":{", epochTime, ts_now, vin,
                     (int)batteryVoltage);
    for (int i = 0; i < sizeof(obdData) / sizeof(obdData[0]); i++) {
        n += snprintf(
            obdbuffer + n, obdbuffersize - n, "\"%u\":%d,", obdData[i].pid, obdData[i].value);
    }
    n--;
    n += snprintf(obdbuffer + n, obdbuffersize - n, "}}");
#if USE_MEMS
    n += snprintf(obdbuffer + n, obdbuffersize - n, ",\"mems\":{\"acc\":[%d,%d,%d]",
                  (int)((acc[0] - accBias[0]) * 100),
                  (int)((acc[1] - accBias[1]) * 100),
                  (int)((acc[2] - accBias[2]) * 100));
    obdbuffer[n++] = '}';
#endif
#if USE_GNSS
    if (lastGPStime) {
        n += snprintf(
            obdbuffer + n, obdbuffersize - n,
            ",\"gps\":{\"lat\":%f,\"lng\":%f,\"alt\":%f,\"speed\":%f}",
            gd->lat, gd->lng, gd->alt, gd->speed);
    }
#endif
    obdbuffer[n++] = '}';
    client.send(obdbuffer, n);
    delay(100);
}

void listDir(fs::FS &fs, const char *dirname, uint8_t levels) {
    Serial.printf("Listing directory: %s\n", dirname);
    fs::File root = fs.open(dirname);
    if (!root) {
        Serial.println("Failed to open directory");
        return;
    }
    if (!root.isDirectory()) {
        Serial.println("Not a directory");
        return;
    }

    fs::File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            Serial.println(file.name());
            if (levels) {
                listDir(fs, file.name(), levels - 1);
            }
        } else {
            Serial.print(file.name());
            Serial.print(' ');
            Serial.print(file.size());
            Serial.println(" bytes");
        }
        file = root.openNextFile();
    }
}

class DataLogger {
  public:
    void init() {
#if USE_GNSS == 1
        if (!checkState(STATE_GPS_FOUND)) {
            Serial.print("GNSS:");
            if (sys.gpsBeginExt(GPS_SERIAL_BAUDRATE)) {
                setState(STATE_GPS_FOUND);
                Serial.println("OK(E)");
            } else if (sys.gpsBegin()) {
                setState(STATE_GPS_FOUND);
                Serial.println("OK(I)");
            } else {
                Serial.println("NO");
            }
        }
#endif

#if USE_OBD
        Serial.print("OBD:");
        if (obd.init()) {
            Serial.println("OK");
            pidErrors = 0;
            // retrieve VIN
            Serial.print("VIN:");
            char buffer[128];
            if (obd.getVIN(buffer, sizeof(buffer))) {
                Serial.println(buffer);
                strncpy(vin, buffer, sizeof(vin) - 1);
            } else {
                Serial.println("NO");
            }
            setState(STATE_OBD_READY);
        } else {
            Serial.println("NO");
        }
#endif
        startTime = millis();
    }
#if USE_GNSS == 1
    void processGPSData() {
        // issue the command to get parsed GPS data
        if (checkState(STATE_GPS_FOUND) && sys.gpsGetData(&gd)) {
            if (lastGPStime == gd->time)
                return;
            setState(STATE_GPS_READY);
            lastGPStime = gd->time;
            setState(STATE_GPS_READY);
        }
    }
    void waitGPS() {
        int elapsed = 0;
        for (uint32_t t = millis(); millis() - t < 300000;) {
            int t1 = (millis() - t) / 1000;
            if (t1 != elapsed) {
                Serial.print("Waiting for GPS (");
                Serial.print(elapsed);
                Serial.println(")");
                elapsed = t1;
            }
            // read parsed GPS data
            if (sys.gpsGetData(&gd) && gd->sat != 0 && gd->sat != 255) {
                Serial.print("Sats:");
                Serial.println(gd->sat);
                break;
            }
        }
    }
#endif
    void standby() {
#if USE_GNSS == 1
        if (checkState(STATE_GPS_READY)) {
            Serial.print("GNSS:");
            sys.gpsEnd(); // turn off GPS power
            Serial.println("OFF");
        }
#endif
        // this will put co-processor into a delayed sleep
        sys.resetLink();
        clearState(STATE_OBD_READY | STATE_GPS_READY);
        setState(STATE_STANDBY);
        Serial.println("Standby");
#if USE_MEMS
        if (checkState(STATE_MEMS_READY)) {
            calibrateMEMS();
            while (checkState(STATE_STANDBY)) {
                // calculate relative movement
                float motion = 0;
                unsigned int n = 0;
                for (uint32_t t = millis(); millis() - t < 1000; n++) {
                    mems->read(acc, 0, 0, &temp);
                    for (byte i = 0; i < 3; i++) {
                        float m = (acc[i] - accBias[i]);
                        motion += m * m;
                    }
                }
                // check movement
                if (motion / n >=
                    WAKEUP_MOTION_THRESHOLD * WAKEUP_MOTION_THRESHOLD) {
                    Serial.print("Motion:");
                    Serial.println(motion / n);
                    break;
                }
            }
        }
#else
        while (!obd.init())
            Serial.print('.');
#endif
        Serial.println("Wakeup");
        // this will wake up co-processor
        sys.reactivateLink();
        // ESP.restart();
        clearState(STATE_STANDBY);
    }
    bool checkState(byte flags) { return (m_state & flags) == flags; }
    void setState(byte flags) { m_state |= flags; }
    void clearState(byte flags) { m_state &= ~flags; }

  private:
    byte m_state = 0;
};

DataLogger logger;

void showSysInfo() {
    Serial.print("CPU:");
    Serial.print(ESP.getCpuFreqMHz());
    Serial.print("MHz FLASH:");
    Serial.print(ESP.getFlashChipSize() >> 20);
    Serial.println("MB");
    Serial.print("IRAM:");
    Serial.print(ESP.getHeapSize() >> 10);
    Serial.print("KB");
    Serial.println();
}

void setup() {
    delay(500);

    Serial.begin(115200);
    showSysInfo();

#ifdef PIN_LED
    // init LED pin
    pinMode(PIN_LED, OUTPUT);
    pinMode(PIN_LED, HIGH);
#endif

#if USE_OBD
    if (sys.begin(true, USE_GNSS > 1)) {
        Serial.print("TYPE:");
        Serial.println(sys.devType);
        obd.begin(sys.link);
    }
#else
    sys.begin(false, USE_GNSS > 1);
#endif

    // initMesh();

#if USE_MEMS
    if (!logger.checkState(STATE_MEMS_READY))
        do {
            Serial.print("MEMS:");
            mems = new ICM_42627;
            byte ret = mems->begin();
            if (ret) {
                logger.setState(STATE_MEMS_READY);
                Serial.println("ICM-42627");
                break;
            }
            delete mems;
            mems = new ICM_20948_I2C;
            ret = mems->begin();
            if (ret) {
                logger.setState(STATE_MEMS_READY);
                Serial.println("ICM-20948");
                break;
            }
            delete mems;
            mems = new MPU9250;
            ret = mems->begin();
            if (ret) {
                logger.setState(STATE_MEMS_READY);
                Serial.println("MPU-9250");
                break;
            }
            Serial.println("NO");
        } while (0);
#endif

#ifdef PIN_LED
    pinMode(PIN_LED, LOW);
#endif

    logger.init();

    // wifi init
    initWiFi();
    delay(500);

    client.onMessage(onMessageCallback);
    client.onEvent(onEventsCallback);
    connectWS();
    delay(500);
}

void loop() {

    connectWiFi();
    client.poll();
    

#if USE_GNSS == 1
    if (logger.checkState(STATE_GPS_FOUND)) {
        logger.processGPSData();
    }
#endif

#if USE_OBD
    if (logger.checkState(STATE_STANDBY)) {
        logger.standby();
        Serial.print("OBD:");
        if (!obd.init()) {
            Serial.println("NO");
            return;
        }
        logger.setState(STATE_OBD_READY);
        Serial.println("OK");
        startTime = millis();
    }
#endif

#if USE_OBD
    if (logger.checkState(STATE_OBD_READY)) {
        // poll and log OBD data
        static int idx[2] = {0, 0};
        int tier = 1;
        for (byte i = 0; i < sizeof(obdData) / sizeof(obdData[0]); i++) {
            if (obdData[i].tier > tier) {
                // reset previous tier index
                idx[tier - 2] = 0;
                // keep new tier number
                tier = obdData[i].tier;
                // move up current tier index
                i += idx[tier - 2]++;
                // check if into next tier
                if (obdData[i].tier != tier) {
                    idx[tier - 2] = 0;
                    i--;
                    continue;
                }
            }
            byte pid = obdData[i].pid;
            if (!obd.isValidPID(pid))
                continue;
            if (obd.readPID(pid, obdData[i].value)) {
                obdData[i].ts = millis();
            } else {
                pidErrors++;
                Serial.print("PID ");
                Serial.print((int)pid | 0x100, HEX);
                Serial.print(" Error #");
                Serial.println(pidErrors);
                break;
            }
#if USE_GNSS == 1
            if (logger.checkState(STATE_GPS_FOUND)) {
                logger.processGPSData();
            }
#endif
            if (tier > 1)
                break;
        }

        // log battery voltage (from voltmeter), data in 0.01v
        batteryVoltage = obd.getVoltage() * 100;

        if (obd.errors >= 3) {
            logger.clearState(STATE_OBD_READY);
            logger.setState(STATE_STANDBY);
        }
    } else if (millis() - lastOBDCheckTime >= OBD_RETRY_INTERVAL) {
        Serial.print("OBD:");
        if (obd.init()) {
            logger.setState(STATE_OBD_READY);
            Serial.println("OK");
        } else {
            Serial.println("NO");
        }
        lastOBDCheckTime = millis();
    }
#endif

#if USE_MEMS
    if (logger.checkState(STATE_MEMS_READY)) {
        bool updated;
        updated = mems->read(acc, gyr, mag, &temp);
        // if (updated) {
        //     store.log(PID_ACC, (int16_t)(acc[0] * 100), (int16_t)(acc[1] * 100),
        //               (int16_t)(acc[2] * 100));
        //     store.log(PID_GYRO, (int16_t)(gyr[0] * 100),
        //               (int16_t)(gyr[1] * 100), (int16_t)(gyr[2] * 100));
        // }
    }
#endif

    if (client.available()) {
        handleLiveData();
    } else {
        WS_CONNECTED = false;
        connectWS();
    }
}
