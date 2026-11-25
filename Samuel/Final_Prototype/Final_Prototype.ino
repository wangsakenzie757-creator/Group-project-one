//---------------------THE CODE EXPLANATION-----------------------
//First we load all the libraries 
//Start the sensor - checks if the sensors are connected
//Connect to the WIFI and MQTT server - checks if they are connected, tries for 15 seconds if not
//Secure the connection and load the sensors
//Reads the data from the sensors - checks if they can be read
//Publishes the data - chceks if they were uploaded, if one can't be uploaded, none can
//It publishes if all went well or exactly what failed - if wifi or mqtt fails it doesn't write anything
//Goes to deep sleep


//-----------------------LIBRARIES-------------------------------
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <SensirionI2cSen66.h>


//-----------------------CONNECTION IDS---------------------------
const char* WIFI_SSID   = "#########";  //wifi name
const char* WIFI_PASS   = "#########";  //wifi password 
const char* MQTT_SERVER = "q6800e68.ala.eu-central-1.emqxsl.com"; //mqtt broker adress
const char* MQTT_USER   = "SamDodo";   //mqtt username
const char* MQTT_PASS   = "Samdodo1.";  //mqtt password 


//----------------------SECURED CONNECTION------------------------
WiFiClientSecure secureClient;  // encrypted line
PubSubClient client(secureClient);  // communication


//----------------------NAME OF THE SENSORS------------------------
Adafruit_BME280 bme;
SensirionI2cSen66 sen;



//----------------------FAILURE STORAGE----------------------------
String errors = "";                  // everything that went wrong in this cycle

//prints the failure and stores it, it does NOT go to sleep
void fail(const char* msg) {
  Serial.println(msg);
  if (errors.length() > 0) errors += "; ";
  errors += msg;
}


//----------------------DEEP SLEEP FACTOR--------------------------
#define uS_TO_S_FACTOR 1000000ULL    // microseconds factor
#define TIME_TO_SLEEP  120           // 2 minutes 
#define CONNECT_TIMEOUT 15000



//--------------------------FAILURE FUNCTION------------------------

String errors = "";        // everything that went wrong in this cycle

//prints the failure and stores it, it does NOT go to sleep
void fail(const char* msg) {
  Serial.println(msg);
  if (errors.length() > 0) errors += "; ";
  errors += msg;
}


//------------------------------DEEP SLEEP FUNCTION-------------------------
void goToSleep(){
  sen.stopMeasurement();
  if (client.connected()) client.disconnect();
  Serial.println("Going to sleep...");
  Serial.flush();            //ensures everything is send and done before deep sleep 
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  esp_deep_sleep_start();
}





//--------------------------------------MAIN CODE---------------------------------------

