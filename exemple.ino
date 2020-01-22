#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>

extern "C" {
#include <user_interface.h>
}

ADC_MODE(ADC_VCC);

#include <Wire.h>
#include "SparkFunBME280.h"
BME280 bme;

#define TIMEOUT_WIFI_CONNECTION 15
#define SLEEP_TIME              30

#define DATA_SENT           0
#define WIFI_NOT_CONFIGURED 1
#define DATA_NOT_SEND       2
#define SENSOR_NOT_READ     3
#define LOW_POWER           4
#define WIFI_CONFIGURATION  5
#define WIFI_CONFIGURATION_ERROR 6
#define WIFI_CONFIGURATION_SUCCESS 7

#define RED_GPIO           14
#define GREEN_GPIO         13
#define BLUE_GPIO          12

// rtcData stores volatile data to increase wifi connection speed.
// Each time the µC is powered off, this structure is lost.
struct {
  uint32_t crc32;
  uint8_t ap_channel;
  uint8_t ap_bssid[6];
  uint8_t ap_ssid[32];
  uint8_t ap_passphrase[64];
  uint32_t sleep_time;
  IPAddress ip;
  IPAddress gw;
  IPAddress msk;
  IPAddress dns;
} rtcData;

// eepromData stores wifi information data.
struct {
  uint32_t crc32;
  uint32_t ap_ssid[32];
  uint8_t ap_passphrase[64];
  uint32_t sleep_time;
} eepromData;

// sensorValues is a structure to save last values read.
struct {
  float humidity;
  float temperature;
  float pressure;
  float battery;
} sensorValues;


uint32_t time_no_client;

uint32_t sleep_time;


void ledSetup() {
  pinMode(RED_GPIO, OUTPUT);
  pinMode(GREEN_GPIO, OUTPUT);
  pinMode(BLUE_GPIO, OUTPUT);

  digitalWrite(RED_GPIO, HIGH);
  digitalWrite(GREEN_GPIO, HIGH);
  digitalWrite(BLUE_GPIO, HIGH);
}

void ledBlink(uint8_t message) {
  Serial.println(message);
  switch (message) {
    case (uint8_t) DATA_SENT:
      digitalWrite(RED_GPIO, HIGH);
      digitalWrite(GREEN_GPIO, LOW);
      digitalWrite(BLUE_GPIO, HIGH);
      break;

    case (uint8_t) WIFI_NOT_CONFIGURED:
      Serial.println("WiFi not configured");
      digitalWrite(BLUE_GPIO, HIGH);
      digitalWrite(GREEN_GPIO, HIGH);

      for (int i = 0 ; i < 5 ; i++) {
        Serial.println(i);
        digitalWrite(RED_GPIO, LOW);
        delay(300);
        digitalWrite(RED_GPIO, HIGH);
        delay(300);
      }
      break;

    case (uint8_t) WIFI_CONFIGURATION:
      Serial.println("WiFi configuration");
      digitalWrite(RED_GPIO, HIGH);
      digitalWrite(GREEN_GPIO, HIGH);
      digitalWrite(BLUE_GPIO, LOW);
      break;

    case (uint8_t) WIFI_CONFIGURATION_ERROR:
      Serial.println("WiFi configuration error");
      digitalWrite(RED_GPIO, LOW);
      digitalWrite(GREEN_GPIO, HIGH);
      digitalWrite(BLUE_GPIO, HIGH);
      delay(500);
      break;

    case (uint8_t) WIFI_CONFIGURATION_SUCCESS:
      Serial.println("WiFi configuration success");
      digitalWrite(RED_GPIO, HIGH);
      digitalWrite(GREEN_GPIO, LOW);
      digitalWrite(BLUE_GPIO, HIGH);
      delay(1000);
      break;
  }

}





uint32_t CRC32(const uint8_t *data, size_t size) {
  uint32_t crc = 0xFFFFFFFF;

  while (size--) {
    uint8_t c = *data++;

    for (uint32_t i = 0x80 ; i > 0 ; i >>= 1) {
      bool bit = crc & 0x80000000;

      if (c & i) {
        bit = !bit;
      }

      crc <<= 1;
      if (bit) {
        crc ^= 0x04c11db7;
      }
    }
  }

  return crc;
}




bool fireMeasures() {
  Wire.begin();
  bme.setI2CAddress(0x76);

  if (bme.beginI2C() == false) {
    Serial.println("Sensor not found");
    return false;
  }

  sensorValues.humidity = bme.readFloatHumidity();
  sensorValues.pressure = bme.readFloatPressure();
  sensorValues.temperature = bme.readTempC();

  return true;
}




