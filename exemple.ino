#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266HTTPClient.h>


extern "C" {
#include <user_interface.h>
}

ADC_MODE(ADC_VCC);

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
Adafruit_BME280 bme;

#define FW_VERSION    "0.1.0"

#define TIMEOUT_WIFI_CONNECTION 15
#define DEFAULT_SLEEP_TIME      30

#define DATA_SENT           0
#define WIFI_NOT_CONFIGURED 1
#define DATA_NOT_SEND       2
#define SENSOR_NOT_READ     3
#define LOW_POWER           4
#define WIFI_CONFIGURATION  5
#define WIFI_CONFIGURATION_ERROR 6
#define WIFI_CONFIGURATION_SUCCESS 7
#define RTC_UPDATED         8
#define OFF                 9

#define RED_GPIO           12
#define GREEN_GPIO         13
#define BLUE_GPIO          14

// rtcData stores volatile data to increase wifi connection speed.
// Each time the µC is powered off, this structure is lost.
struct {
  uint32_t crc32;
  uint32_t count;
  float temp;
  float hum;
  float pres;
  float volt;
  uint8_t ap_channel;
  uint8_t ap_bssid[6];
  uint8_t ap_ssid[32];
  uint8_t ap_passphrase[64];
  uint32_t sleep_time;
  IPAddress ip;
  IPAddress gw;
  IPAddress msk;
  uint32_t altitude;
} rtcData;

// eepromData stores wifi information data.
struct {
  uint32_t crc32;
  uint32_t ap_ssid[32];
  uint8_t ap_passphrase[64];
  uint32_t sleep_time;
  float altitude;
} eepromData;

