#include "FS.h"
#include "Secrets.h"     // contains cert files info
#include <ESP8266WiFi.h> // Include the Wi-Fi library
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <MQTTClient.h>
#include <WiFiManager.h>
#include <rtc_memory.h>      

#define DEBUG 0 // Switch debug output on and off by 1 or 0

#define FORMAT_SPIFFS_IF_FAILED true

#if DEBUG
#define PRINTS(s)       \
  {                     \
    Serial.print(F(s)); \
  }
#define PRINT(s, v)     \
  {                     \
    Serial.print(F(s)); \
    Serial.print(v);    \
  }

  
#else
#define PRINTS(s)
#define PRINT(s, v)
#endif


const byte LED_PIN = 2;
const byte SENSOR_ACTIVATE_PIN = 5; //Turns on NPN transistor and Capactive Soil Sensor
const byte SOIL_MOISTURE_PIN = 0;

const uint64_t LONG_SLEEP_TIME = 3.6e+9; //60 minutes
const uint64_t SHORT_SLEEP_TIME = 3e+7; //30 seconds

const float AREF_VOLTAGE = 1000.0;       //milivtols - Regulator is connected to Aref

struct tm timeinfo;

typedef struct {
    int counter;
} MyData;

WiFiClientSecure net;
MQTTClient client = MQTTClient(512);
WiFiManager wm;
RtcMemory rtcMemory;


void setup()
{

  #if DEBUG
    Serial.begin(115200);
  #endif

  delay(3000);

  pinMode(SENSOR_ACTIVATE_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
 

  
  PRINTS("Booting up Sensor\n");

  
  if(rtcMemory.begin()){
    PRINTS("Initialization done!");
  } else {
   PRINTS("No previous data found. The memory is reset to zeros!");
  }
  
  // Get the data
  MyData* data = rtcMemory.getData<MyData>();

  if( data->counter == 0 || data->counter > 7 ){

   //Indicate things are on and working
  //Yes, it's opposite thean what it would normally be
  digitalWrite(LED_PIN, LOW);
  digitalWrite(SENSOR_ACTIVATE_PIN, HIGH);
  connectWiFi();
  connectAWS();
  delay(2000); //this delay is necesary or else adc will be off
  publishStatus();
  PRINTS("Going to Sleep");
  net.stop();
  wm.disconnect();
  data->counter = 0;
  data->counter++;
  rtcMemory.save();
  ESP.deepSleep(LONG_SLEEP_TIME, WAKE_RF_DISABLED);
  delay(100);
    
  }else{
   data->counter++;
  Serial.println(String("Value to persist: ") + data->counter);
  rtcMemory.save();
  if( data->counter == 8){
      ESP.deepSleep(LONG_SLEEP_TIME,WAKE_RF_DEFAULT);
  }else{
      ESP.deepSleep(LONG_SLEEP_TIME,WAKE_RF_DISABLED);
  }
 
  delay(100);

}
  
 
}


void connectWiFi(){
  WiFi.mode(WIFI_STA); 
  wm.setCleanConnect(true); // disconnect before connect, clean connect
  wm.setConfigPortalTimeout(60); //timesout after 120 seconds
  wm.setConnectTimeout(60);

  bool wifiResult;
  wifiResult = wm.autoConnect(); // password protected ap
    if(!wifiResult) {
        PRINTS("Failed to connect\n");
         ESP.deepSleep(SHORT_SLEEP_TIME,WAKE_RF_DEFAULT);
    } 
    else {
          
  PRINTS("\nConnection Established\n");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov"); 
  
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2)
  {
    delay(500);
    now = time(nullptr);
  }
 
  gmtime_r(&now, &timeinfo);
  PRINT("Current time: ", asctime(&timeinfo));
  PRINTS("\n");
    
    
  }

  


}

void connectAWS()
{

  X509List cert(AWS_CERT_CA);

  X509List publicCert(AWS_CERT_CRT);

  PrivateKey key(AWS_CERT_PRIVATE);

  // Configure WiFiClientSecure to use the AWS IoT device credentials
  net.setTrustAnchors(&cert);
  net.setClientRSACert(&publicCert, &key);

  // Connect to the MQTT broker on the AWS endpoint we defined earlier
  client.begin(AWS_IOT_ENDPOINT, 8883, net);

  PRINTS("Connecting to AWS IOT\n");

  while (!client.connect(DEVICE_NAME))
  {
    PRINTS(".");
    PRINT("",client.lastError());
    delay(10000);
  }

  if (!client.connected())
  {
    PRINTS("AWS IoT Timeout!\n");
    return;
  }

  PRINTS("AWS IoT Connected!");
}

void publishStatus()
{

  StaticJsonDocument<200> jsonDoc;
  JsonObject stateObj = jsonDoc.createNestedObject("state");
  JsonObject reportedObj = stateObj.createNestedObject("reported");
  reportedObj["moisture"] = getSoilReading();
  reportedObj["id"] = ESP.getChipId();
  reportedObj["operation"] = ESP.getResetReason();
  char jsonBuffer[512];
  serializeJson(jsonDoc, jsonBuffer);
  client.publish(AWS_IOT_PUBLISH_TOPIC, jsonBuffer);
  PRINTS("Sent Shadow Update\n");


}

//Samples ADC readings from a specific pin and returns the averag
//////////////////////////////////////////////
float sampleReadings(byte pin, byte samplesize)
{
  int totalReadings = 0;
  for (byte i = 0; i < samplesize; i++)
  {
    totalReadings = totalReadings + analogRead(pin);
    delay(10); //just pause a bit between readings;
  }
  float counts = totalReadings / (float)samplesize;
  return counts;
}

//Returns the  output Voltage of the capacitive soil sensor
/////////////////////////////////////
uint16_t getSoilReading()
{
  float counts = sampleReadings(SOIL_MOISTURE_PIN, 25);
  PRINT("\nSoil Sensor Counts is: ", counts);
  uint16_t voltage = ((counts / 1023.0) * AREF_VOLTAGE);
  PRINT("\nSoil Sensor voltage is: ", voltage);
  PRINTS("\n");
  return voltage;
}



void loop()
{
  
}
