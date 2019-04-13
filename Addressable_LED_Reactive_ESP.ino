#include <Adafruit_NeoPixel.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPClient.h>
#include <WiFiUDP.h>
#include <ArduinoOTA.h>
 
#define BUTTON    14
#define N_PIXELS  20  // Number of pixels you are using
#define MIC_PIN   A0  // Microphone is attached to Trinket GPIO #2/Gemma D2 (A1)
#define LED_PIN   13  // NeoPixel LED strand is connected to GPIO #0 / D0
#define DC_OFFSET 0  // DC offset in mic signal - if unusure, leave 0
#define NOISE     235  // Noise/hum/interference in mic signal
#define SAMPLES   60  // Length of buffer for dynamic level adjustment
#define TOP       (N_PIXELS +1) // Allow dot to go slightly off scale
//#define POT_PIN    3  // if defined, a potentiometer is on GPIO #3 (A3, Trinket only) 
 
byte
  peak      = 0,      // Used for falling dot
  dotCount  = 0,      // Frame counter for delaying dot-falling speed
  volCount  = 0,      // Frame counter for storing past volume data
  red = 0, 
  green = 0, 
  blue = 0,
  selectableR = 255,
  selectableG = 0,
  selectableB = 0;
  
int
  vol[SAMPLES],       // Collection of prior volume samples
  lvl       = 10,     // Current "dampened" audio level
  minLvlAvg = 0,      // For dynamic adjustment of graph low & high
  maxLvlAvg = 256,
  btnCounter = 0,
  countRead = 0,
  ledColCount = 0,
  colCount = 0;

String page = "<html lang='en'>";

MDNSResponder mdns;
ESP8266WebServer server(80);
HTTPClient http;

WiFiUDP UDP;

struct led_command {
  uint8_t opmode;
  uint32_t data;
  uint8_t red;
  uint8_t green;
  uint8_t blue;
};

byte cmd[4];

void connect ()
{
  disconnect();
  Serial.printf("wake = %d\n", WiFi.forceSleepWake());
  WiFi.mode(WIFI_STA);
  WiFi.hostname("node_server");
  WiFi.begin("ap_name", "password");
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }
  
    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });
  
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println();
  Serial.print("MAC: ");
  Serial.println(WiFi.macAddress());
  
  if (mdns.begin("esp8266", WiFi.localIP())) {
    Serial.println("MDNS responder started");
  }

  server.on("/", [](){
    server.send ( 200, "text/html", page );
  });

  server.on("/initial", [](){
    server.send ( 200, "text/html", String(btnCounter) );
  });

  server.on("/+", [](){
    server.send( 200, "text/html", page );
    // Turn off LED    
    Serial.println("hit"); 
    if (btnCounter == 5) {
      btnCounter = 0;
    } else {
      btnCounter++;
    }
  });
  
  server.on("/next", [](){
    if (btnCounter == 5) {
      btnCounter = 0;
    } else {
      btnCounter++;
    }
    server.send( 200, "text/html", String(btnCounter) );
    Serial.println("App: Next effect"); 
  });

  server.on("/effect", [](){
    btnCounter = server.arg(0).toInt();
    Serial.println("App: Certain Effect"); 
  });

  server.on("/color", [](){
    selectableR = server.arg(0).toInt();
    selectableG = server.arg(1).toInt();
    selectableB = server.arg(2).toInt();
    Serial.println("App: Color Selected"); 
  });

  server.on("/getcolor", [](){
    server.send( 200, "text/html", String(selectableR) + "," + String(selectableG) + "," + String(selectableB));
    Serial.println("App: Color Retrieved"); 
  });

  server.begin();
  Serial.println("HTTP server started");

  UDP.begin(7171); 
}

void disconnect ()
{
  WiFi.disconnect(true);
  Serial.printf("sleep 1us = %d\n", WiFi.forceSleepBegin(1));
}

void setup() {
  #ifdef BUTTON
    pinMode(BUTTON, INPUT);
  #endif

  pinMode(LED_BUILTIN, OUTPUT);

  #ifdef LED_PIN
    pinMode(LED_PIN, OUTPUT);
  #endif

  Serial.begin(115200);

  WiFi.persistent(false);

  page += "<head>";
  page +=   "<meta http-equiv='refresh' content='60' name='viewport' content='width=device-width, initial-scale=1'/>";
  page +=   "<link rel='stylesheet' href='https://maxcdn.bootstrapcdn.com/bootstrap/3.3.7/css/bootstrap.min.css'/>";
  page +=   "<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.1.1/jquery.min.js'></script>";
  page +=   "<script src='https://maxcdn.bootstrapcdn.com/bootstrap/3.3.7/js/bootstrap.min.js'></script>";
  page +=   "<meta name='viewport' content='width=device-width, initial-scale=1'/>";
  page +=   "<title>Reactive LEDs</title>";
  page += "</head>";
  page += "<body>";
  page +=   "<div class='container-fluid'>";
  page +=     "<div class='row'>";
  page +=       "<div class='col-md-12'>";
  page +=         "<div class='jumbotron' style='text-align: center'>";
  page +=           "<h2>Reactive LEDs Server Running</h2>";
  page +=           "<br/>";
  page +=           "<p>";
  page +=             "<a class='btn btn-primary btn-large' href='+'>Next Effect</a>";
  page +=           "</p>";
  page +=         "</div>";
  page +=       "</div>";
  page +=     "</div>";
  page +=   "</div>";
  page += "</body>";
  page += "</html>";
  
  connect();
  
}

void loop() {
  int n;

  #ifdef BUTTON        
    if ((digitalRead(BUTTON) == 1) && (countRead == btnCounter)) {
      if (btnCounter == 5) {
        btnCounter = 0;
      } else {
        btnCounter++;
      }
    } 
    if (digitalRead(BUTTON) == 0) {
      countRead = btnCounter;
    }
  #endif
  
  server.handleClient();
  ArduinoOTA.handle();
  
  n   = analogRead(MIC_PIN);                 // Raw reading from mic 
  n   = abs(n - 512 - DC_OFFSET);            // Center on zero
  n   = (n <= NOISE) ? 0 : (n - NOISE);      // Remove noise/hum

//  int packetSize = UDP.parsePacket();
//  if (packetSize)
//  {
//    UDP.read((char *)&cmd, 1);
//    //lastReceived = millis();
//
//    String sVal = ""; 
//
//    for (int i = 0; i < 4; i++) {
//      sVal += cmd[i];
//    }
//  
//    n = sVal.toInt();    
//  }

  Serial.println("Sending sound: " + String(n));
  Serial.println("Sending mode: " + String(btnCounter));

  sendLedData(n, btnCounter);

  #ifdef LED_PIN
    if (btnCounter < 5) {
      digitalWrite(LED_PIN, HIGH); 
    } 
  #endif

  if (btnCounter == 5) {
    #ifdef LED_PIN
      digitalWrite(LED_PIN, LOW); 
    #endif
  }

  delay(4);
}

void sendLedData(uint32_t data, uint8_t op_mode) 
{
   struct led_command send_data;
   send_data.opmode = op_mode;
   send_data.data = data; 
   send_data.red = selectableR;
   send_data.green = selectableG;
   send_data.blue = selectableB;
   
  IPAddress ip(192,168,1,200);
  UDP.beginPacket(ip, 7001); 
  UDP.write((char*)&send_data,sizeof(struct led_command));
  UDP.endPacket();
}
