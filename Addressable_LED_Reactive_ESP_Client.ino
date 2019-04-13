#include <Adafruit_NeoPixel.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WiFiUDP.h>
#include <ArduinoOTA.h>

#define N_PIXELS  20  // Number of pixels you are using
#define LED_PIN   13  // NeoPixel LED strand is connected to GPIO #0 / D0
#define SAMPLES   60
#define TOP       (N_PIXELS +1)

Adafruit_NeoPixel strip = Adafruit_NeoPixel(N_PIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);
MDNSResponder mdns;
ESP8266WebServer server(80);
WiFiUDP UDP;

struct led_command {
  uint8_t opmode;
  uint32_t data;
  uint8_t red;
  uint8_t green;
  uint8_t blue;
};

struct led_command cmd;

String page = "<html lang='en'>";

byte
  volCount    = 0,
  peak        = 0,
  red         = 0, 
  green       = 0, 
  blue        = 0,
  selectableR = 0,
  selectableG = 255,
  selectableB = 0;
  
int
  vol[SAMPLES],
  lvl         = 10,
  minLvlAvg   = 0,      // For dynamic adjustment of graph low & high
  maxLvlAvg   = 256,
  soundVal    = 0,
  opMode      = 0,
  ledColCount = 0,
  colCount    = 0;

void setup() {
  memset(vol,0,sizeof(int)*SAMPLES);
  strip.begin();
  
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
  page +=           "<h2>Reactive LEDs Client 1 Running</h2>";
  page +=         "</div>";
  page +=       "</div>";
  page +=     "</div>";
  page +=   "</div>";
  page += "</body>";
  page += "</html>";

  connect();
}

void loop() {
  server.handleClient();

  if (opMode == 0) {
    int packetSize = UDP.parsePacket();
    if (packetSize)
    {
      UDP.read((char *)&cmd, sizeof(struct led_command));
      //lastReceived = millis();
    }

    soundVal = cmd.data;
    opMode = cmd.opmode;
    selectableR = cmd.red;
    selectableG = cmd.green;
    selectableB = cmd.blue;
    Serial.println("Val received: " + String(soundVal));
    Serial.println("Mode received: " + String(opMode));
  
    soundReactive();

    ArduinoOTA.handle();
  } else if (opMode == 1) {
    strip.show();
    rainbowCycle(20);

  } else if (opMode == 2) {
    strip.show();
    CylonBounce(1, 30, 20); 
  } else if (opMode == 3) {
    strip.show();
    meteorRain(4, 64, true, 60);
  } else if (opMode == 4) {
    strip.show();
    setAll();
  } else if (opMode == 5) {
    strip.show();
    setAll(0,0,0);
  }
}

void connect ()
{
  disconnect();
  Serial.printf("wake = %d\n", WiFi.forceSleepWake());
  WiFi.mode(WIFI_STA);
  WiFi.hostname("node_client_1");
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
  
  if (mdns.begin("esp8266_client", WiFi.localIP())) {
    Serial.println("MDNS responder started");
  }

  server.on("/", [](){
    server.send ( 200, "text/html", page );
    Serial.println("App: Server hit");
  });

  server.on("/soundval", [](){
    soundVal = server.arg(0).toInt();
    Serial.println("Server: Sound Received: " + String(soundVal));
  });

  server.begin();
  Serial.println("HTTP server started");

  UDP.begin(7001);
}

void disconnect ()
{
  WiFi.disconnect(true);
  Serial.printf("sleep 1us = %d\n", WiFi.forceSleepBegin(1));
}

