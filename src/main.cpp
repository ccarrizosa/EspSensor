/**
 * Homie-ADS115
 *
 * ADS1115 connected to an ESP-12F which transmits via MQTT the raw
 * data of each one of the channels.
 */

/*
 * Includes:
 */
#include <Homie.h>
#include <Adafruit_ADS1015.h>

#include "Arduino.h"

/*
 * Defines and Constants:
 */

#ifdef DBG
  #define PRINT(x)  Serial.println (x)
#else
  #define PRINT(x)
#endif

/**
 * How many time (uSec.) the ESP is going to be in deep sleep mode previously
 * to send data again.
 */
static const uint32_t LOOP_TIME = 300000000u;

/** If set to HIGH, the ESP configuration is reset. */
static const uint8_t RESET_PIN = 14u;

/** While the ADC measurement is ongoing this GPIO is set to high */
static const uint8_t MEASUREMENT_ONGOING_PIN = 12u;

/** ADC instance */
Adafruit_ADS1115 ads(0x48);

/** MQTT node where ADC data is going to be published. */
HomieNode adcNode("adc", "adc");

/** Transmission done flag */
bool sentOnce = false;

/*
 * Functions declaration:
 */

/** Arduino Framework setup function */
void setup();

/** Arduino Framework loop function */
void loop();

/** Homie Events callback function */
void onHomieEvent(const HomieEvent& event);

/** Setup function called once WiFi and MQTT has been connected */
void setupHandler();

/** Loop function called once WiFi and MQTT has been connected */
void loopHandler();

/*
 * Functions definition:
 */

void setupHandler() {
  ads.begin();
  pinMode(MEASUREMENT_ONGOING_PIN, OUTPUT);
}

void loopHandler(){
  if(!sentOnce) {
    digitalWrite(MEASUREMENT_ONGOING_PIN, HIGH);
    for(uint8_t channel=0; channel<4; channel++){
      String value = String(ads.readADC_SingleEnded(channel));
      PRINT("Channel " + String(channel) + " value = " + value);
      adcNode.setProperty("channel_"+String(channel)).send(value);
    }
    digitalWrite(MEASUREMENT_ONGOING_PIN, LOW);
    sentOnce = true;
  }  
}

void setup() {
  #ifdef DBG
    Serial.begin(115200);
  #else
    Homie.disableLogging();
  #endif

  Homie_setFirmware("EspSensor", "1.0.1");

  pinMode(RESET_PIN, INPUT);
  Homie.setResetTrigger(RESET_PIN, HIGH, 2000);
  Homie.disableResetTrigger();
  Homie.disableLedFeedback();
  Homie.onEvent(onHomieEvent);
  Homie.setSetupFunction(setupHandler).setLoopFunction(loopHandler);

  adcNode.advertise("channel_0");
  adcNode.advertise("channel_1");
  adcNode.advertise("channel_2");
  adcNode.advertise("channel_3");
  Homie.setup();
}

void onHomieEvent(const HomieEvent& event) {
  switch(event.type) {
    case HomieEventType::WIFI_DISCONNECTED:
    case HomieEventType::MQTT_DISCONNECTED: {
      static uint8_t retries = 5;
      if(!retries--){
        PRINT("Could not connect to WiFi or MQTT server");
        PRINT("Going to sleep in order to save power.");
        ESP.deepSleep(LOOP_TIME);
      }
      break;
    }
    case HomieEventType::MQTT_CONNECTED: {
      PRINT("MQTT connected, preparing for deep sleep...");
      sentOnce = false;
      Homie.prepareToSleep();
      break;
    }
    case HomieEventType::READY_TO_SLEEP: {
      Serial.println("Ready for sleep");
      ESP.deepSleep(LOOP_TIME);
      break;
    }
  }
}

void loop() {
  Homie.loop();
}
