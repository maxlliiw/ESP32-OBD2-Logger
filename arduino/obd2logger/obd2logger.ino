#include "secrets.h"
#include <ArduinoJson.h>
#include <ArduinoWebsockets.h>
#include <FreematicsPlus.h>
#include <WiFi.h>

#define PIN_LED 4

FreematicsESP32 sys;
COBD obd;

bool WS_CONNECTED = false;
bool OBD2_CONNECTED = false;
unsigned long count = 0;

const char *ntpServer = "pool.ntp.org";
int epochTime = 0;

using namespace websockets;
WebsocketsClient client;
String ws_url = "ws://" + String(WS_HOST) + String(WS_ENDPOINT) +
                "?api_key=" + String(WS_KEY);
char vin[18] = {0};

void initWiFi() {
    Serial.println("Initializing WiFi connection");
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.println("\nConnecting");

    while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        delay(100);
    }

    Serial.println("\nConnected to the WiFi network");
    Serial.print("Local ESP32 IP: ");
    Serial.println(WiFi.localIP());
}

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

void connectWiFi() {
    if (WiFi.status() == WL_CONNECTION_LOST) {
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
    }
}

void connectWS() {
    while (!WS_CONNECTED) {
        if (client.connect(ws_url)) {
            Serial.println("[WebSocket] Connected to server");
            WS_CONNECTED = true;
        } else {
            Serial.println("[WebSocket] Connection failed");
        }
    }
}

void connectOBD() {
    if (!OBD2_CONNECTED) {
        digitalWrite(PIN_LED, HIGH);
        Serial.print("Connecting to OBD...");
        if (obd.init()) {
            Serial.println("OK");
            OBD2_CONNECTED = true;
            char buf[128];
            if (obd.getVIN(buf, sizeof(buf))) {
                memcpy(vin, buf, sizeof(vin) - 1);
                Serial.print("VIN:");
                Serial.println(vin);
            }
        } else {
            Serial.println();
        }
        digitalWrite(PIN_LED, LOW);
        return;
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

void setup() {
    // put your setup code here, to run once:
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, HIGH);
    delay(1000);
    digitalWrite(PIN_LED, LOW);
    Serial.begin(115200);

    // obd2 init
    while (!sys.begin());
    obd.begin(sys.link);
    delay(200);

    // wifi init
    initWiFi();
    delay(200);

    // websocket init
    client.onMessage(onMessageCallback);
    client.onEvent(onEventsCallback);
    connectWS();
    delay(200);

    // time init
    initTime();
    delay(200);
}

void loop() {
    digitalWrite(PIN_LED, HIGH);

    connectOBD();
    connectWiFi();

    client.poll();

    if (client.available() && OBD2_CONNECTED) {
		int engineSpeed = 0;
		int vehicleSpeed = 0;
		int batteryVoltage = obd.getVoltage();
		obd.readPID(PID_RPM, engineSpeed);
		obd.readPID(PID_SPEED, vehicleSpeed);
        unsigned long timestampMS = millis();

        String json = "{\"startTime\": " + String(epochTime) + 
                    ", \"timestampMS\": " + String(timestampMS) + 
					", \"vehicleSpeed\": " + String(vehicleSpeed) + 
					", \"engineSpeed\": " + String(engineSpeed) + 
					", \"batteryVoltage\": " + String(batteryVoltage) + "}";

        Serial.println("[WebSocket] Sending: " + json);
        client.send(json);
    } else {
        Serial.println("[WebSocket] Not connected — skipping send");
		WS_CONNECTED = false;
    }

	if (obd.errors > 2) {
        Serial.println("OBD disconnected");
        OBD2_CONNECTED = false;
        obd.reset();
    }

    digitalWrite(PIN_LED, LOW);
    delay(50);
}