bool sendGETRequest(String url) {
  Serial.println(url);
  HTTPClient httpClient;
  httpClient.begin(url);
  httpClient.addHeader("Host", "10.3.141.1:8121");
  int httpCode = httpClient.GET();
  Serial.println(String(httpCode));
  Serial.println(httpClient.getString());
  httpClient.end();
  if (httpCode == 200) {
    return true;
  }

  return false;
}



ESP8266WebServer server(80);

void handleRoot() {
  server.send(200, "text/html", "<form action=\"/update\" method=\"POST\"><input type=\"text\" name=\"ssid\" placeholder=\"SSID\"><br /><input type=\"text\" name=\"password\" placeholder=\"Password\"><br /><input type=\"text\" name=\"sleep\" placeholder=\"Sleep Time(s)\"><br /><input type=\"submit\" value=\"Update\"></form>");
}

void handleUpdate() {
  if (!server.hasArg("ssid") || !server.hasArg("password") || !server.hasArg("sleep") || server.arg("ssid") == NULL || server.arg("password") == NULL || server.arg("sleep") == NULL) {
    server.send(400, "text/plain", "400: Invalid Request");
    ledBlink(WIFI_CONFIGURATION_ERROR);
    ledBlink(WIFI_CONFIGURATION);
    return;
  }

  ledBlink(WIFI_CONFIGURATION_SUCCESS);

  String sta_ssid = server.arg("ssid");
  String sta_password = server.arg("password");
  String sleep = server.arg("sleep");

  memcpy(eepromData.ap_ssid, sta_ssid.c_str(), 32);
  memcpy(eepromData.ap_passphrase, sta_password.c_str(), 64);
  eepromData.sleep_time = sleep.toFloat();

  // Storing EEPROM data...
  eepromData.crc32 = CRC32((uint8_t*) &eepromData + 4, sizeof(eepromData) - 4);
  EEPROM.begin(512);
  EEPROM.put(0, eepromData);
  EEPROM.commit();

  // We have to devalidate rtcData
  rtcData.crc32 = 0x00000000;
  ESP.rtcUserMemoryWrite(0, (uint32_t*) &rtcData, sizeof(rtcData));

  server.send(200, "text/plain", "Configuration updated. Reboot will be done in few seconds");

  delay(1000);

  EEPROM.end();

  WiFi.disconnect(true);
  Serial.println("Going to sleep");
  ESP.deepSleepInstant(5 * 1e6, WAKE_RF_DISABLED);
}

void handleNotFound() {
  ledBlink(WIFI_CONFIGURATION_ERROR);
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
  ledBlink(WIFI_CONFIGURATION);
}