void soundReactive() {
  uint8_t  i;
  uint16_t minLvl, maxLvl;
  int      height;
  
  lvl = ((lvl * 7) + soundVal) >> 3;    // "Dampened" reading (else looks twitchy)
  
  // Calculate bar height based on dynamic min/max levels (fixed point):
  height = TOP * (lvl - minLvlAvg) / (long)(maxLvlAvg - minLvlAvg);
 
  if (height < 0L) {
    height = 0;      // Clip output
  } else if (height > TOP) {
    height = TOP;
  }
  if (height > peak) {
    peak   = height; // Keep 'peak' dot at top
  }
 
  // if POT_PIN is defined, we have a potentiometer on GPIO #3 on a Trinket 
  //    (Gemma doesn't have this pin)
    uint8_t bright = 255;   
  #ifdef POT_PIN            
     bright = analogRead(POT_PIN);  // Read pin (0-255) (adjust potentiometer 
                                    //   to give 0 to Vcc volts
  #endif
  strip.setBrightness(bright);    // Set LED brightness (if POT_PIN at top
                                  //  define commented out, will be full)
  // Color pixels based on rainbow gradient
  for(i=0; i<N_PIXELS; i++) {
    server.handleClient();
    if(i >= height) {          
       strip.setPixelColor(i,   0,   0, 0);
    } else { 
       strip.setPixelColor(i,Wheel(map(i,0,strip.numPixels()-1,30,150)));
    }
  } 
 
  strip.show(); // Update strip
 
  vol[volCount] = soundVal; // Save sample for dynamic leveling
  if (++volCount >= SAMPLES) { 
    volCount = 0; // Advance/rollover sample counter
  } 
 
  // Get volume range of prior frames
  minLvl = maxLvl = vol[0];
  for (i=1; i<SAMPLES; i++) {
    server.handleClient();
    if(vol[i] < minLvl) { 
      minLvl = vol[i]; 
    } else if (vol[i] > maxLvl) { 
      maxLvl = vol[i];
    }
  }
  // minLvl and maxLvl indicate the volume range over prior frames, used
  // for vertically scaling the output graph (so it looks interesting
  // regardless of volume level).  If they're too close together though
  // (e.g. at very low volume levels) the graph becomes super coarse
  // and 'jumpy'...so keep some minimum distance between them (this
  // also lets the graph go to zero when no sound is playing):
  if((maxLvl - minLvl) < TOP) maxLvl = minLvl + TOP;
  minLvlAvg = (minLvlAvg * 63 + minLvl) >> 6; // Dampen min/max levels
  maxLvlAvg = (maxLvlAvg * 63 + maxLvl) >> 6; // (fake rolling average)
}

void rainbowCycle(int SpeedDelay) {
  byte *c;
  uint16_t i, j;

  for(j=0; j<256*5; j++) { // 5 cycles of all colors on wheel
    for(i=0; i< N_PIXELS; i++) {
      c=Wheel1(((i * 256 / N_PIXELS) + j) & 255);
      setPixel(i, *c, *(c+1), *(c+2));
    
      int packetSize = UDP.parsePacket();
      if (packetSize)
      {
        UDP.read((char *)&cmd, sizeof(struct led_command));
        //lastReceived = millis();
        opMode = cmd.opmode;
        selectableR = cmd.red;
        selectableG = cmd.green;
        selectableB = cmd.blue;
        //Serial.println("Val received: " + String(soundVal));
        Serial.println("Mode received: " + String(opMode));
      }
      
      uint8_t bright = 255;   
      #ifdef POT_PIN            
         bright = analogRead(POT_PIN);  // Read pin (0-255) (adjust potentiometer 
                                        //   to give 0 to Vcc volts
      #endif
      strip.setBrightness(bright);    // Set LED brightness (if POT_PIN at top
                                    //  define commented out, will be full)
      if (opMode != 1) {
        break;
      }
    }
    showStrip();
    delay(SpeedDelay);
    server.handleClient();
    if (opMode != 1) {
      break;
    }
  }
}

