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
#include <time.h>
#include <Arduino_JSON.h>
#include <base64.h>
#include <mbedtls/md.h>
#include <NetworkUdp.h>
#include "client.h"
#include "protocol.h"

NetworkUDP udp;

//-----------------------CONNECTION IDS---------------------------
const char* WIFI_SSID   = "......";  //wifi name
const char* WIFI_PASS   = ".....";  //wifi password 
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
#define TIME_TO_SLEEP_ERR  60        // 1 minute
#define CONNECT_TIMEOUT 30000



//--------------------------FAILURE FUNCTION------------------------

String errors = "";        // everything that went wrong in this cycle

//prints the failure and stores it, it does NOT go to sleep
void fail(const char* msg) {
  Serial.println(msg);
  if (errors.length() > 0) errors += "; ";
  errors += msg;
}


//------------------------------DEEP SLEEP FUNCTION-------------------------
unsigned long woken = 0;

void goToSleep(bool fastRetry) {
  sen.stopMeasurement();
  if (client.connected()) client.disconnect();
  if (WiFi.isConnected()) WiFi.disconnect();
  Serial.println("Going to sleep...");
  Serial.flush();            //ensures everything is send and done before deep sleep 
  uint64_t to_sleep_for = TIME_TO_SLEEP * uS_TO_S_FACTOR;
  if (fastRetry) {
    to_sleep_for = TIME_TO_SLEEP_ERR * uS_TO_S_FACTOR;
  }
  unsigned long duty_on = micros() - woken;
  if (duty_on >= to_sleep_for) {
    esp_restart();
  }
  esp_sleep_enable_timer_wakeup(to_sleep_for - duty_on);
  esp_deep_sleep_start();
}





//--------------------------------------MAIN CODE---------------------------------------