// sensorValues is a structure to save last values read.
struct {
  float humidity;
  float temperature;
  float pressure;
  float seaLevelPressure;
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
  //Serial.println(message);
  switch (message) {
    case (uint8_t) OFF:
      digitalWrite(RED_GPIO, HIGH);
      digitalWrite(GREEN_GPIO, HIGH);
      digitalWrite(BLUE_GPIO, HIGH);
      break;

    case (uint8_t) DATA_SENT:
      digitalWrite(RED_GPIO, HIGH);
      digitalWrite(GREEN_GPIO, LOW);
      digitalWrite(BLUE_GPIO, HIGH);
      break;

    case (uint8_t) WIFI_NOT_CONFIGURED:
      //Serial.println("WiFi not configured");
      digitalWrite(BLUE_GPIO, HIGH);
      digitalWrite(GREEN_GPIO, HIGH);

      for (int i = 0 ; i < 5 ; i++) {
        //Serial.println(i);
        digitalWrite(RED_GPIO, LOW);
        delay(300);
        digitalWrite(RED_GPIO, HIGH);
        delay(300);
      }
      break;

    case (uint8_t) WIFI_CONFIGURATION:
      //Serial.println("WiFi configuration");
      digitalWrite(RED_GPIO, HIGH);
      digitalWrite(GREEN_GPIO, HIGH);
      digitalWrite(BLUE_GPIO, LOW);
      break;

    case (uint8_t) WIFI_CONFIGURATION_ERROR:
      //Serial.println("WiFi configuration error");
      digitalWrite(RED_GPIO, LOW);
      digitalWrite(GREEN_GPIO, HIGH);
      digitalWrite(BLUE_GPIO, HIGH);
      delay(500);
      break;

    case (uint8_t) WIFI_CONFIGURATION_SUCCESS:
      //Serial.println("WiFi configuration success");
      digitalWrite(RED_GPIO, HIGH);
      digitalWrite(GREEN_GPIO, LOW);
      digitalWrite(BLUE_GPIO, HIGH);
      delay(1000);
      break;

    case (uint8_t) RTC_UPDATED:
      //Serial.println("RTC updated");
      digitalWrite(BLUE_GPIO, HIGH);
      digitalWrite(RED_GPIO, HIGH);

      for (int i = 0 ; i < 5 ; i++) {
        //Serial.println(i);
        digitalWrite(GREEN_GPIO, LOW);
        delay(300);
        digitalWrite(GREEN_GPIO, HIGH);
        delay(300);
      }
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
  Wire.setClock(400000);

  if (bme.begin(0x76) == false) {
    //Serial.println("Sensor not found");
    return false;
  }

  bme.setSampling(Adafruit_BME280::MODE_FORCED,
                  Adafruit_BME280::SAMPLING_X1, // temperature
                  Adafruit_BME280::SAMPLING_X1, // pressure
                  Adafruit_BME280::SAMPLING_X1, // humidity
                  Adafruit_BME280::FILTER_OFF   );

  sensorValues.humidity = bme.readHumidity();
  sensorValues.pressure = bme.readPressure() / 100.0F;
  sensorValues.temperature = bme.readTemperature();
  sensorValues.seaLevelPressure = bme.seaLevelForAltitude(230, bme.readPressure()) / 100.0F;


  return true;
}




bool sendGETRequest(String url) {
  //Serial.println(url);
  HTTPClient httpClient;
  httpClient.begin(url);
  httpClient.addHeader("Host", "10.3.141.1:8121");
  int httpCode = httpClient.GET();
  //Serial.println(String(httpCode));
  //Serial.println(httpClient.getString());
  httpClient.end();
  if (httpCode == 200) {
    return true;
  }

  return false;
}



ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;

void handleCss() {
  server.send(200, "text/css", "@import url(https://fonts.googleapis.com/css?family=Montserrat:400,800);*{box-sizing:border-box}body{font-family:'Montserrat',sans-serif;background:#f6f5f7;display:flex;flex-direction:column;justify-content:center;align-items:center;height:100vh;margin:-20px 0 50px}h1{font-weight:700;margin:0}p{font-size:14px;font-weight:100;line-height:20px;letter-spacing:.5px;margin:20px 0 30px}span{font-size:12px}a{color:#333;font-size:14px;text-decoration:none;margin:15px 0}.container{background:#fff;border-radius:10px;box-shadow:0 14px 28px rgba(0,0,0,.25),0 10px 10px rgba(0,0,0,.22);position:relative;overflow:hidden;width:768px;max-width:100%;min-height:420px;min-width:50%}.form-container{background:#fff;display:flex;flex-direction:column;padding:0 50px;height:100%;justify-content:center;align-items:center;text-align:center;position:absolute;top:0;transition:0.6s ease-in-out}.social-container-top{margin:20px 0;margin-bottom:370px;position:absolute}.social-container-bottom{margin:20px 0;margin-top:400px;position:absolute;font-size:.8em}.social-container-bottom a{font-size:.8em}.social-container a.social{font-size:.5em;border:1px solid #ddd;border-radius:50%;display:inline-flex;justify-content:center;align-items:center;margin:0 5px;height:40px;width:40px;background-color:#fff}.form-container input{background:#eee;border:none;padding:12px 15px;margin:8px 0;width:100%}.form-container button{margin-top:10px}button{border-radius:20px;border:1px solid #ffaa23;background:#ffaa23;color:#fff;font-size:12px;font-weight:700;padding:12px 45px;letter-spacing:1px;text-transform:uppercase;transition:transform 80ms ease-in}button:active{transform:scale(.95)}button:focus{outline:none}button.over-btn{background:transparent;border-color:#fff}.social:hover{color:#ffaa23;border-color:#ffaa23}.configure-container{left:0;width:50%;z-index:2}.update-container{left:0;width:50%;z-index:1;opacity:0}.update-container button{background:#ff782b;border-color:#ff782b}.update-container .social:hover{color:#ff782b;border-color:#ff782b}.configure-container button{background:#ffa705;border-color:#ffa705}.configure-container .forgot{font-size:12px;display:flex;flex-direction:column;margin-bottom:0}.overlay-container{position:absolute;top:0;left:50%;width:50%;height:100%;overflow:hidden;transition:transform 0.6s ease-in-out;z-index:100}.overlay{background:#ffaa23 linear-gradient(to right,#ed2a31,#ffaa23) no-repeat 0 0 / cover;color:#fff;position:relative;left:-100%;height:100%;width:200%;transform:translateX(0);transition:transform 0.6s ease-in-out}.overlay-panel{position:absolute;top:0;display:flex;flex-direction:column;justify-content:center;align-items:center;padding:0 40px;height:100%;width:50%;text-align:center;transform:translateX(0);transition:transform 0.6s ease-in-out}.overlay-right{right:0;transform:translateX(0)}.overlay-left{transform:translateX(-20%)}.container.right-panel-active .configure-container{transform:translateX(100%)}.container.right-panel-active .overlay-container{transform:translateX(-100%)}.container.right-panel-active .update-container{transform:translateX(100%);opacity:1;z-index:5}.container.right-panel-active .overlay{transform:translateX(50%)}.container.right-panel-active .overlay-left{transform:translateX(0)}.container.right-panel-active .overlay-right{transform:translateX(20%)}");
}

void handleJavascript() {
  server.send(200, "text/javascript", "const signUpbtn=document.getElementById(\"configure\"),signInbtn=document.getElementById(\"update\"),container=document.getElementById(\"container\");signUpbtn.addEventListener(\"click\",()=>{container.classList.add(\"right-panel-active\")}),signInbtn.addEventListener(\"click\",()=>{container.classList.remove(\"right-panel-active\")});");
}

void handleRoot() {
  fireMeasures();

  server.send(200, "text/html", "<!DOCTYPE html><html lang=\"en\"><head> <meta charset=\"UTF-8\"/> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"/> <meta http-equiv=\"X-UA-Compatible\" content=\"ie-edge\"/> <title>ESP Sensor Web Server</title> <link rel=\"stylesheet\" href=\"https://cdnjs.cloudflare.com/ajax/libs/font-awesome/4.7.0/css/font-awesome.min.css\"> <link rel=\"stylesheet\" href=\"https://use.fontawesome.com/releases/v5.8.1/css/all.css\"> <link rel=\"stylesheet\" type=\"text/css\" href=\"/style.css\"></head><body> <div class=\"container\" id=\"container\"> <div class=\"form-container update-container\"> <form action=\"/firmware\" method=\"POST\" enctype=\"multipart/form-data\"> <h1>Update</h1> <span>the Firmware of your ESP Sensor</span> <input type=\"file\" accept=\".bin\" placeholder=\"Firmware\" name=\"firmware\"/> <button>Update</button> </form><div class=\"social-container social-container-top\"> Current version: " + (String)FW_VERSION + " </div><div class=\"social-container social-container-bottom\"> Check a new FW Version at <a href=\"#\">https://github.com/paranoramix/</a> </div></div><div class=\"form-container configure-container\"> <form action=\"/configure\" method=\"POST\" > <h1>Configure</h1> <span>Your ESP Sensor (ChipID: " + String(ESP.getChipId(), HEX) + ")</span> <input type=\"text\" placeholder=\"SSID\" name=\"ssid\"/> <input type=\"text\" placeholder=\"Passphrase\" name=\"password\"/> <input type=\"text\" placeholder=\"Sleep Time\" name=\"sleep\"/> <input type=\"text\" placeholder=\"Altitude\" name=\"altitude\"/> <button>Update</button> </form> <div class=\"social-container social-container-bottom\"><a href=\"/restart\">Click here to restart your ESP Sensor</a> </div></div><div class=\"overlay-container\"> <div class=\"overlay\"> <div class=\"overlay-panel overlay-left\"> <div class=\"social-container social-container-top\"> <a href=\"#\" class=\"social\">" + String(sensorValues.temperature, 1) + "°C</a> <a href=\"#\" class=\"social\">" + String(sensorValues.humidity, 0) + "%</a> <a href=\"#\" class=\"social\">" + String(sensorValues.seaLevelPressure, 0) + "HPa</a> <a href=\"#\" class=\"social\">" + String(ESP.getVcc() / 1000.0, 2) + "V</a> <a href=\"#\" class=\"social\">" + String(rtcData.count) + "</a> </div><h1>Configure</h1> <p>Your ESP Sensor (WiFi, altitude, period)</p><button class=\"over-btn\" id=\"update\">Go!</button> </div><div class=\"overlay-panel overlay-right\"> <h1>Update</h1> <p>the Firmware of your ESP Sensor</p><button class=\"over-btn\" id=\"configure\">Go!</button> <div class=\"social-container social-container-top\"> <a href=\"#\" class=\"social\">" + String(sensorValues.temperature, 1) + "°C</a> <a href=\"#\" class=\"social\">" + String(sensorValues.humidity, 0) + "%</a> <a href=\"#\" class=\"social\">" + String(sensorValues.seaLevelPressure, 0) + "HPa</a> <a href=\"#\" class=\"social\">" + String(ESP.getVcc() / 1000.0, 2) + "V</a> <a href=\"#\" class=\"social\">" + String(rtcData.count) + "</a> </div></div></div></div></div><script type=\"text/javascript\" src=\"/script.js\"></script></body></html>");
}

void handleRestart() {
  server.send(200, "text/plain", "Reboot will be done in few seconds");

  delay(1000);
  WiFi.disconnect(true);
  //Serial.println("Going to sleep");
  ESP.deepSleepInstant(5 * 1e6, WAKE_RF_DISABLED);
}

void handleConfigure() {
  if (!server.hasArg("ssid") || !server.hasArg("password") || server.arg("ssid") == NULL || server.arg("password") == NULL) {
    server.send(400, "text/plain", "400: Invalid Request");
    ledBlink(WIFI_CONFIGURATION_ERROR);
    ledBlink(WIFI_CONFIGURATION);
    return;
  }

  ledBlink(WIFI_CONFIGURATION_SUCCESS);

  String sta_ssid = server.arg("ssid");
  String sta_password = server.arg("password");

  if (server.hasArg("sleep") && server.arg("sleep") != NULL) {
    eepromData.sleep_time = String(server.arg("sleep")).toFloat();
  }

  if (server.hasArg("altitude") && server.arg("altitude") != NULL) {
    eepromData.altitude = String(server.arg("altitude")).toFloat();
  } else {
    eepromData.altitude = 0.0F;
  }

  memcpy(eepromData.ap_ssid, sta_ssid.c_str(), 32);
  memcpy(eepromData.ap_passphrase, sta_password.c_str(), 64);

  // Storing EEPROM data...
  eepromData.crc32 = CRC32((uint8_t*) &eepromData + 24, sizeof(eepromData) - 24);
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
  //Serial.println("Going to sleep");
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
  //Serial.begin(74880);
  delay(1);
  //Serial.println();
  //Serial.println(ESP.getChipId(), HEX);
  //Serial.println(ESP.getFlashChipId(), HEX);
  //Serial.println(ESP.getFlashChipRealSize());
  //Serial.println(ESP.getFlashChipSize());
  //Serial.println(ESP.getFlashChipSpeed());
  //Serial.println(ESP.getFlashChipMode());

  sleep_time = DEFAULT_SLEEP_TIME;

  ledSetup();
  // Important: to preserve battery, we force stop WiFi at startup.
  //Serial.println("To preserve battery, we force stop WiFi at startup.");
  WiFi.mode(WIFI_OFF);
  WiFi.forceSleepBegin();

  // Displaying the reset information
  //Serial.println("ResetInfo: " + ESP.getResetReason());
  delay(1);

  // Now we start the sequence to start WiFi, according to the reset information
  switch (ESP.getResetInfoPtr()->reason) {
    case REASON_EXT_SYS_RST:
      // This is a restart on demand. In this way, we will configure WiFi
      //Serial.println("Configuration mode: allow user to configure WiFi settings");
      ledBlink(WIFI_CONFIGURATION);

      // Prepare for OTA Web Browser update.
      httpUpdater.setup(&server, "/firmware", "admin", "admin");

      // We have to create a server.
      server.on("/", handleRoot);
      server.on("/configure", handleConfigure);
      server.on("/script.js", handleJavascript);
      server.on("/style.css", handleCss);
      server.on("/restart", handleRestart);
      server.onNotFound(handleNotFound);


      // Starting WiFi Access Point.
      WiFi.persistent(false);
      WiFi.mode(WIFI_AP);
      delay(1);
      WiFi.softAP("ESP_" + String(ESP.getChipId(), HEX));
      //Serial.print("AP IP address: ");
      //Serial.println(WiFi.softAPIP());
      delay(500);
      server.begin();
      //Serial.println("HTTP server started");

      break;

    //case REASON_DEEP_SLEEP_AWAKE:
    default:
      // This is the "normal" mode when the module ended sleep period.
      // In this way, we will get sensor measures, connect to the WiFi
      // and send data to the server. After that, sensor will go back
      // to sleep.
      //Serial.println("Normal mode: getting sensor measures to send it to server.");

      bool rtcIsValid = false;

      if (ESP.rtcUserMemoryRead(0, (uint32_t*)&rtcData, sizeof(rtcData))) {
        // Able to read data from rtc Memory.
        uint32_t crc = CRC32((uint8_t*) &rtcData + 24, sizeof(rtcData) - 24);

        if (crc == rtcData.crc32) {
          // rtcData is valid.
          rtcIsValid = true;
          sleep_time = rtcData.sleep_time;
        }
      }

      // We start measurement.
      if (fireMeasures()) {
        // If measures are OK, we start WiFi.
        //Serial.println("Measures done! Starting WiFi.");
        WiFi.persistent(false);
        delay(1);

        WiFi.mode(WIFI_STA);
        delay(1);

        if (rtcIsValid) {
          //Serial.println("RTC data are valid. Quick connect!");
          WiFi.config(rtcData.ip, rtcData.gw, rtcData.msk);
          WiFi.begin((const char*) rtcData.ap_ssid, (const char*) rtcData.ap_passphrase, rtcData.ap_channel, rtcData.ap_bssid, true);
        } else {
          // If RTC is not valid, we get connection Data from EEPROM
          //Serial.println("RTC is not valid, we get connection Data from EEPROM");

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

            //Serial.println("Error: WIFI is not configured");
            // Go to sleep...
            ESP.deepSleepInstant(sleep_time * 1e6, WAKE_RF_DISABLED);
          }

          delay(1);
        }

        float batVal = ESP.getVcc() / 1000.0;

        // If we are here, it means data can be sent.
        //Serial.println("Temperature: " + String(sensorValues.temperature) + "(diff: " + abs(rtcData.temp * 10 - sensorValues.temperature * 10) + ")");
        //Serial.println("Humidity: " + String(sensorValues.humidity) + "(diff: " + abs(rtcData.hum * 10 - sensorValues.humidity * 10) + ")");
        //Serial.println("Pressure: " + String(sensorValues.pressure));
        //Serial.println("Sea Level Pressure: " + String(sensorValues.seaLevelPressure) + "(diff: " + abs(rtcData.pres * 10 - sensorValues.seaLevelPressure * 10) + ")");
        //Serial.println("Battery: " + String(batVal) + "(diff: " + abs(rtcData.volt * 100 - batVal * 100) + ")");
        //Serial.println("Counter: " + String(rtcData.count));

        if (abs(rtcData.temp * 10 - sensorValues.temperature * 10) >= 1) {
          sendGETRequest("http://10.3.141.1:8121/device=Sensor&taskid=" + String(ESP.getChipId()) + "&cmd=temperature&value=" + String(sensorValues.temperature));
          rtcData.temp = sensorValues.temperature;
        }

        if (abs(rtcData.hum * 10 - sensorValues.humidity * 10) >= 5) {
          sendGETRequest("http://10.3.141.1:8121/device=Sensor&taskid=" + String(ESP.getChipId()) + "&cmd=humidity&value=" + String(sensorValues.humidity));
          rtcData.hum = sensorValues.humidity;
        }

        if (abs(rtcData.volt * 100 - batVal * 100) >= 1) {

          sendGETRequest("http://10.3.141.1:8121/device=Sensor&taskid=" + String(ESP.getChipId()) + "&cmd=battery&value=" + String(batVal, 2));
          rtcData.volt = batVal;
        }

        if (abs(rtcData.pres * 10 - sensorValues.seaLevelPressure * 10) >= 5) {
          sendGETRequest("http://10.3.141.1:8121/device=Sensor&taskid=" + String(ESP.getChipId()) + "&cmd=sealevelpressure&value=" + String(sensorValues.seaLevelPressure));
          rtcData.pres = sensorValues.seaLevelPressure;
        }

        sendGETRequest("http://10.3.141.1:8121/device=Sensor&taskid=" + String(ESP.getChipId()) + "&cmd=count&value=" + String(rtcData.count));


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

          rtcData.altitude = eepromData.altitude;

          rtcData.crc32 = CRC32((uint8_t*) &rtcData + 24, sizeof(rtcData) - 24);

          ledBlink(RTC_UPDATED);
        }

        rtcData.count = rtcData.count + 1;
        ESP.rtcUserMemoryWrite(0, (uint32_t*) &rtcData, sizeof(rtcData));
      }

      sendGETRequest("http://10.3.141.1:8121/device=Sensor&taskid=" + String(ESP.getChipId()) + "&cmd=time&value=" + String(millis() / 1000.0));

      // And now, we can stop WiFi and go to sleep.
      WiFi.disconnect(true);

      //Serial.println("Going to sleep for " + String(sleep_time) + "sec");

      ledBlink(OFF);

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
