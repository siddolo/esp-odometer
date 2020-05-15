/*
 * ESP Odometer - 0.1a
 * Tested on ESP8266 WeMos D1 Mini
 * 2020 - Pasquale 'sid' Fiorillo
*/

#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>

#define FIRMWARE_VERSION "0.1a"

#define WIFI_ESSID "SV650"
#define WIFI_PASSWORD "12345678"
#define SERVER_PORT 80
#define TRIGGER_STEP 4 // how many reed point?
#define DEBUG 1
#define DEBUG_SERIAL_BAUDRATE 115200
#define SIMULATE 1

#define REED D2 // pin connected to read switch
#define EEPROM_SIZE 4
#define EEPROM_LOCATION 0x00

uint8_t step = 0;
unsigned long revolutions = 0; // (unit32_t)
boolean triggerFlag = false;
boolean risingFlag = false;
boolean fallingFlag = false;


const char response[] PROGMEM = R"rawliteral(
{
  "revolutions": %REVOLUTIONS%,
  "firmware": "%FIRMWARE%"
}
)rawliteral";


String response_processor(const String& var){
  if (var == "REVOLUTIONS"){
    return String(revolutions);
  } else if (var == "FIRMWARE"){
    return String(FIRMWARE_VERSION);
  }
  return String();
}


AsyncWebServer server(SERVER_PORT);


#if SIMULATE == 1
  /*
    Interrupt at freq of 1kHz
  */
  void ICACHE_RAM_ATTR ISR_TIMER(){
    timer1_write(600000);//12us
    risingFlag = true;
  }


  /*
    ESP8266 Timer and Ticker
  */
  void timerSetup() {
    timer1_attachInterrupt(ISR_TIMER);
    timer1_enable(TIM_DIV16, TIM_EDGE, TIM_SINGLE);
    timer1_write(600000);
  }
  
#else

  /*
    Interrupt at reed trigger
  */
  void ICACHE_RAM_ATTR ISR_REED() {
    risingFlag = true;
  }

#endif


void doFalling() {
  #if DEBUG == 2
    Serial.println("doFalling()");
  #endif
  
  digitalWrite(BUILTIN_LED, LOW);
  fallingFlag = false;
}


void doRising() {
  #if DEBUG == 2
    Serial.println("doRising()");
  #endif
  
  digitalWrite(BUILTIN_LED, HIGH);
  
  step++;
  
  if (step == TRIGGER_STEP) {
    step = 0;
    triggerFlag = true;
  }
  
  risingFlag = false;

  #if SIMULATE == 0
    do {
      delay(1);
    } while(digitalRead(REED) == HIGH);
  #endif
  
  doFalling();
}


boolean setupWifi() {
  return WiFi.softAP(WIFI_ESSID, WIFI_PASSWORD);
}


/* 
 * This function will return a 4 byte (32bit) long from the eeprom
 */
unsigned long eepromRead() {
  unsigned long four = EEPROM.read(EEPROM_LOCATION);
  unsigned long three = EEPROM.read(EEPROM_LOCATION + 1);
  unsigned long two = EEPROM.read(EEPROM_LOCATION + 2);
  unsigned long one = EEPROM.read(EEPROM_LOCATION + 3);
  
  //Return the recomposed long by using bitshift.
  return ((four << 0) & 0xFF) + ((three << 8) & 0xFFFF) + ((two << 16) & 0xFFFFFF) + ((one << 24) & 0xFFFFFFFF);
}


/* 
 * This function will write a 4 byte (32bit) long to the eeprom 
 */
void eepromWrite(unsigned long value) {
  byte four = (value & 0xFF);
  byte three = ((value >> 8) & 0xFF);
  byte two = ((value >> 16) & 0xFF);
  byte one = ((value >> 24) & 0xFF);
  
  //Write the 4 bytes into the eeprom memory.
  EEPROM.write(EEPROM_LOCATION, four);
  EEPROM.write(EEPROM_LOCATION + 1, three);
  EEPROM.write(EEPROM_LOCATION + 2, two);
  EEPROM.write(EEPROM_LOCATION + 3, one);
  EEPROM.commit();
}


void clearRevolutions() {
  eepromWrite(0);
  revolutions = eepromRead();
}


void setup() {
  #ifdef DEBUG
    Serial.begin(DEBUG_SERIAL_BAUDRATE);
  #endif

  #if SIMULATE == 1
    pinMode(REED, INPUT_PULLUP);
  #else
    pinMode(REED, INPUT);
  #endif
  

  pinMode(BUILTIN_LED, OUTPUT);

  // read last value from the eeprom
  EEPROM.begin(EEPROM_SIZE);
  revolutions = eepromRead();

  boolean wifi = setupWifi();
  IPAddress IP = WiFi.softAPIP();
  
  #ifdef DEBUG
  if (wifi == true) {
    Serial.println("\n\nWiFi Ready");
    Serial.print("AP IP address: ");
    Serial.println(IP);
  } else {
    Serial.println("\n\nWiFi Failed!");
  }
  #endif

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "application/json", response, response_processor);
  });

  server.on("/reset", HTTP_POST, [](AsyncWebServerRequest *request){
    clearRevolutions();
    request->send_P(200, "application/json", response, response_processor);
  });

  server.on("/set", HTTP_POST, [](AsyncWebServerRequest *request){
    int paramsNr = request->params();
    if(request->hasParam("revolutions", true)) {
      AsyncWebParameter* p = request->getParam("revolutions", true);
      revolutions = atol(p->value().c_str());
      eepromWrite(atol(p->value().c_str()));
      #if DEBUG == 2
        Serial.print("Set revolutions to: ");
        Serial.println(atol(p->value().c_str()));
      #endif
    }
    request->send_P(200, "application/json", response, response_processor);
  });

  server.begin();

  #if SIMULATE == 1
    timerSetup();
  #else
    attachInterrupt(digitalPinToInterrupt(REED), ISR_REED, RISING);
  #endif

  #ifdef DEBUG
    Serial.println("\nSetup done");
  #endif
}


void loop() {
  //float speed;
  
  if (risingFlag == true) {
    doRising();
  }
  
  
  // If no trigger, do nothing
  if (triggerFlag == false) {
    return;
  }
  
  #if DEBUG == 2
    Serial.println("triggerFlag = true");
  #endif
  
  revolutions++;
  eepromWrite(revolutions);

  #ifdef DEBUG
    Serial.print("Revolutions: ");
    Serial.println(revolutions);
  #endif

  triggerFlag = false; // Reset the trigger flag
}