void CylonBounce(int EyeSize, int SpeedDelay, int ReturnDelay){
  for(int i = 0; i < N_PIXELS-EyeSize-2; i++) {
    if (colCount < 255) {
      colCount++;
      if (ledColCount == 0) {
        red++;
        green++;
        blue++;
      } else if (ledColCount == 1) {
        red++;
        green++;
        blue++;
      } else {
        red++;
        green++;
        blue++;
      }
    } else {
      colCount = 0;
      if (ledColCount < 2) {
        ledColCount++;
      } else {
        ledColCount = 0;
      }
    }

    int packetSize = UDP.parsePacket();
    if (packetSize)
    {
      UDP.read((char *)&cmd, sizeof(struct led_command));
      //lastReceived = millis();
      opMode = cmd.opmode;
      selectableR = cmd.red;
      selectableG = cmd.green;
      selectableB = cmd.blue;
      //Serial.println("Val received: " + String(soundVal));
      Serial.println("Mode received: " + String(opMode));
    }
    
    uint8_t bright = 255;   
    #ifdef POT_PIN            
       bright = analogRead(POT_PIN);  // Read pin (0-255) (adjust potentiometer 
                                      //   to give 0 to Vcc volts
    #endif
    strip.setBrightness(bright);    // Set LED brightness (if POT_PIN at top
                                    //  define commented out, will be full)
    if (opMode != 2) {
      break;
    }
    setAll(0,0,0);
    
    for(int j = 1; j <= EyeSize; j++) {
      strip.setPixelColor(i, Wheel((ledColCount * 1 + colCount) & 255));
      strip.setPixelColor(i+j, Wheel((ledColCount * 1 + colCount) & 255));
      strip.setPixelColor(i+EyeSize+1, Wheel((ledColCount * 1 + colCount) & 255));
      if (opMode != 2) {
        break;
      }
    }
    
    showStrip();
    delay(SpeedDelay);
  }

  delay(ReturnDelay);

  for(int i = N_PIXELS-EyeSize-2; i > 0; i--) {
    if (colCount < 255) {
      colCount++;
      if (ledColCount == 0) {
        red++;
        green++;
        blue++;
      } else if (ledColCount == 1) {
        red++;
        green++;
        blue++;
      } else {
        red++;
        green++;
        blue++;
      }
    } else {
      colCount = 0;
      if (ledColCount < 2) {
        ledColCount++;
      } else {
        ledColCount = 0;
      }
    }
    int packetSize = UDP.parsePacket();
    if (packetSize)
    {
      UDP.read((char *)&cmd, sizeof(struct led_command));
      //lastReceived = millis();
    }
    
    opMode = cmd.opmode;
    selectableR = cmd.red;
    selectableG = cmd.green;
    selectableB = cmd.blue;
    //Serial.println("Val received: " + String(soundVal));
    Serial.println("Mode received: " + String(opMode));
    if (opMode != 2) {
      break;
    }
    server.handleClient();
    setAll(0,0,0);
    
    for(int j = 1; j <= EyeSize; j++) {
      strip.setPixelColor(i, Wheel((ledColCount * 1 + colCount) & 255));
      strip.setPixelColor(i+j, Wheel((ledColCount * 1 + colCount) & 255));
      strip.setPixelColor(i+EyeSize+1, Wheel((ledColCount * 1 + colCount) & 255));
      if (opMode != 2) {
        break;
      }
    }
    
    showStrip();
    delay(SpeedDelay);
  }
 
  delay(ReturnDelay);
}

void meteorRain(byte meteorSize, byte meteorTrailDecay, boolean meteorRandomDecay, int SpeedDelay) {  
  setAll(0,0,0);
  
  for(int i = 0; i < N_PIXELS+N_PIXELS; i++) {
    // fade brightness all LEDs one step
    for(int j=0; j<N_PIXELS; j++) {
      if( (!meteorRandomDecay) || (random(10)>5) ) {
        fadeToBlack(j, meteorTrailDecay ); 

        int packetSize = UDP.parsePacket();
        if (packetSize)
        {
          UDP.read((char *)&cmd, sizeof(struct led_command));
          //lastReceived = millis();
          opMode = cmd.opmode;
          selectableR = cmd.red;
          selectableG = cmd.green;
          selectableB = cmd.blue;
          //Serial.println("Val received: " + String(soundVal));
          Serial.println("Mode received: " + String(opMode));
        }
        
        if (opMode != 3) {
          break;
        }
      }
    }

    if (opMode != 3) {
      break;
    }
    server.handleClient();
    
    // draw meteor
    for(int j = 0; j < meteorSize; j++) {
      if( ( i-j <N_PIXELS) && (i-j>=0) ) {
        setPixel(i-j, selectableR, selectableG, selectableB);
        int packetSize = UDP.parsePacket();
        if (packetSize)
        {
          UDP.read((char *)&cmd, sizeof(struct led_command));
          //lastReceived = millis();
          opMode = cmd.opmode;
          //Serial.println("Val received: " + String(soundVal));
          Serial.println("Mode received: " + String(opMode));
        } 
      
        if (opMode != 3) {
          break;
        }
        server.handleClient();
      } 
    }
    showStrip();
    delay(SpeedDelay);
  }
}