void setup() {
  woken = micros();
  Serial.begin(115200);
  Wire.begin(21, 22);
  delay(500);



  //-----------------------------------START BME280 and SEN66--------------------------------------
  
  bool bmeOk = bme.begin(0x76);

  sen.begin(Wire, SEN66_I2C_ADDR_6B);
  bool senOk = sen.deviceReset() == 0; // reset the sensor to a clean state

  // senOk &= sen.startFanCleaning() == 0;
  // if (senOk) delay(12000);

  senOk &= sen.startContinuousMeasurement() == 0;
  unsigned long senWaitUntil = millis() + 40000;

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
    goToSleep(true);
  }
  Serial.println("WiFi connected!");

  // Do a Roughtime time synchronization
  uint64_t rough_time = 0;
  uint64_t rough_time_our = 0;

  udp.begin(WiFi.localIP(), 12345);
  for (int j = 0; j < 5; j++) {
    uint8_t nonce[roughtime::kNonceLength];
    esp_fill_random(nonce, sizeof(nonce));
    const std::string request = roughtime::CreateRequest(nonce);
    udp.beginPacket("162.159.200.123", 2003); // Cloudflare Roughtime
    udp.write((const uint8_t*) request.data(), request.size());
    udp.endPacket();

    uint8_t recv_buf[roughtime::kMinRequestSize];

    for (int i = 0; i < 500; i++) {
      if (udp.parsePacket() > 0) {
        break;
      }
      delay(1);
    }
    if (udp.available() == 0) continue;
    int buf_len = udp.read(recv_buf, roughtime::kMinRequestSize);
    roughtime::rough_time_t timestamp;
    uint32_t radius;
    std::string error;
    unsigned char public_key[32] = {
      0xd0, 0x60, 0xfb, 0x73, 0x7c, 0x8f, 0xf3, 0x11, 0x1c, 0xe1, 0x99, 0x76, 0xcd, 0xeb, 0x8d, 0xd9, 
      0x29, 0x4b, 0xbc, 0x35, 0x55, 0xa1, 0xc8, 0xec, 0x3d, 0x22, 0xfc, 0xfd, 0x19, 0x7f, 0xef, 0x38
    }; // Cloudflare Roughtime
    if (roughtime::ParseResponse(
            &timestamp, &radius, &error,
            public_key, recv_buf,
            buf_len, nonce)) {
      rough_time = timestamp;
      rough_time_our = micros();
      break;
    }
    fail(error.c_str());
  }

  //---------------------------------MQTT----------------------------------------
  
  //connecting to the mqtt broker
  secureClient.useBuiltinCACertBundle();  // encrypt connection
  client.setServer(MQTT_SERVER, 8883); //stores the adress
  client.setBufferSize(2048);
  //client.setKeepAlive(60);   //keeps the connection alive for 60

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
    goToSleep(true);
  }
  Serial.println("MQTT connected!");



  //----------------------------------READING VALUES----------------------------

  //Checking the connection with the sensors
  if (!bmeOk) fail("BME280 not found, check wiring/address");  //sends it to the mqtt
  else Serial.println("BME connected!");

  if (!senOk) fail("SEN66 not found, check wiring");
  else Serial.println("SEN connected!");


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
  float humSen, tempSen, voc, nox;              // required by the function, not used
  uint16_t co2;
  bool senValid = false;


  //gives pm1, pm25 and pm10 their values
  //checks if it can read them, needs 10 seconds to reboot the sensor and get valuable data
  if (senOk) {
    while (senWaitUntil > millis()) {
      client.loop();                       // keep the MQTT connection alive
      Serial.print(".");
      delay(1000);
    }
    Serial.println();

    SEN66DeviceStatus status;

    senValid = (sen.readMeasuredValues(pm1, pm25, pm4, pm10, humSen, tempSen, voc, nox, co2) == 0 && pm25 < 6553.0);
    senValid &= sen.readDeviceStatus(status) == 0;
    senValid &= status.value == 0;
    if (!senValid) fail("SEN66 PM values not valid");
  }
 
   if (bmeValid) {
    Serial.println("===BME280 Data===");
    Serial.printf("Temp: %.1f C\n", temp);
    Serial.printf("Humidity: %.1f %%\n", hum);
    Serial.printf("Pressure: %.1f hPa\n", press);
  }
  if (senValid) {
    Serial.println("===SEN66 Data===");
    Serial.printf("PM 1.0: %.1f ug/m3\n", pm1);
    Serial.printf("PM 2.5: %.1f ug/m3\n", pm25);
    Serial.printf("PM 10: %.1f ug/m3\n", pm10);
    Serial.printf("Temp: %.1f C\n", tempSen);
    Serial.printf("Humidity: %.1f %%\n", humSen);
  }

  


  //--------------------------------PUBLISH MEASUREMENTS---------------------------------------

  //publishing the values to the mqtt, must be a text not a number 
  //if only one data is not sent the whole publish fails
  JSONVar innerPayload;
  
  innerPayload["timestamp"] = (unsigned long) ((rough_time + (micros() - rough_time_our)) / 1000000); // roughtime

  if (senValid) {
    innerPayload["pm1"] = pm1;
    innerPayload["pm25"] = pm25;
    innerPayload["pm10"] = pm10;
  }
  

  if (bmeValid) {
    innerPayload["temperature"] = temp;
    innerPayload["humidity"] = hum;
    innerPayload["pressure"] = press;
  }


  if (errors.length() > 0) {
    innerPayload["status"] = errors;
  }
  else {
    innerPayload["status"] = "OK";
  }

  String innerPayloadString = JSON.stringify(innerPayload);

  unsigned char k[32] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
  };
  unsigned char a[32];
  if (mbedtls_md_hmac(
    mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
    k, 32,
    (const unsigned char*) innerPayloadString.c_str(), innerPayloadString.length(),
    a
  )) fail("Failed to authenticate message");

  JSONVar outerPayload;

  outerPayload["data"] = innerPayloadString;
  outerPayload["auth"] = base64::encode(a, 32);

  String outerPayloadString = JSON.stringify(outerPayload);

  Serial.println(outerPayloadString);

  //checks if the data were send
  if (!client.publish("esp32/authenticated_data", outerPayloadString.c_str())) fail("Publish failed");

  client.loop();                         // let the messages upload
  delay(500);

  if (errors.length() > 0) {
  Serial.print("Failures: ");
  Serial.println(errors);
  }
  else {
    Serial.println("Everything OK, data sent");
  }

  //enables sleep mode
  goToSleep(errors.length() > 0);
}

void loop() {
  
}
