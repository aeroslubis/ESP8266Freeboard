#include <ESP8266WiFi.h> /*ESP8266 core wifi library*/
#include <ESP8266mDNS.h> /*MulticastDNS library*/
#include <ESP8266WebServer.h> /*ESP8266 Webserver library*/
#include <ESP8266HTTPUpdateServer.h> /*ESP8266 HTTP Over The Air update library*/
#include "WebSocketsServer.h" /*WebSocket library*/
#include <FS.h>
#include "ArduinoJson.h"
#include "DHTesp.h"

#include "credentials.h" /*Default configuration*/

const char* _ssid = DEFAULT_SSID;
const char* _password = DEFAULT_PASSWORD;
const char* _update_username = DEFAULT_UPDATE_USERNAME;
const char* _update_password = DEFAULT_UPDATE_PASSWORD;

int temperature, humidity, pressure, light, analog;
int totalClient;
int pwm1_value, pwm2_value, pwm3_value;
unsigned long previousMillis;

ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;
WebSocketsServer ws = WebSocketsServer(81);
DHTesp dht;

void setup()
{
    Serial.begin(115200);
    Serial.print("Connecting to Wifi Access Point");
    /*WiFi.softAP(_ssid, _password);*/
    WiFi.begin(_ssid, _password);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print('.');
    }
    Serial.println(" OK");

    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("MAC Address: ");
    Serial.println(WiFi.macAddress().c_str());

    /*
     *Setup mDNS responder
     */
    Serial.print("Starting mDNS responder...");
    if (!MDNS.begin("esp8266"))
    {
        Serial.println(" FAIL");
        while (true) { delay(1000); }
    }
    Serial.println(" OK");

    /*
     *Setup ESP8266 Updater over the air
     */
    httpUpdater.setup(&server, "/update", _update_username, _update_password);

    /*
     *Setup SPIFFS
     */
    Serial.println("Listing SPIFF Files");
    SPIFFS.begin();
    Dir dir = SPIFFS.openDir("/");
    while (dir.next())
    {
        String fileName = dir.fileName();
        size_t fileSize = dir.fileSize();
        Serial.printf(" %s, size: %s\n", fileName.c_str(), formatBytes(fileSize).c_str());
    }

    Serial.print("Starting Webserver...");

    /*
     *Callback function for 404 filenotfound
     */
    server.onNotFound([]() {
        if (!handleFileRead(server.uri()))
        {
            server.send(404, "application/json", "FileNotFound");
        }
    });

    /*
     *Callback function on specific route
     */
    server.on("/info", handleESPInfo);
    server.on("/pwm", handlePWM);
    server.on("/gpio", handleGpio);
    server.begin();
    Serial.println(" OK");

    /*
     *Add service to MDNS-SD
     */
    MDNS.addService("http", "tcp", 80);

    /*
     *Setup Websocket server
     */
    Serial.print("Starting WebSocket server..");
    ws.begin();
    /*Callback function for websocket event*/
    ws.onEvent(webSocketEvent);
    Serial.println(" OK");

    /*
     *Setup DHT11 sensor
     */
    dht.setup(D8);

    /*
     *Setup Gpio pin as OUTPUT
     */
    pinMode(D1, OUTPUT);
    pinMode(D2, OUTPUT);
    pinMode(D3, OUTPUT);
    pinMode(D4, OUTPUT);
    pinMode(D5, OUTPUT);
    pinMode(D6, OUTPUT);
    pinMode(D7, OUTPUT);
}

void loop()
{
    ws.loop();
    server.handleClient();
    yield();

    if (millis() > previousMillis + 3000 && totalClient > 0)
    {
        previousMillis = millis();
        sendDataValue();
    }
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length)
{
    switch (type)
    {
        case WStype_DISCONNECTED:
        {
            Serial.printf("[%u] Disconnected!\r\n", num);
            totalClient--;
            blink();
            break;
        }

        case WStype_CONNECTED:
        {
            IPAddress ip = ws.remoteIP(num);
            Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\r\n", num, ip[0], ip[1], ip[2], ip[3], payload);
            totalClient++;
            blink();
            break;
        }
    }
}