void setup() {

  Serial.begin(115200);
  Wire.begin(21, 22);  
  sen.begin(Wire, SEN66_I2C_ADDR_6B);
  delay(500);



  //-----------------------------------START BME280 and SEN66--------------------------------------
  
  bool bmeOk = bme.begin(0x76);
  if (!bmeOk) fail("BME280 not found, check wiring/address");  //sends it to the mqtt
  else Serial.println("BME connected!");

  sen.stopMeasurement();               // reset the sensor to a clean state
  delay(1000);

  bool senOk = (sen.startContinuousMeasurement() == 0);
  if (!senOk) fail("SEN66 not found, check wiring");
  else Serial.println("SEN connected!");



  //--------------------------------WIFI----------------------------------------
  //connecting to the wifi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  
  //connecting to the wifi for 15 seconds
  unsigned long start = millis();  //how long is esp running
  while (WiFi.status() != WL_CONNECTED && millis() - start < CONNECT_TIMEOUT) { //stopwatch trying to connect to the wifi for 15 seconds
    if (WiFi.status() == WL_CONNECTED) break;    //breaks the cycle if the wifi connects 
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi failed");     // no WiFi = no way to publish
    goToSleep();
  }
  Serial.println("WiFi connected!");



  //---------------------------------MQTT----------------------------------------
  
  //connecting to the mqtt broker
  secureClient.setInsecure();  // encrypt connection
  client.setServer(MQTT_SERVER, 8883); //stores the adress

  //connecting to the mqtt for 15 seconds
  start = millis();
  while (!client.connected() && millis() - start < CONNECT_TIMEOUT) {
    if (client.connect("esp32-airquality", MQTT_USER, MQTT_PASS)) break; //breaks the cycle if the mqtt connects
    Serial.print("MQTT retry, code=");
    Serial.println(client.state());
    delay(1000);
  }

  if (!client.connected()) {
    Serial.println("MQTT failed");     // no MQTT = no way to publish
    goToSleep();
  }
  Serial.println("MQTT connected!");



  //----------------------------------READING VALUES----------------------------

  //reading and checking values from bme
  //reading from the sensor 
  
  float temp = 0, hum = 0, press = 0;
  bool bmeValid = false;

  //checking if the esp can read the measurements
  if (bmeOk) {
    temp  = bme.readTemperature();
    hum   = bme.readHumidity();
    press = bme.readPressure() / 100.0F;

    bmeValid = !(isnan(temp) || isnan(hum) || isnan(press));
    if (!bmeValid) fail("Failed to read from BME280");
  }



  //reading and checking values from sen
  float pm1 = 0, pm25 = 0, pm4 = 0, pm10 = 0;
  float aaa, bbb, voc, nox;              // required by the function, not used
  uint16_t co2;
  bool senValid = false;


  //gives pm1, pm25 and pm10 their values
  //checks if it can read them, needs 10 seconds to reboot the sensor and get valuable data
  if (senOk) {
    delay(10000);                        // sensor warm-up
    client.loop();                       // keep the MQTT connection alive

    senValid = (sen.readMeasuredValues(pm1, pm25, pm4, pm10, aaa, bbb, voc, nox, co2) == 0 && pm25 < 6553.0);
    if (!pmValid) fail("SEN66 PM values not valid");
  }
 
   if (bmeValid) {
    Serial.printf("Temp: %.1f C\n", temp);
    Serial.printf("Humidity: %.1f %%\n", hum);
    Serial.printf("Pressure: %.1f hPa\n", press);
  }
  if (senValid) {
    Serial.printf("PM 1.0: %.1f ug/m3\n", pm1);
    Serial.printf("PM 2.5: %.1f ug/m3\n", pm25);
    Serial.printf("PM 10: %.1f ug/m3\n\n", pm10);
  }

  


  //--------------------------------PUBLISH MEASUREMENTS---------------------------------------

  //publishing the values to the mqtt, must be a text not a number 
  //if only one data is not sent the whole publish fails
  char payload[16];
  bool ok = true;
  

  if (senValid) {
    snprintf(payload, sizeof(payload), "%.1f", pm1);
    ok &= client.publish("esp32/sen66/pm1", payload);

    snprintf(payload, sizeof(payload), "%.1f", pm25);
    ok &= client.publish("esp32/sen66/pm25", payload);

    snprintf(payload, sizeof(payload), "%.1f", pm10);
    ok &= client.publish("esp32/sen66/pm10", payload);
  }
  

  if (bmeValid) {
    snprintf(payload, sizeof(payload), "%.1f", temp);
    ok &= client.publish("esp32/bme280/temperature", payload);

    snprintf(payload, sizeof(payload), "%.1f", hum);
    ok &= client.publish("esp32/bme280/humidity", payload);

    snprintf(payload, sizeof(payload), "%.1f", press);
    ok &= client.publish("esp32/bme280/pressure", payload);
  }

  //checks if the data were send
  if (!ok) fail("Publish failed");



  //--------------------------------PUBLISH FAILURES OR OK-----------------------------

  if (errors.length() > 0) {
    client.publish("esp32/status", errors.c_str());   // all failures in one message
  }
  else {
    client.publish("esp32/status", "OK");
  }

  client.loop();                         // let the messages upload
  delay(500);

  Serial.println(errors.length() > 0 ? "Finished with failures" : "Data sent");

  //enables sleep mode
  goToSleep();
}

void loop() {
  
}