void setup() {
  // put your setup code here, to run once:
  Serial.begin(74880);
  delay(1);
  Serial.println();
  Serial.println(ESP.getFlashChipId(), HEX);
  Serial.println(ESP.getFlashChipRealSize());
  Serial.println(ESP.getFlashChipSize());
  Serial.println(ESP.getFlashChipSpeed());
  Serial.println(ESP.getFlashChipMode());

  sleep_time = SLEEP_TIME;

  ledSetup();
  // Important: to preserve battery, we force stop WiFi at startup.
  Serial.println("To preserve battery, we force stop WiFi at startup.");
  WiFi.mode(WIFI_OFF);
  WiFi.forceSleepBegin();

  // Displaying the reset information
  Serial.println("ResetInfo: " + ESP.getResetReason());
  delay(1);

  // We store reset reason in RTC Memory

  // Now we start the sequence to start WiFi, according to the reset information
  switch (ESP.getResetInfoPtr()->reason) {
    case REASON_EXT_SYS_RST:
      // This is a restart on demand. In this way, we will configure WiFi
      Serial.println("Configuration mode: allow user to configure WiFi settings");
      ledBlink(WIFI_CONFIGURATION);

      // We have to create a server.
      server.on("/", handleRoot);
      server.on("/update", handleUpdate);
      server.onNotFound(handleNotFound);


      // Starting WiFi Access Point.
      WiFi.persistent(false);
      WiFi.mode(WIFI_AP);
      delay(1);
      WiFi.softAP("esp8266", "12345678");
      Serial.print("AP IP address: ");
      Serial.println(WiFi.softAPIP());
      delay(500);
      server.begin();
      Serial.println("HTTP server started");

      break;

    //case REASON_DEEP_SLEEP_AWAKE:
    default:
      // This is the "normal" mode when the module ended sleep period.
      // In this way, we will get sensor measures, connect to the WiFi
      // and send data to the server. After that, sensor will go back
      // to sleep.
      Serial.println("Normal mode: getting sensor measures to send it to server.");

      bool rtcIsValid = false;

      if (ESP.rtcUserMemoryRead(0, (uint32_t*)&rtcData, sizeof(rtcData))) {
        // Able to read data from rtc Memory.
        uint32_t crc = CRC32((uint8_t*) &rtcData + 4, sizeof(rtcData) - 4);

        if (crc == rtcData.crc32) {
          // rtcData is valid.
          rtcIsValid = true;
          sleep_time = rtcData.sleep_time;
        }
      }

      // We start measurement.
      if (fireMeasures()) {
        // If measures are OK, we start WiFi.
        Serial.println("Measures done! Starting WiFi.");
        WiFi.persistent(false);
        delay(1);

        WiFi.mode(WIFI_STA);
        delay(1);

        if (rtcIsValid) {
          Serial.println("RTC data are valid. Quick connect!");
          WiFi.config(rtcData.ip, rtcData.gw, rtcData.msk);
          WiFi.begin((const char*) rtcData.ap_ssid, (const char*) rtcData.ap_passphrase, rtcData.ap_channel, rtcData.ap_bssid, true);
        } else {
          // If RTC is not valid, we get connection Data from EEPROM
          Serial.println("RTC is not valid, we get connection Data from EEPROM");

          EEPROM.begin(512);
          EEPROM.get(0, eepromData);

          WiFi.begin((const char*) eepromData.ap_ssid, (const char*) eepromData.ap_passphrase);
        }

        // System has maximum 15 seconds to connect to WiFi.
        // After this delay, it will goes automatically in sleep mode...
        while (WiFi.status() != WL_CONNECTED) {
          if (millis() > TIMEOUT_WIFI_CONNECTION * 1e3) {
            // Stopping WiFi.
            WiFi.disconnect(true);

            // Blink 5 times red led to alert about the issue (WiFi must be reconfigured).
            ledBlink(WIFI_NOT_CONFIGURED);

            Serial.println("Error: WIFI is not configured");
            // Go to sleep...
            ESP.deepSleepInstant(sleep_time * 1e6, WAKE_RF_DISABLED);
          }

          delay(1);
        }

        // If we are here, it means data can be sent.
        // @TODO
        Serial.println("Temperature: " + String(sensorValues.temperature));
        Serial.println("Humidity: " + String(sensorValues.humidity));
        Serial.println("Pressure: " + String(sensorValues.pressure / 100.0));
        Serial.println("Battery: " + String(ESP.getVcc() / 1000.0));


        sendGETRequest("http://10.3.141.1:8121/device=Sensor&taskid=" + String(ESP.getChipId(), HEX) + "&cmd=temperature&value=" + String(sensorValues.temperature));
        sendGETRequest("http://10.3.141.1:8121/device=Sensor&taskid=" + String(ESP.getChipId(), HEX) + "&cmd=humidity&value=" + String(sensorValues.humidity));
        sendGETRequest("http://10.3.141.1:8121/device=Sensor&taskid=" + String(ESP.getChipId(), HEX) + "&cmd=pressure&value=" + String(sensorValues.pressure / 100.0));
        sendGETRequest("http://10.3.141.1:8121/device=Sensor&taskid=" + String(ESP.getChipId(), HEX) + "&cmd=battery&value=" + String(ESP.getVcc() / 1000.0));
        sendGETRequest("http://10.3.141.1:8121/device=Sensor&taskid=" + String(ESP.getChipId(), HEX) + "&cmd=time&value=" + String(millis() / 1000.0));

        ledBlink(DATA_SENT);

        // Before the end, we have to save data if rtc memory data was invalid...
        if (!rtcIsValid) {
          memcpy(rtcData.ap_ssid, eepromData.ap_ssid, 32);
          memcpy(rtcData.ap_passphrase, eepromData.ap_passphrase, 64);
          memcpy(rtcData.ap_bssid, WiFi.BSSID(), 6);
          rtcData.ap_channel = WiFi.channel();
          rtcData.sleep_time = eepromData.sleep_time;
          rtcData.ip = WiFi.localIP();
          rtcData.gw = WiFi.gatewayIP();
          rtcData.msk = WiFi.subnetMask();
          rtcData.dns = WiFi.dnsIP();

          rtcData.crc32 = CRC32((uint8_t*) &rtcData + 4, sizeof(rtcData) - 4);

          ESP.rtcUserMemoryWrite(0, (uint32_t*) &rtcData, sizeof(rtcData));
        }
      }

      // And now, we can stop WiFi and go to sleep.
      WiFi.disconnect(true);

      Serial.println("Going to sleep");

      ESP.deepSleepInstant(sleep_time * 1e6, WAKE_RF_DISABLED);

      break;
  }
}

void loop() {
  // put your main code here, to run repeatedly:
  // If a client is not connected for 2 minutes, the chip returns in deep sleep mode.
  if (WiFi.softAPgetStationNum() > 0) {
    time_no_client = millis();
  }

  if (millis() - time_no_client > 120000) {
    // Go sleep mode
    //server.end();
    WiFi.disconnect(true);
    ESP.deepSleepInstant(sleep_time * 1e6, WAKE_RF_DISABLED);
  }

  server.handleClient();
}
