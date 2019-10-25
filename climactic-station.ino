#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include "psswd.h"
#include "user_interface.h"

#include <LiquidCrystal_I2C.h>
LiquidCrystal_I2C lcd(0x3F,20,4);

#include <BME280I2C.h>
#include <Wire.h>

BME280I2C bme;    // Default : forced mode, standby time = 1000 ms
                  // Oversampling = pressure ×1, temperature ×1, humidity ×1, filter off,

const char* ssid = STASSID;
const char* password = STAPSK;

WiFiUDP Udp;
unsigned int localUdpPort = 2390;
char packetBuffer[255]; 
char registredBuffer[] = "R";

IPAddress broadcast;
IPAddress remoteServer;

const String version = "0.5.1";

ESP8266WebServer server(80);

const int BUZZER = 14;
// Status LEDs
const int RED = 15;                   // Error
const int YELLOW = 13;                // Booting, waiting, etc
const int GREEN = 12;                 // Everything good!
const int NONE = -1;
const int STATUS_LED[] = { 
  GREEN,
  YELLOW, 
  RED
};
int currentStatus = STATUS_LED[1];

void setupStatusLeds() {
    for(int i = 0; i < 3; i++) {
      pinMode(STATUS_LED[i], OUTPUT);
    }
}

void setStatus(int status) {
  if(status == currentStatus) {
    return;
  }
  
  if(currentStatus != -1) {
    digitalWrite(currentStatus, LOW);
  }

  if(status != NONE) {
    digitalWrite(status, HIGH);   
  }
  
  currentStatus = status;  
}

void shutOnboardLeds() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, 1);
}

void beep(int delayMS = 250) {
  digitalWrite(BUZZER, HIGH);
  delay(delayMS);
  digitalWrite(BUZZER, LOW);
}

void doubleBeep() {
  beep(200);
  delay(50);
  beep(200);  
}

boolean discoverServer() {
  Serial.println("Sending discovery request");
  Udp.beginPacket(broadcast, localUdpPort);
  Udp.write("D");
  Udp.endPacket();
}

void setup(void) {\
  pinMode(BUZZER, OUTPUT);
  beep();
  
  setupStatusLeds();
  setStatus(YELLOW);
  
  Serial.begin(115200);
  while(!Serial) {} // Wait
  Wire.begin();
  
  WiFi.mode(WIFI_STA);

  wifi_set_sleep_type(NONE_SLEEP_T); //LIGHT_SLEEP_T and MODE_SLEEP_T
  
  WiFi.begin(ssid, password);
  Serial.println("");

  int wifiAttempts = 0;

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {  
    setStatus(NONE);
    delay(250);
    Serial.print(".");
    setStatus(YELLOW);
    delay(250);
    wifiAttempts++;

    if(wifiAttempts >= 50) {
      setStatus(RED);
      doubleBeep();
      Serial.println("");
      Serial.println("**ERROR: Unable to connect to network, are your credentials right?**");
      return;
    }
  }
  
  Serial.println("");
  Serial.print("Connected.");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0,0);
  lcd.print("IP: ");
  lcd.setCursor(0,1);
  lcd.print(WiFi.localIP().toString());
  
  if (MDNS.begin("esp8266")) {
    Serial.println("MDNS responder started");
  }

  server.on("/", []() {
    server.send(200, "text/json", "{\"version\":\"" + String(version) + "\"}");
  });

  // Kinda... useless _for now_
  server.on("/info", []() {
    String res = "{\"ip_address\":\"" + WiFi.localIP().toString() + "\"}";
    server.send(200, "text/json", res);
  });

  server.on("/data", []() {
    server.send(200, "text/json", envSensorData());
  });

  server.on("/datah", []() {
    server.send(200, "text/html", envSensorDataHTML());
  });

  server.on("/beep", []() {
    if(server.method() == HTTP_POST) {
      beep();
      server.send(200, "text/json", "{\"done\": true}");
    } else {
      server.send(405);
    }
  });

  
  server.on("/dbeep", []() {
    if(server.method() == HTTP_POST) {
      doubleBeep();
      server.send(200, "text/json", "{\"done\": true}");
    } else {
      server.send(405);
    }
  });

  // Used by the master server to verify if a sara-air node
  server.on("/climactic-station-node", []() {
    server.send(200, "text/json", "{\"climactic-station-node\": true}");
  });
 
  server.begin();
  Serial.println("HTTP server started");

  Serial.println("");
  Serial.println("Connecting to BME280 sensor");
  while(!bme.begin())
  {
    setStatus(NONE);
    delay(250);
    Serial.print(".");
    setStatus(YELLOW);
    delay(250);
  }

  switch(bme.chipModel())
  {
     case BME280::ChipModel_BME280:
       Serial.println("Found BME280 sensor! Success.");
       break;
     case BME280::ChipModel_BMP280:
       Serial.println("Found BMP280 sensor! No Humidity available.");
       break;
     default:
       Serial.println("Found UNKNOWN sensor! Error!");
       setStatus(RED);
       return;
  }

  Serial.println("");
  Udp.begin(localUdpPort);
  Serial.print("UDP socket listening on port ");
  Serial.println(localUdpPort);

  broadcast = IPAddress( WiFi.localIP().v4() | ( ~ WiFi.subnetMask().v4() ));

  Serial.print("Broadcasting on ");
  Serial.print(broadcast);
  Serial.println("");

  discoverServer();
}

String envSensorData()
{
   float temp(NAN), hum(NAN), pres(NAN);

   BME280::TempUnit tempUnit(BME280::TempUnit_Celsius);
   BME280::PresUnit presUnit(BME280::PresUnit_Pa);

   bme.read(pres, temp, hum, tempUnit, presUnit);
   
   return "{\"temp\":" + String(temp) + ",\"humidity\":" + String(hum) + ",\"pressure\":" + String(pres) + "}";
}

String envSensorDataHTML()
{
   float temp(NAN), hum(NAN), pres(NAN);

   BME280::TempUnit tempUnit(BME280::TempUnit_Celsius);
   BME280::PresUnit presUnit(BME280::PresUnit_Pa);

   bme.read(pres, temp, hum, tempUnit, presUnit);

   String res = "<body><h1>Room data</h1><h2>Temp: " + String(temp) + " "+ String(tempUnit == BME280::TempUnit_Celsius ? 'C' :'F');
   res += "<h2>Humidity: " + String(hum) + " % RH</h2>";
   res += "<h2>Pressure: " + String(pres) + " Pa</h2>";
   res += "</body>";

   return res;
}

boolean remoteServerErrorShown = false;
boolean noWifiErrorShown = false;

void loop(void) {

  if(WiFi.status() != WL_CONNECTED  &&  !noWifiErrorShown) {
    setStatus(RED);
    doubleBeep();
    noWifiErrorShown = true;
  }
  
  int packetSize = Udp.parsePacket();
  if (packetSize) {
    int len = Udp.read(packetBuffer, 255);
    if (len > 0) {
      packetBuffer[len] = 0;
    }

    if(strcmp(packetBuffer, "R") == 0) {
      Serial.print("Found server at ");
      Serial.println(Udp.remoteIP());
      remoteServer = Udp.remoteIP();
      doubleBeep();
      setStatus(GREEN);
    }
  }
  
  server.handleClient();
  MDNS.update();
}
