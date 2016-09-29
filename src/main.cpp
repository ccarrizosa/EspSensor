/**
 * ESP-12F based sensor board.
 *
 * Just an ADS1115 connected to an ESP-12F which transmits via MQTT the raw
 * data of each one of the channels.
 */

/*
 * Includes:
 */
#include <FS.h>

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <Adafruit_ADS1015.h>

#include "Arduino.h"

/*
 * Defines and Constants:
 */

#ifdef DBG
  #define DBG_PRINT(x)  Serial.println (x)
#else
  #define DBG_PRINT(x)
#endif


/**
 * How many time (uSec.) the ESP is going to be in deep sleep mode previously
 * to send data again.
 */
static const uint32_t LOOP_TIME = 300000000;

/**
 * How many time (uSec.) wait after an error
 */
static const uint32_t RETRY_TIME = LOOP_TIME / 5;


/*
 * Local vars:
 */

/**
 * MQTT default values.
 * If there are different values in config.json, they are overwritten.
 */
char mqtt_server[20];
char mqtt_user[20];
char mqtt_password[20];
char mqtt_port[6]= "1883";
char mqtt_topic[20];

/** flag for saving data */
bool shouldSaveConfig = false;

/** client to send MQTT data */
WiFiClient espClient;
PubSubClient client(espClient);


/*
 * Functions declaration:
 */

/** callback notifying us of the need to save config */
void saveConfigCallback ();

/** mqtt callback function */
void mqtt_callback(char* topic, byte* payload, unsigned int length);

/** Try to enter deep sleep mode. If not, try to reset ESP. */
void go_to_sleep(uint32_t sleep_time);

/** Send ADS data via MQTT */
void send_ads_data();

/** Arduino Framework setup function */
void setup();

/** Arduino Framework loop function */
void loop();

/*
 * Functions definition:
 */

void saveConfigCallback () {
  DBG_PRINT("Should save config");
  shouldSaveConfig = true;
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {

}

void setup() {
  #ifdef DBG
    Serial.begin(115200);
  #endif

  DBG_PRINT("\n ESP ID: " + String(ESP.getChipId()));

  //read configuration from FS json
  DBG_PRINT("mounting FS...");

  if (SPIFFS.begin()) {
    DBG_PRINT("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      DBG_PRINT("reading config file...");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        DBG_PRINT("Stored Config:");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());

        #ifdef DBG
          json.prettyPrintTo(Serial);
        #endif

        if (json.success()) {
          DBG_PRINT("\nParsed json");

          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_user, json["mqtt_user"]);
          strcpy(mqtt_password, json["mqtt_password"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(mqtt_topic, json["mqtt_topic"]);

        } else {
          DBG_PRINT("ERROR: Failed to load json config");
        }
      }
    }
  } else {
    DBG_PRINT("failed to mount FS");
    go_to_sleep(RETRY_TIME);
  }
  //end read

  //WiFiManager
  WiFiManager wifiManager;

  #ifndef DBG
    /* Disable debug output */
    wifiManager.setDebugOutput(false);
  #endif

  //wifiManager.resetSettings();

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  // MQTT Parameters:
  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 20);
  WiFiManagerParameter custom_mqtt_user("user", "mqtt user", mqtt_user, 20);
  WiFiManagerParameter custom_mqtt_password("password", "mqtt password", mqtt_password, 20);
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 6);
  WiFiManagerParameter custom_mqtt_topic("topic", "mqtt topic", mqtt_topic, 20);

  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_user);
  wifiManager.addParameter(&custom_mqtt_password);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_topic);

  //sets timeout until configuration portal gets turned off
  wifiManager.setConfigPortalTimeout(300);

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect("AutoConnectAP", "password")) {
    //Failed to connect. Go to deep sleep
    DBG_PRINT("failed to connect and hit timeout");
    go_to_sleep(RETRY_TIME);
  }

  //if you get here you have connected to the WiFi
  DBG_PRINT("connected...yeey :)");

  //read updated parameters
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_user, custom_mqtt_user.getValue());
  strcpy(mqtt_password, custom_mqtt_password.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_topic, custom_mqtt_topic.getValue());

  DBG_PRINT("MQTT Server: " + String(mqtt_server));
  DBG_PRINT("MQTT User: " + String(mqtt_user));
  DBG_PRINT("MQTT Password: " + String(mqtt_password));
  DBG_PRINT("MQTT Port: " + String(mqtt_port));
  DBG_PRINT("MQTT Topic: " + String(mqtt_topic));

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    DBG_PRINT("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = mqtt_server;
    json["mqtt_user"] = mqtt_user;
    json["mqtt_password"] = mqtt_password;
    json["mqtt_port"] = mqtt_port;
    json["mqtt_topic"] = mqtt_topic;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      DBG_PRINT("failed to open config file for writing");
      go_to_sleep(RETRY_TIME);
    }
    json.printTo(configFile);
    configFile.close();
    //end save
  }

  DBG_PRINT("ESP ip: ");
  DBG_PRINT(WiFi.localIP());

  // Setup MQTT client
  delay(1000);
  client.setServer(mqtt_server, String(mqtt_port).toInt());
  client.setCallback(mqtt_callback);

}

void go_to_sleep(uint32_t sleep_time){
  DBG_PRINT("Going to sleep for " + String(sleep_time) + " uSec.");
  ESP.deepSleep(sleep_time);
  delay(1000);
  ESP.reset();
}

void reconnect() {
  uint8_t retries = 5;
  // Loop until we're reconnected
  while (!client.connected() && retries--) {
    DBG_PRINT("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP8266Client-";
    clientId += String(ESP.getChipId());
    // Attempt to connect
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_password)) {
      DBG_PRINT("connected!");
    } else {
      DBG_PRINT("failed, rc=" + String(client.state()));
      DBG_PRINT(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void send_ads_data(void){
  Adafruit_ADS1115 ads(0x48);
  int16_t adc_values[4];
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();

  ads.begin();

  for(uint8_t channel=0; channel<4; channel++){
    adc_values[channel] = ads.readADC_SingleEnded(channel);

    root["channel_" + String(channel)] = (String)adc_values[channel];
    DBG_PRINT("Channel " + String(channel) + " value = " +\
                   String(adc_values[channel]));
  }

  #ifdef DBG
    root.prettyPrintTo(Serial);
    DBG_PRINT("");
  #endif

  reconnect();
  if(!client.connected()){
    DBG_PRINT("Not possible to connect to MQTT server.");
    go_to_sleep(RETRY_TIME);
  }

  client.loop();

  char data[200];
  root.printTo(data, root.measureLength() + 1);
  client.publish(mqtt_topic, data, true);
  DBG_PRINT("Transmission done!");

  delay(1000);

}

/**
 * Main loop
 */
void loop() {
  send_ads_data();
  go_to_sleep(LOOP_TIME);
}
