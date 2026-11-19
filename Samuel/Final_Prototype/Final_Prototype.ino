//---------------------THE CODE EXPLANATION-----------------------
//First we load all the libraries 
//Connect to the WIFI and MQTT server
//Secure the connection and load the sensors
//Define deep sleep function - the esp goes to sleep if it doesnt connect to the sensors or cant read the values

//Starts the sensors
//Connects to the WIFI and MQTT 
//waits 10 sesonds to start the sensors
//Reads the values
//Publishes the values 
//Goes to sleep

//After every connection and reading the code checks if it came through


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


//----------------------DEEP SLEEP FACTOR--------------------------
#define uS_TO_S_FACTOR 1000000ULL    // microseconds factor
#define TIME_TO_SLEEP  120           // 2 minutes 



//---------------------DEEP SLEEP FUNCTION-------------------------
void goToSleep(){
  sen.stopMeasurement();
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



  //-----------------------------------BME280--------------------------------------
  //check if esp can find bme
  if (!bme.begin(0x76)) {           
    Serial.println("BME280 not found, check wiring/address");
    goToSleep();
  }

  Serial.println("BME connected!");
  
  //-----------------------------------SEN66----------------------------------------

  //restets the sensor to clean state 
  sen.stopMeasurement();
  delay(1000);

  //chcek if esp can find sen
  if (sen.startContinuousMeasurement() != 0) {
    Serial.println("SEN66 not found, check wiring");
    goToSleep();
  }

  Serial.println("SEN connected!");

  //--------------------------------WIFI----------------------------------------
  //connecting to the wifi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  
  //connecting to the wifi for 15 seconds
  unsigned long start = millis();  //how long is esp running
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) { //stopwatch trying to connect to the wifi for 15 seconds
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nWiFi failed");
    goToSleep();
  }
  Serial.println("WiFi connected!");




  //---------------------------------MQTT----------------------------------------
  
  //connecting to the mqtt broker
  secureClient.setInsecure();  // encrypt connection
  client.setServer(MQTT_SERVER, 8883); //stores the adress

  //check the connection between esp and the broker 
  if (!client.connect("esp32-airquality", MQTT_USER, MQTT_PASS)) {
    Serial.print("MQTT failed, code=");
    Serial.println(client.state());
    goToSleep();
  }
  Serial.println("MQTT connected!");


  //------------------------------READING VALUES--------------------------

  //reading and checking values from bme
  //reading from the sensor 
  float temp  = bme.readTemperature();
  float hum   = bme.readHumidity();
  float press = bme.readPressure() / 100.0F;

  //checking if the esp can read the measurements
  if (isnan(temp) || isnan(hum) || isnan(press)) {
    Serial.println("Failed to read from BME280");
    goToSleep();
  }


  //reading and checking values from sen
  float pm1, pm25, pm4, pm10;
  float aaa, bbb, voc, nox;                 // required by the function, but not used
  uint16_t co2;

  //gives pm1, pm25 and pm10 their values
  //checks if it can read them, needs 10 seconds to reboot the sensor and get valuable data 
  delay(10000);                       // sensor warm-up 
  client.loop();                      // keep the MQTT connection alive

  bool notPm = (sen.readMeasuredValues(pm1, pm25, pm4, pm10, aaa, bbb, voc, nox, co2) == 0 && pm25 < 6553.0);


  Serial.printf("Temp: %.1f C\n", temp);
  Serial.printf("Humidity: %.1f %%\n", hum);
  Serial.printf("Pressure: %.1f hPa\n", press);

  if (notPm) {
    Serial.printf("PM 1.0: %.1f ug/m3\n", pm1);
    Serial.printf("PM 2.5: %.1f ug/m3\n", pm25);
    Serial.printf("PM 10: %.1f ug/m3\n\n", pm10);
  }
  else {
    Serial.println("SEN66 PM values not valid, sending BME280 data only");
  }

  


  //--------------------------------PUBLISH---------------------------------------

  //publishing the values to the mqtt, must be a text not a number 
  //if only one data is not sent the whole publish fails
  char payload[16];
  bool ok = true;
  

  snprintf(payload, sizeof(payload), "%.1f", pm1);
  ok &=client.publish("esp32/sen66/pm1", payload);

  snprintf(payload, sizeof(payload), "%.1f", pm25);
  ok &=client.publish("esp32/sen66/pm25", payload);

  snprintf(payload, sizeof(payload), "%.1f", pm10);
  ok &=client.publish("esp32/sen66/pm10", payload);
  

  snprintf(payload, sizeof(payload), "%.1f", temp);
  ok &=client.publish("esp32/bme280/temperature", payload);  //subsribed label

  snprintf(payload, sizeof(payload), "%.1f", hum);
  ok &=client.publish("esp32/bme280/humidity", payload);

  snprintf(payload, sizeof(payload), "%.1f", press);
  ok &=client.publish("esp32/bme280/pressure", payload);


  client.loop(); //let the message upload 
  delay(500);
  client.disconnect();

  //checks if the data were send
  if (ok) {
    Serial.println("Data sent");
  } 
  else {
    Serial.println("Publish failed");
  } 

  //enables sleep mode
  goToSleep();
}

void loop() {
  
}