void fadeToBlack(int ledNo, byte fadeValue) {

 #ifdef ADAFRUIT_NEOPIXEL_H
    // NeoPixel
    uint32_t oldColor;
    uint8_t r, g, b;
    int value;

    oldColor = strip.getPixelColor(ledNo);
    
    r = (oldColor & 0x00ff0000UL) >> 16;
    g = (oldColor & 0x0000ff00UL) >> 8;
    b = (oldColor & 0x000000ffUL);

    r=(r<=10)? 0 : (int) r-(r*fadeValue/256);
    g=(g<=10)? 0 : (int) g-(g*fadeValue/256);
    b=(b<=10)? 0 : (int) b-(b*fadeValue/256);
    
    strip.setPixelColor(ledNo, r,g,b);
 #endif

 #ifndef ADAFRUIT_NEOPIXEL_H
   // FastLED
   leds[ledNo].fadeToBlackBy( fadeValue );
 #endif  
}

uint32_t Wheel(byte WheelPos) {
  if(WheelPos < 85) {
   return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
  } else if(WheelPos < 170) {
   WheelPos -= 85;
   return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  } else {
   WheelPos -= 170;
   return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
}

void setPixel(int Pixel, byte red, byte green, byte blue) {
 #ifdef ADAFRUIT_NEOPIXEL_H
   // NeoPixel
   strip.setPixelColor(Pixel, strip.Color(red, green, blue));
 #endif
 #ifndef ADAFRUIT_NEOPIXEL_H
   // FastLED
   leds[Pixel].r = red;
   leds[Pixel].g = green;
   leds[Pixel].b = blue;
 #endif
}

void setAll(byte red, byte green, byte blue) {
  for(int i = 0; i < N_PIXELS; i++ ) {
    setPixel(i, red, green, blue);
    int packetSize = UDP.parsePacket();
    if (packetSize)
    {
      UDP.read((char *)&cmd, sizeof(struct led_command));
      //lastReceived = millis();
      opMode = cmd.opmode;
      selectableR = cmd.red;
      selectableG = cmd.green;
      selectableB = cmd.blue;
      //Serial.println("Val received: " + String(soundVal));
      Serial.println("Mode received: " + String(opMode));
    }
  
    if (opMode != 5) {
      break;
    }
  }
  showStrip();
}

void setAll() {
  for(int i = 0; i < N_PIXELS; i++ ) {
    setPixel(i, selectableR, selectableG, selectableB);
    int packetSize = UDP.parsePacket();
    if (packetSize)
    {
      UDP.read((char *)&cmd, sizeof(struct led_command));
      //lastReceived = millis();
      opMode = cmd.opmode;
      selectableR = cmd.red;
      selectableG = cmd.green;
      selectableB = cmd.blue;
      //Serial.println("Val received: " + String(soundVal));
      Serial.println("Mode received: " + String(opMode));
    }
  
    if (opMode != 4) {
      break;
    }
  }
  showStrip();
}

uint32_t Wheel2(byte WheelPos) {
  if(WheelPos < 85) {
   return strip.Color(WheelPos * 3 / 10, 255 - WheelPos * 3 / 10, 0);
  } else if(WheelPos < 170) {
   WheelPos -= 85;
   return strip.Color(255 - WheelPos * 3 / 10, 0, WheelPos * 3 / 10);
  } else {
   WheelPos -= 170;
   return strip.Color(0, WheelPos * 3 / 10, 255 - WheelPos * 3 / 10);
  }
}

byte * Wheel1(byte WheelPos) {
  static byte c[3];
 
  if(WheelPos < 85) {
   c[0]=WheelPos * 3;
   c[1]=255 - WheelPos * 3;
   c[2]=0;
  } else if(WheelPos < 170) {
   WheelPos -= 85;
   c[0]=255 - WheelPos * 3;
   c[1]=0;
   c[2]=WheelPos * 3;
  } else {
   WheelPos -= 170;
   c[0]=0;
   c[1]=WheelPos * 3;
   c[2]=255 - WheelPos * 3;
  }

  return c;
}

void showStrip() {
 #ifdef ADAFRUIT_NEOPIXEL_H
   // NeoPixel
   strip.show();
 #endif
 #ifndef ADAFRUIT_NEOPIXEL_H
   // FastLED
   FastLED.show();
 #endif
}