void sendDataValue()
{
    temperature = dht.getTemperature();
    humidity = dht.getHumidity();
    analog = analogRead(A0);
    pressure = random(14, 16);
    light = random(25, 32);

    StaticJsonBuffer<250> jsonBuffer;
    JsonObject& root = jsonBuffer.createObject();
    /*
     *Send sensor value
     */
    root["temperature"] = temperature;
    root["humidity"] = humidity;
    root["pressure"] = pressure;
    root["light"] = light;
    /*
     *Send total connected client
     */
    root["clients"] = totalClient;
    /*
     *Send pin state of analog, digital and pwm pin
     */
    root["analog"] = analog;
    root["pwm_1"] = pwm1_value;
    root["pwm_2"] = pwm2_value;
    root["pwm_3"] = pwm3_value;
    root["D1"] = digitalRead(D1);
    root["D2"] = digitalRead(D2);
    root["D3"] = digitalRead(D3);
    root["D4"] = digitalRead(D4);

    String json = "";
    root.printTo(json);
    ws.broadcastTXT(json);
}

/*
 *Function for handling Gpio request
 *Gpio is set to OTPUT mode only
 *Only digital pin D1 to D4 are allowed
 */
void handleGpio()
{
    if (server.argName(0) == "pin" && server.argName(1) == "value")
    {
        char pin = server.arg(0).charAt(1);
        switch (pin - '0') /*Convert (char)pin to interger*/
        {
            case 1 : digitalWrite(D1, server.arg(1).toInt()); break;
            case 2 : digitalWrite(D2, server.arg(1).toInt()); break;
            case 3 : digitalWrite(D3, server.arg(1).toInt()); break;
            case 4 : digitalWrite(D4, server.arg(1).toInt()); break;
        }
        /*Freeboard slider widget expect a return value, so send one*/
        server.send(200, "text/plain", "OK");
    }
}

/*
 *Function for handling pwm request
 *Only digital pin D5 to D7 are allowed
 */
void handlePWM()
{
   if (server.argName(0) == "pin" && server.argName(1) == "value")
   {
        int pwm_value = server.arg(1).toInt();
        char pin = server.arg(0).charAt(1);
        switch (pin - '0') /*Convert (char)pin to interger*/
        {
            case 5 : analogWrite(D5, pwm_value); pwm1_value = pwm_value; break;
            case 6 : analogWrite(D6, pwm_value); pwm2_value = pwm_value; break;
            case 7 : analogWrite(D7, pwm_value); pwm3_value = pwm_value; break;
        }
        /*Freeboard slider widget expect a return value, so send one*/
        server.send(200, "text/plain", "OK");
   }
}

void handleESPInfo()
{
    StaticJsonBuffer<160> ESPInfo;
    JsonObject& root = ESPInfo.createObject();
    root["ip_address"] = WiFi.localIP().toString();
    root["mac_address"] = WiFi.macAddress();
    root["wifi_mode"] = "STA";
    root["wifi_signal"] = WiFi.RSSI();
    /*
     *Get ESP8266 free RAM
     */
    root["ram"] = ESP.getFreeHeap() / 98000;

    String json = "";
    root.printTo(json);
    server.send(200, "text/plain", json);
}

/*
 *Helper function for reading file from SPIFF
 */
String formatBytes(size_t bytes)
{
    if (bytes < 1024) return String(bytes)+"B";
    else if (bytes < (1024 * 1024)) return String(bytes/1024.0)+"KB";
    else if (bytes < (1024 * 1024 * 1024)) return String(bytes/1024.0/1024.0)+"MB";
    else return String(bytes/1024.0/1024.0/1024.0)+"GB";
}

String getContentType(String filename)
{
    if (server.hasArg("download")) return "application/octet-stream";
    else if (filename.endsWith(".htm")) return "text/html";
    else if (filename.endsWith(".html")) return "text/html";
    else if (filename.endsWith(".css")) return "text/css";
    else if (filename.endsWith(".js")) return "application/javascript";
    else if (filename.endsWith(".png")) return "image/png";
    else if (filename.endsWith(".gif")) return "image/gif";
    else if (filename.endsWith(".jpg")) return "image/jpeg";
    else if (filename.endsWith(".ico")) return "image/x-icon";
    else if (filename.endsWith(".xml")) return "text/xml";
    else if (filename.endsWith(".pdf")) return "application/x-pdf";
    else if (filename.endsWith(".zip")) return "application/x-zip";
    else if (filename.endsWith(".gz")) return "application/x-gzip";
    return "text/plain";
}

bool handleFileRead(String path)
{
    Serial.println("Transfering: " + path);
    if (path.endsWith("/")) path += "index.html";
    String contentType = getContentType(path);
    String pathWithGz = path + ".gz";
    if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path))
    {
        if (SPIFFS.exists(pathWithGz)) path += ".gz";
        File file = SPIFFS.open(path, "r");
        server.streamFile(file, contentType);
        file.close();
        return true;
    }
    return false;
}

void blink()
{
    for (int i = 0; i < 10; i++)
    {
        if (i%2) digitalWrite(D4, HIGH);
        else digitalWrite(D4, LOW);
        delay(50);
    }
}
