#include <WiFi.h>
#include <WiFiMulti.h>
#include <AsyncTCP.h>
#include "ESPAsyncWebServer.h"
#include <EEPROM.h>
#include "esp_task_wdt.h"
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "lib/menu.h"
#include "lib/StringStream.h"
#include "M5StickCPlus2.h"
#include <ImprovWiFiLibrary.h>

#include <DMComm.h>

WiFiMulti wifiMulti;
AsyncWebServer server(80);
ImprovWiFi improvSerial(&Serial);
WiFiClient espClient;
PubSubClient client(espClient);
using namespace DMComm;

#define mqtt_broker "mqtt.wificom.dev"
#define mqtt_port 1883

String boardname = "M5StickCPlus2";
String version = "beta-2024-11-11";

int BrightnessMAX = 70;
int BrightnessMIN = 1;

int INPUT_PIN = 36;
int OUTPUT_PIN = 26;

int led_pin = 19;
int board_voltage = 3300;
int analog_resolution = 10;

DComOutput output = DComOutput(OUTPUT_PIN, DMCOMM_NO_PIN);
AnalogProngInput input = AnalogProngInput(INPUT_PIN, board_voltage, analog_resolution);
ClassicCommunicator classic_comm = ClassicCommunicator(output, input);
ColorCommunicator color_comm = ColorCommunicator(output, input);
Controller controller = Controller();

String secrets_mqtt_username = "";
String secrets_mqtt_password = "";
String secrets_user_uuid = "";
String secrets_device_uuid = "";

String mqtt_io_prefix;
String mqtt_topic_identifier;
String mqtt_topic_input;
String mqtt_topic_output;

String digioutput;
String mqtt_digirom;
String application_id;
boolean hide_output;
boolean api_response;
String ack_id;

String* temp_array;
String* temp_code_array;

int connectc = 0;
int prog_index = 0;
int menu_index = 0;
int menu_id = 0;
int old_menu_id = 1;
int inProg = 0;
int exit_c = 0;
int active_time = 1000 * 30;
int punch_index = 0;
int old_punch_index = 1;
int sub_punch_index = 0;
int old_sub_punch_index = 1;
int PunchingMODE = 0;
int reboot = 0;
int scrollOffset = 0;

bool active_sc = true;
unsigned long active_time_tmp = 0;
unsigned long timer = 0;
unsigned long time_bt = 0;
unsigned long time_batt = 0;
unsigned long time_sc = 0;

BaseDigiROM* buffer_digirom = nullptr;

void setup() {

  auto cfg = M5.config();
  StickCP2.begin(cfg);

  Serial.begin(9600);
  EEPROM.begin(512);
  analogReadResolution(10);

  String fristrun = readStringFromEEPROM(500);
  if (fristrun != "free") {
    writeStringToEEPROM(0, "");
    writeStringToEEPROM(50, "");
    writeStringToEEPROM(100, "");
    writeStringToEEPROM(150, "");
    writeStringToEEPROM(200, "");
    writeStringToEEPROM(250, "");
    writeStringToEEPROM(300, "");
    writeStringToEEPROM(350, "");
    writeStringToEEPROM(400, "");
    writeStringToEEPROM(450, "");
    writeStringToEEPROM(500, "free");
  }
  secrets_mqtt_username = readStringFromEEPROM(300);
  secrets_mqtt_password = readStringFromEEPROM(350);
  secrets_user_uuid = readStringFromEEPROM(400);
  secrets_device_uuid = readStringFromEEPROM(450);

  mqtt_io_prefix = secrets_mqtt_username + "/f/";
  mqtt_topic_identifier = secrets_user_uuid + '-' + secrets_device_uuid;
  mqtt_topic_input = mqtt_io_prefix + mqtt_topic_identifier + "/wificom-input";
  mqtt_topic_output = mqtt_io_prefix + mqtt_topic_identifier + "/wificom-output";

  StickCP2.Display.setBrightness(BrightnessMAX);
  StickCP2.Display.fillScreen(BLACK);
  StickCP2.Display.setSwapBytes(true);
  StickCP2.Display.setRotation(1);
  StickCP2.Display.setTextWrap(0);
  StickCP2.Display.loadFont(kanitFont);

  controller.add(classic_comm);
  controller.add(color_comm);
  client.setBufferSize(1024);
  client.setServer(mqtt_broker, mqtt_port);
  client.setCallback(mqttcallback);
}

void loop() {
  StickCP2.update();
  if (StickCP2.BtnA.wasPressed() && inProg == 0) {
    if (active_sc) {
      runProgram();
    }
    active_screen();
    StickCP2.Speaker.tone(8000, 20);
    delay(10);

  } else if (StickCP2.BtnB.wasPressed() && inProg == 0) {
    if (active_sc) {
      ++menu_id;
      if (menu_id > 4 && menu_index == 0) {
        menu_id = 0;
      } else if (menu_id > 2 && menu_index > 0) {
        menu_id = 0;
      }
    }
    active_screen();
    StickCP2.Speaker.tone(8000, 20);
    delay(10);

  } else if (StickCP2.BtnA.pressedFor(3000)) {
    active_screen();
    StickCP2.Display.fillScreen(BLACK);
    StickCP2.Display.setCursor(40, 55);
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.print("RESTART..");
    delay(1000);
    exit_c = 1;
    ESP.restart();
  }

  if (millis() - active_time_tmp >= active_time) {
    active_time_tmp = millis();
    active_sc = false;
  }

  if (prog_index == 1) {
    //wificom program
    active_sc = true;
    String digioutput;
    if (!client.connected()) {
      Serial.print("MQTT connecting...");
      StickCP2.Display.setCursor(15, 56);
      StickCP2.Display.print("Try connecting MQTT.");
      const int length = 10;
      char clientid[length + 1];
      for (int i = 0; i < length; i++) {
        clientid[i] = '0' + random(10);
      }
      clientid[length] = '\0';
      if (client.connect(clientid, secrets_mqtt_username.c_str(), secrets_mqtt_password.c_str())) {
        client.subscribe(mqtt_topic_input.c_str());
        StickCP2.Display.pushImage(0, 0, 240, 135, wificonnect);
      } else {
        StickCP2.Display.pushImage(0, 0, 240, 135, mqttfail);
        delay(5000);
        return;
      }
    } else {
      if (mqtt_digirom != "") {
        const char* digirom = mqtt_digirom.c_str();
        mqtt_digirom.trim();
        DigiROMType rom_type = digiROMType(digirom);
        if (rom_type.signal_type != kSignalTypeInfo) {
          if (!hide_output) {
            Serial.print(F("got "));
            Serial.print(mqtt_digirom.length());
            Serial.print(F(" bytes: "));
            Serial.print(mqtt_digirom);
            Serial.print(F(" -> "));
          }
        }
        buffer_digirom = nullptr;
        switch (rom_type.signal_type) {
          case kSignalTypeV:
          case kSignalTypeX:
          case kSignalTypeY:
            buffer_digirom = new ClassicDigiROM(digirom);
            break;
          case kSignalTypeC:
            buffer_digirom = new WordsDigiROM(digirom);
            break;
          default:
            break;
        }
        if (rom_type.signal_type == kSignalTypeInfo) {
          if (!hide_output) {
            Serial.print(DMCOMM_BUILD_INFO);
          }
        } else if (buffer_digirom != nullptr) {
          if (!hide_output) {
            Serial.print(F("(new DigiROM)"));
          }
        } else {
          StickCP2.Display.setTextColor(0x57EA);
          StickCP2.Display.fillRect(0, 112, 240, 23, 0xA800);
          StickCP2.Display.drawString("PAUSE", 96, 116);
          StickCP2.Display.setTextColor(0xFFFF);
          if (!hide_output) {
            Serial.print(F("(paused)"));
          }
        }
        if (!hide_output) {
          Serial.println();
        }
      }

      mqtt_digirom = "";
      if (millis() - timer >= 5000) {
        timer = millis();
        if (buffer_digirom != nullptr) {
          active_screen();
          StickCP2.Display.setTextColor(0xA800);
          StickCP2.Display.fillRect(0, 112, 240, 23, 0x15);
          StickCP2.Display.drawString("Execute Command", 51, 116);
          StickCP2.Display.setTextColor(0xFFFF);
          controller.execute(*buffer_digirom, 3000);
          StringStream digistream(digioutput);
          buffer_digirom->printResult(digistream);
          if (digioutput != "") {
            if (!hide_output) {
              Serial.print(digioutput);
              Serial.println();
            }
            String digiout = "{\"name\":\"espcom\", \"board\":\"" + boardname + "\", \"version\":\"" + version + "\", \"application_uuid\": \"" + application_id + "\",\"device_uuid\": \"" + secrets_device_uuid + "\",\"output\": \"" + digioutput + "\"}";
            client.publish(mqtt_topic_output.c_str(), digiout.c_str());
          }
          if (buffer_digirom->turn() == 2 && digioutput.indexOf("r:") >= 0) {
            StickCP2.Display.setTextColor(0xA800);
            StickCP2.Display.fillRect(0, 112, 240, 19, 0x15);
            StickCP2.Display.drawString("Summon Successfully", 51, 116);
            StickCP2.Display.setTextColor(0xFFFF);

            controller.execute(*buffer_digirom, 3000);
            StickCP2.Speaker.tone(8000, 20);
            delay(10);
            StickCP2.Speaker.tone(8000, 20);
            delay(10);

          } else if (buffer_digirom->turn() == 1 && digioutput.indexOf("r:") >= 0) {
            StickCP2.Display.setTextColor(0xA800);
            StickCP2.Display.fillRect(0, 112, 240, 19, 0x15);
            StickCP2.Display.drawString("Battle Successfully", 51, 116);
            StickCP2.Display.setTextColor(0xFFFF);
            StickCP2.Speaker.tone(8000, 20);
            delay(10);
            StickCP2.Speaker.tone(8000, 20);
            delay(10);

          } else {
            StickCP2.Display.fillRect(0, 112, 240, 23, 0x0);
          }
          if (buffer_digirom->turn() == 1) {
            delay(1000);
          }
        } else {
          String digiout = "{\"name\":\"espcom\", \"board\":\"" + boardname + "\", \"version\":\"" + version + "\", \"device_uuid\": \"" + secrets_device_uuid + "\", \"output\": \"None\", \"application_uuid\": null}";
          client.publish(mqtt_topic_output.c_str(), digiout.c_str());
          delay(2000);
        }
      }
    }
    client.loop();
  }
  if (prog_index == 2) {
    active_sc = true;
    String digioutput;
    mqtt_digirom = Serial.readString();
    mqtt_digirom.trim();
    if (mqtt_digirom != "") {
      StickCP2.Display.setCursor(0, 115);
      StickCP2.Display.setTextColor(RED, GREEN);
      if (mqtt_digirom == "p" || mqtt_digirom == "P") {
        StickCP2.Display.setTextColor(0x57EA);
        StickCP2.Display.fillRect(0, 112, 240, 23, 0xA800);
        StickCP2.Display.drawString("PAUSE", 96, 116);
        StickCP2.Display.setTextColor(0xFFFF);
      } else {
        StickCP2.Display.setTextColor(0xA800);
        StickCP2.Display.fillRect(0, 112, 240, 23, 0x15);
        StickCP2.Display.drawString("Got New Command", 51, 116);
        StickCP2.Display.setTextColor(0xFFFF);
      }
      StickCP2.Display.setTextColor(WHITE, BLACK);
      StickCP2.Speaker.tone(8000, 20);
      delay(10);
      StickCP2.Speaker.tone(8000, 20);
      delay(10);

      const char* digirom = mqtt_digirom.c_str();
      DigiROMType rom_type = digiROMType(digirom);
      if (rom_type.signal_type != kSignalTypeInfo) {
        Serial.print(F("got "));
        Serial.print(mqtt_digirom.length());
        Serial.print(F(" bytes: "));
        Serial.print(mqtt_digirom);
        Serial.print(F(" -> "));
      }
      buffer_digirom = nullptr;
      switch (rom_type.signal_type) {
        case kSignalTypeV:
        case kSignalTypeX:
        case kSignalTypeY:
          buffer_digirom = new ClassicDigiROM(digirom);
          break;
        case kSignalTypeC:
          buffer_digirom = new WordsDigiROM(digirom);
          break;
        default:
          break;
      }
      if (rom_type.signal_type == kSignalTypeInfo) {
        Serial.print(DMCOMM_BUILD_INFO);
      } else if (buffer_digirom != nullptr) {
        Serial.print(F("(new DigiROM)"));
      } else {
        StickCP2.Display.setTextColor(0x57EA);
        StickCP2.Display.fillRect(0, 112, 240, 23, 0xA800);
        StickCP2.Display.drawString("PAUSE", 96, 116);
        StickCP2.Display.setTextColor(0xFFFF);
        Serial.print(F("(paused)"));
      }
      Serial.println();
    }
    mqtt_digirom = "";
    if (buffer_digirom != nullptr) {
      active_screen();
      StickCP2.Display.setTextColor(0xA800);
      StickCP2.Display.fillRect(0, 112, 240, 23, 0x15);
      StickCP2.Display.drawString("Execute Command", 51, 116);
      StickCP2.Display.setTextColor(0xFFFF);
      controller.execute(*buffer_digirom, 3000);
      StringStream digistream(digioutput);
      buffer_digirom->printResult(digistream);
      if (digioutput != "") {
        Serial.print(digioutput);
        Serial.println();
      }
      if (buffer_digirom->turn() == 2 && digioutput.indexOf("r:") >= 0) {
        StickCP2.Display.setTextColor(0xA800);
        StickCP2.Display.fillRect(0, 112, 240, 19, 0x15);
        StickCP2.Display.drawString("Summon Successfully", 51, 116);
        StickCP2.Display.setTextColor(0xFFFF);
        StickCP2.Speaker.tone(8000, 20);
        delay(10);
        StickCP2.Speaker.tone(8000, 20);
        delay(10);

      } else if (buffer_digirom->turn() == 1 && digioutput.indexOf("r:") >= 0) {
        StickCP2.Display.setTextColor(0xA800);
        StickCP2.Display.fillRect(0, 112, 240, 19, 0x15);
        StickCP2.Display.drawString("Battle Successfully", 51, 116);
        StickCP2.Display.setTextColor(0xFFFF);
        StickCP2.Speaker.tone(8000, 20);
        delay(10);
        StickCP2.Speaker.tone(8000, 20);
        delay(10);

      } else {
        StickCP2.Display.fillRect(0, 112, 240, 23, 0x0);
      }
      if (buffer_digirom->turn() == 1) {
        delay(1000);
      }
    } else {
      delay(DMCOMM_INACTIVE_DELAY_MILLIS);
    }
  }
  if (prog_index == 3) {
    if (StickCP2.BtnA.wasPressed() && PunchingMODE == 1) {
      active_screen();
      prog_index = 5;
      time_bt = millis();
      displaySubMenu();
    }
    if (StickCP2.BtnB.wasPressed()) {
      active_screen();
      ++punch_index;
    }
    if (StickCP2.BtnPWR.wasPressed()) {
      active_screen();
      --punch_index;
    }
    if (punch_index > 15) {
      punch_index = 0;
    }
    if (punch_index < 0) {
      punch_index = 15;
    }
    if (punch_index != old_punch_index) {
      old_punch_index = punch_index;
      PunchingBag();
    }
  }
  if (prog_index == 5) {
    if (StickCP2.BtnA.wasPressed() && PunchingMODE == 2 && (millis() - time_bt > 500)) {
      active_screen();
      time_bt = millis();
      Serial.println("go to punching");
      PunchingMODE = 3;
      PunchingStart(temp_array[sub_punch_index], temp_code_array[sub_punch_index]);
    }
    if (StickCP2.BtnB.wasPressed()) {
      active_screen();
      ++sub_punch_index;
    }
    if (StickCP2.BtnPWR.wasPressed()) {
      active_screen();
      --sub_punch_index;
    }
    if (sub_punch_index != old_sub_punch_index) {
      old_sub_punch_index = sub_punch_index;
      displaySubMenu();
      scrollOffset = 0;
    }

    if (sub_punch_index >= 0 && sub_punch_index <= 5 && temp_array[sub_punch_index].length() > 27 && (millis() - time_sc > 300)) {
      time_sc = millis();
      if (sub_punch_index == 0) {
        StickCP2.Display.fillRect(0, 8, 240, 20, 0x0);
        StickCP2.Display.setCursor(2, 8);
      } else if (sub_punch_index == 1) {
        StickCP2.Display.fillRect(0, 28, 240, 20, 0x0);
        StickCP2.Display.setCursor(2, 28);
      } else if (sub_punch_index == 2) {
        StickCP2.Display.fillRect(0, 48, 240, 20, 0x0);
        StickCP2.Display.setCursor(2, 48);
      } else if (sub_punch_index == 3) {
        StickCP2.Display.fillRect(0, 68, 240, 20, 0x0);
        StickCP2.Display.setCursor(2, 68);
      } else if (sub_punch_index == 4) {
        StickCP2.Display.fillRect(0, 88, 240, 20, 0x0);
        StickCP2.Display.setCursor(2, 88);
      } else if (sub_punch_index == 5) {
        StickCP2.Display.fillRect(0, 108, 240, 20, 0x0);
        StickCP2.Display.setCursor(2, 108);
      }
      StickCP2.Display.print("> ");
      StickCP2.Display.print(temp_array[sub_punch_index].substring(scrollOffset, scrollOffset + 27));
      if (scrollOffset > (temp_array[sub_punch_index].length() - 26)) {
        scrollOffset = 0;
      } else if (scrollOffset < temp_array[sub_punch_index].length()) {
        scrollOffset++;
      }
    }

    if (sub_punch_index > 5 && temp_array[sub_punch_index].length() > 27 && (millis() - time_sc > 300)) {
      time_sc = millis();
      StickCP2.Display.fillRect(0, 108, 240, 20, 0x0);
      StickCP2.Display.setCursor(2, 108);
      StickCP2.Display.print("> ");
      StickCP2.Display.print(temp_array[sub_punch_index].substring(scrollOffset, scrollOffset + 27));
      if (scrollOffset > (temp_array[sub_punch_index].length() - 26)) {
        scrollOffset = 0;
      } else if (scrollOffset < temp_array[sub_punch_index].length()) {
        scrollOffset++;
      }
    }

    if (StickCP2.BtnB.pressedFor(2000) && (millis() - time_bt > 1000)) {
      time_bt = millis();
      sub_punch_index = 0;
      old_sub_punch_index = 1;
      prog_index = 3;
      PunchingBag();
    }
  }
  if (prog_index == 4) {
    improvSerial.handleSerial();
  }
  if (inProg == 0 && menu_id != old_menu_id) {
    ShowMainMenu();
    old_menu_id = menu_id;
  }
  if ((millis() - time_batt) > 15000) {
    time_batt = millis();
    batt_show();
  }
  if (active_sc) {
    StickCP2.Display.setBrightness(BrightnessMAX);
  } else {
    StickCP2.Display.setBrightness(BrightnessMIN);
  }
  if (reboot) {
    delay(5000);
    ESP.restart();
  }
}

void runProgram() {
  inProg = 1;
  StickCP2.Display.fillScreen(BLACK);
  StickCP2.Display.setCursor(8, 0);
  if (menu_index == 0 && menu_id == 0) {  //wificom menu
    wificom();
  } else if (menu_index == 0 && menu_id == 1) {  //ACOM[USB]
    acommode();
  } else if (menu_index == 0 && menu_id == 2) {  //PunchingBag
    prog_index = 3;
  } else if (menu_index == 0 && menu_id == 3) {  //Setting
    Serial.begin(115200);
    wifisetting();
  } else if (menu_index == 0 && menu_id == 4) {  //shutdown
    StickCP2.Power.powerOff();
  }
}

void ShowMainMenu() {
  inProg = 0;
  StickCP2.Display.fillScreen(TFT_BLACK);
  if (menu_id == 0) {
    StickCP2.Display.pushImage(0, 0, 240, 135, menu_0);
  } else if (menu_id == 1) {
    StickCP2.Display.pushImage(0, 0, 240, 135, menu_1);
  } else if (menu_id == 2) {
    StickCP2.Display.pushImage(0, 0, 240, 135, menu_2);
  } else if (menu_id == 3) {
    StickCP2.Display.pushImage(0, 0, 240, 135, menu_3);
  } else if (menu_id == 4) {
    StickCP2.Display.pushImage(0, 0, 240, 135, menu_4);
  }
  active_screen();
}

void displaySubMenu() {
  StickCP2.Display.fillScreen(BLACK);
  int size_index = 0;
  if (punch_index == 0) {
    size_index = 2;
    temp_array = dmog_n;
    temp_code_array = dmog_c;
  } else if (punch_index == 1) {
    size_index = 8;
    temp_array = dmx_n;
    temp_code_array = dmx_c;
  } else if (punch_index == 2) {
    size_index = 39;
    temp_array = dm20_n;
    temp_code_array = dm20_c;
  } else if (punch_index == 3) {
    size_index = 11;
    temp_array = dmc_n;
    temp_code_array = dmc_c;
  } else if (punch_index == 4) {
    size_index = 1;
    temp_array = dmmini_n;
    temp_code_array = dmmini_c;
  } else if (punch_index == 5) {
    size_index = 1;
    temp_array = xros_n;
    temp_code_array = xros_c;
  } else if (punch_index == 6) {
    size_index = 26;
    temp_array = penz_n;
    temp_code_array = penz_c;
  } else if (punch_index == 7) {
    size_index = 29;
    temp_array = pen20_n;
    temp_code_array = pen20_c;
  } else if (punch_index == 8) {
    size_index = 35;
    temp_array = penc_n;
    temp_code_array = penc_c;
  } else if (punch_index == 9) {
    size_index = 18;
    temp_array = penog_n;
    temp_code_array = penog_c;
  } else if (punch_index == 10) {
    size_index = 45;
    temp_array = penx_n;
    temp_code_array = penx_c;
  } else if (punch_index == 11) {
    size_index = 2;
    temp_array = d2_n;
    temp_code_array = d2_c;
  } else if (punch_index == 12) {
    size_index = 15;
    temp_array = d3_n;
    temp_code_array = d3_c;
  } else if (punch_index == 13) {
    size_index = 14;
    temp_array = dscan_n;
    temp_code_array = dscan_c;
  } else if (punch_index == 14) {
    size_index = 2;
    temp_array = accel_n;
    temp_code_array = accel_c;
  } else if (punch_index == 15) {
    size_index = 1;
    temp_array = ic_n;
    temp_code_array = ic_c;
  }
  if (sub_punch_index > size_index - 1) {
    sub_punch_index = 0;
  }
  if (sub_punch_index < 0) {
    sub_punch_index = size_index - 1;
  }
  int i = sub_punch_index;
  
  if (i == 0) {
    StickCP2.Display.setCursor(2, 8);
    if (temp_array[0] != "") {
      StickCP2.Display.print("> ");
    }
    if (temp_array[0] != "") {
      StickCP2.Display.print(temp_array[0]);
    }
    if (size_index > 1) {
      StickCP2.Display.setCursor(2, 28);
      if (temp_array[1] != "") {
        StickCP2.Display.print(temp_array[1]);
      }
    }
    if (size_index > 2) {
      StickCP2.Display.setCursor(2, 48);
      if (temp_array[2] != "") {
        StickCP2.Display.print(temp_array[2]);
      }
    }
    if (size_index > 3) {
      StickCP2.Display.setCursor(2, 68);
      if (temp_array[3] != "") {
        StickCP2.Display.print(temp_array[3]);
      }
    }
    if (size_index > 4) {
      StickCP2.Display.setCursor(2, 88);
      if (temp_array[4] != "") {
        StickCP2.Display.print(temp_array[4]);
      }
    }
    if (size_index > 5) {
      StickCP2.Display.setCursor(2, 108);
      if (temp_array[5] != "") {
        StickCP2.Display.print(temp_array[5]);
      }
    }
  } else if (i == 1) {
    StickCP2.Display.setCursor(2, 8);
    if (temp_array[0] != "") {
      StickCP2.Display.print(temp_array[0]);
    }
    if (size_index > 1) {
      StickCP2.Display.setCursor(2, 28);
      if (temp_array[1] != "") {
        StickCP2.Display.print("> ");
      }
      if (temp_array[1] != "") {
        StickCP2.Display.print(temp_array[1]);
      }
    }
    if (size_index > 2) {
      StickCP2.Display.setCursor(2, 48);
      if (temp_array[2] != "") {
        StickCP2.Display.print(temp_array[2]);
      }
    }
    if (size_index > 3) {
      StickCP2.Display.setCursor(2, 68);
      if (temp_array[3] != "") {
        StickCP2.Display.print(temp_array[3]);
      }
    }
    if (size_index > 4) {
      StickCP2.Display.setCursor(2, 88);
      if (temp_array[4] != "") {
        StickCP2.Display.print(temp_array[4]);
      }
    }
    if (size_index > 5) {
      StickCP2.Display.setCursor(2, 108);
      if (temp_array[5] != "") {
        StickCP2.Display.print(temp_array[5]);
      }
    }
  } else if (i == 2) {
    StickCP2.Display.setCursor(2, 8);
    if (temp_array[0] != "") {
      StickCP2.Display.print(temp_array[0]);
    }
    if (size_index > 1) {
      StickCP2.Display.setCursor(2, 28);
      if (temp_array[1] != "") {
        StickCP2.Display.print(temp_array[1]);
      }
    }
    if (size_index > 2) {
      StickCP2.Display.setCursor(2, 48);
      if (temp_array[2] != "") {
        StickCP2.Display.print("> ");
      }
      if (temp_array[2] != "") {
        StickCP2.Display.print(temp_array[2]);
      }
    }
    if (size_index > 3) {
      StickCP2.Display.setCursor(2, 68);
      if (temp_array[3] != "") {
        StickCP2.Display.print(temp_array[3]);
      }
    }
    if (size_index > 4) {
      StickCP2.Display.setCursor(2, 88);
      if (temp_array[4] != "") {
        StickCP2.Display.print(temp_array[4]);
      }
    }
    if (size_index > 5) {
      StickCP2.Display.setCursor(2, 108);
      if (temp_array[5] != "") {
        StickCP2.Display.print(temp_array[5]);
      }
    }
  } else if (i == 3) {
    StickCP2.Display.setCursor(2, 8);
    if (temp_array[0] != "") {
      StickCP2.Display.print(temp_array[0]);
    }
    if (size_index > 1) {
      StickCP2.Display.setCursor(2, 28);
      if (temp_array[1] != "") {
        StickCP2.Display.print(temp_array[1]);
      }
    }
    if (size_index > 2) {
      StickCP2.Display.setCursor(2, 48);
      if (temp_array[2] != "") {
        StickCP2.Display.print(temp_array[2]);
      }
    }
    if (size_index > 3) {
      StickCP2.Display.setCursor(2, 68);
      if (temp_array[3] != "") {
        StickCP2.Display.print("> ");
      }
      if (temp_array[3] != "") {
        StickCP2.Display.print(temp_array[3]);
      }
    }
    if (size_index > 4) {
      StickCP2.Display.setCursor(2, 88);
      if (temp_array[4] != "") {
        StickCP2.Display.print(temp_array[4]);
      }
    }
    if (size_index > 5) {
      StickCP2.Display.setCursor(2, 108);
      if (temp_array[5] != "") {
        StickCP2.Display.print(temp_array[5]);
      }
    }
  } else if (i == 4) {
    StickCP2.Display.setCursor(2, 8);
    if (temp_array[0] != "") {
      StickCP2.Display.print(temp_array[0]);
    }
    if (size_index > 1) {
      StickCP2.Display.setCursor(2, 28);
      if (temp_array[1] != "") {
        StickCP2.Display.print(temp_array[1]);
      }
    }
    if (size_index > 2) {
      StickCP2.Display.setCursor(2, 48);
      if (temp_array[2] != "") {
        StickCP2.Display.print(temp_array[2]);
      }
    }
    if (size_index > 3) {
      StickCP2.Display.setCursor(2, 68);
      if (temp_array[3] != "") {
        StickCP2.Display.print(temp_array[3]);
      }
    }
    if (size_index > 4) {
      StickCP2.Display.setCursor(2, 88);
      if (temp_array[4] != "") {
        StickCP2.Display.print("> ");
      }
      if (temp_array[4] != "") {
        StickCP2.Display.print(temp_array[4]);
      }
    }
    if (size_index > 5) {
      StickCP2.Display.setCursor(2, 108);
      if (temp_array[5] != "") {
        StickCP2.Display.print(temp_array[5]);
      }
    }
  } else if (i == 5) {
    if (temp_array[0] != "") {
      StickCP2.Display.setCursor(2, 8);
      StickCP2.Display.print(temp_array[0]);
    }
    if (size_index > 1) {
      StickCP2.Display.setCursor(2, 28);
      if (temp_array[1] != "") {
        StickCP2.Display.print(temp_array[1]);
      }
    }
    if (size_index > 2) {
      StickCP2.Display.setCursor(2, 48);
      if (temp_array[2] != "") {
        StickCP2.Display.print(temp_array[2]);
      }
    }
    if (size_index > 3) {
      StickCP2.Display.setCursor(2, 68);
      if (temp_array[3] != "") {
        StickCP2.Display.print(temp_array[3]);
      }
    }
    if (size_index > 4) {
      StickCP2.Display.setCursor(2, 88);
      if (temp_array[4] != "") {
        StickCP2.Display.print(temp_array[4]);
      }
    }
    if (size_index > 5) {
      StickCP2.Display.setCursor(2, 108);
      if (temp_array[5] != "") {
        StickCP2.Display.print("> ");
      }
      if (temp_array[5] != "") {
        StickCP2.Display.print(temp_array[5]);
      }
    }
  } else if (i > 5) {
    StickCP2.Display.setCursor(2, 8);
    if (temp_array[i - 5] != "") {
      StickCP2.Display.print(temp_array[i - 5]);
    }
    StickCP2.Display.setCursor(2, 28);
    if (temp_array[i - 4] != "") {
      StickCP2.Display.print(temp_array[i - 4]);
    }
    StickCP2.Display.setCursor(2, 48);
    if (temp_array[i - 3] != "") {
      StickCP2.Display.print(temp_array[i - 3]);
    }
    StickCP2.Display.setCursor(2, 68);
    if (temp_array[i - 2] != "") {
      StickCP2.Display.print(temp_array[i - 2]);
    }
    StickCP2.Display.setCursor(2, 88);
    if (temp_array[i - 1] != "") {
      StickCP2.Display.print(temp_array[i - 1]);
    }
    StickCP2.Display.setCursor(2, 108);
    if (temp_array[i] != "") {
      StickCP2.Display.print("> ");
    }
    if (temp_array[i] != "") {
      StickCP2.Display.print(temp_array[i]);
    }
  }
  PunchingMODE = 2;
  Serial.println(i);
  batt_show();
}

void PunchingBag() {
  inProg = 1;
  prog_index = 3;
  PunchingMODE = 1;
  
  StickCP2.Display.fillScreen(BLACK);
  StickCP2.Display.setCursor(2, 8);
  StickCP2.Display.print(" > " + device[punch_index]);
  StickCP2.Display.setCursor(2, 28);
  StickCP2.Display.print("   " + device[(punch_index + 1 > 15 ? punch_index - 15 : punch_index + 1)]);
  StickCP2.Display.setCursor(2, 48);
  StickCP2.Display.print("   " + device[(punch_index + 2 > 15 ? punch_index - 14 : punch_index + 2)]);
  StickCP2.Display.setCursor(2, 68);
  StickCP2.Display.print("   " + device[(punch_index + 3 > 15 ? punch_index - 13 : punch_index + 3)]);
  StickCP2.Display.setCursor(2, 88);
  StickCP2.Display.print("   " + device[(punch_index + 4 > 15 ? punch_index - 12 : punch_index + 4)]);
  StickCP2.Display.setCursor(2, 108);
  StickCP2.Display.print("   " + device[(punch_index + 5 > 15 ? punch_index - 11 : punch_index + 5)]);
  Serial.println(punch_index);
  batt_show();
}

void active_screen() {
  active_sc = true;
  active_time_tmp = millis();
  time_batt = millis();
  batt_show();
}

void batt_show() {
  if (prog_index == 5) {
    return;
  }
  int vol = map(StickCP2.Power.getBatteryVoltage(), 3100, 4000, 0, 100);
  if (vol >= 1 && vol <= 10) {
    StickCP2.Display.drawBitmap(210, 2, image_battery_10_bits, 24, 16, 0xFFFF);
  } else if (vol > 10 && vol <= 20) {
    StickCP2.Display.drawBitmap(210, 2, image_battery_20_bits, 24, 16, 0xFFFF);
  } else if (vol > 20 && vol <= 30) {
    StickCP2.Display.drawBitmap(210, 2, image_battery_30_bits, 24, 16, 0xFFFF);
  } else if (vol > 30 && vol <= 40) {
    StickCP2.Display.drawBitmap(210, 2, image_battery_40_bits, 24, 16, 0xFFFF);
  } else if (vol > 40 && vol <= 50) {
    StickCP2.Display.drawBitmap(210, 2, image_battery_50_bits, 24, 16, 0xFFFF);
  } else if (vol > 50 && vol <= 60) {
    StickCP2.Display.drawBitmap(210, 2, image_battery_60_bits, 24, 16, 0xFFFF);
  } else if (vol > 60 && vol <= 70) {
    StickCP2.Display.drawBitmap(210, 2, image_battery_70_bits, 24, 16, 0xFFFF);
  } else if (vol > 70 && vol <= 80) {
    StickCP2.Display.drawBitmap(210, 2, image_battery_80_bits, 24, 16, 0xFFFF);
  } else if (vol > 80 && vol <= 90) {
    StickCP2.Display.drawBitmap(210, 2, image_battery_100_bits, 24, 16, 0xFFFF);
  } else if (vol > 90 && vol <= 100) {
    StickCP2.Display.drawBitmap(210, 2, image_battery_100_bits, 24, 16, 0xFFFF);
  } else {
    StickCP2.Display.drawBitmap(210, 2, image_battery_0_bits, 24, 16, 0xFFFF);
  }
}

void writeStringToEEPROM(int addrOffset, const String& strToWrite) {
  byte len = strToWrite.length();
  EEPROM.write(addrOffset, len);
  EEPROM.commit();
  for (int i = 0; i < len; i++) {
    EEPROM.write(addrOffset + 1 + i, strToWrite[i]);
    EEPROM.commit();
  }
}

String readStringFromEEPROM(int addrOffset) {
  int newStrLen = EEPROM.read(addrOffset);
  char data[newStrLen + 1];
  for (int i = 0; i < newStrLen; i++) {
    data[i] = EEPROM.read(addrOffset + 1 + i);
  }
  data[newStrLen] = '\0';
  return String(data);
}


void wifisetting() {
  prog_index = 4;
  StickCP2.Display.pushImage(0, 0, 240, 135, acomsetting);
  improvSerial.setDeviceInfo(ImprovTypes::ChipFamily::CF_ESP32, "byNaynan", version.c_str(), "m5Wificom", "http://{LOCAL_IPV4}");
  improvSerial.onImprovConnected(onImprovWiFiConnectedCb);
  improvSerial.handleSerial();
}

void onImprovWiFiConnectedCb(const char* ssid, const char* password) {
  
  writeStringToEEPROM(0, ssid);
  writeStringToEEPROM(50, password);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    String wifi_ssid1 = readStringFromEEPROM(0);
    String wifi_password1 = readStringFromEEPROM(50);
    String wifi_ssid2 = readStringFromEEPROM(100);
    String wifi_password2 = readStringFromEEPROM(150);
    String wifi_ssid3 = readStringFromEEPROM(200);
    String wifi_password3 = readStringFromEEPROM(250);

    String mqtt_username = readStringFromEEPROM(300);
    String mqtt_password = readStringFromEEPROM(350);
    String user_uuid = readStringFromEEPROM(400);
    String device_uuid = readStringFromEEPROM(450);
    request->send(200, "text/html", "<!DOCTYPE html><html><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1, user-scalable=no\"><style>html{font-family: Lucida Console, Courier New, monospace;}input[type=text],select {width: 100%;padding: 12px 20px;margin: 8px 0;display: inline-block;border: 1px solid #ccc;border-radius: 4px;box-sizing: border-box;}input[type=submit] {width: 100%;background-color: #4CAF50;color: white;padding: 14px 20px;margin: 8px 0;border: none;border-radius: 4px;cursor: pointer;}input[type=submit]:hover {background-color: #45a049;}</style><body><h2>Setting Page</h2><div><form action=\"/save\"><h3>WIFI Setting</h3><label for=\"wifi_ssid1\">WIFI SSID 1</label><input type=\"text\" id=\"wifi_ssid1\" name=\"wifi_ssid1\" value=\"" + readStringFromEEPROM(0) + "\" placeholder=\"[max50digit]\"><label for=\"wifi_password1\">WIFI PASSWORD 1</label><input type=\"text\" id=\"wifi_password1\" name=\"wifi_password1\" value=\"" + readStringFromEEPROM(50) + "\" placeholder=\"[max50digit]\"> <label for=\"wifi_ssid2\">WIFI SSID 2</label><input type=\"text\" id=\"wifi_ssid2\" name=\"wifi_ssid2\" value=\"" + readStringFromEEPROM(100) + "\" placeholder=\"[max50digit]\"><label for=\"wifi_password2\">WIFI PASSWORD 2</label><input type=\"text\" id=\"wifi_password2\" name=\"wifi_password2\" value=\"" + readStringFromEEPROM(150) + "\" placeholder=\"[max50digit]\"> <label for=\"wifi_ssid3\">WIFI SSID 3</label><input type=\"text\" id=\"wifi_ssid3\" name=\"wifi_ssid3\" value=\"" + readStringFromEEPROM(200) + "\" placeholder=\"[max50digit]\"><label for=\"wifi_password3\">WIFI PASSWORD 3</label><input type=\"text\" id=\"wifi_password3\" name=\"wifi_password3\" value=\"" + readStringFromEEPROM(250) + "\" placeholder=\"[max50digit]\"><hr><h3>WIFICOM Setting</h3><label for=\"mqtt_username\">mqtt_username</label><input type=\"text\" id=\"mqtt_username\" name=\"mqtt_username\" value=\"" + readStringFromEEPROM(300) + "\" placeholder=\"[max50digit]\"><label for=\"mqtt_password\">mqtt_password</label><input type=\"text\" id=\"mqtt_password\" name=\"mqtt_password\" value=\"" + readStringFromEEPROM(350) + "\" placeholder=\"[max50digit]\"><label for=\"user_uuid\">user_uuid</label><input type=\"text\" id=\"user_uuid\" name=\"user_uuid\" value=\"" + readStringFromEEPROM(400) + "\" placeholder=\"[max50digit]\"><label for=\"device_uuid\">device_uuid</label><input type=\"text\" id=\"device_uuid\" name=\"device_uuid\" value=\"" + readStringFromEEPROM(450) + "\" placeholder=\"[max50digit]\"><hr><input type=\"submit\" value=\"SAVE\"></form></div></body></html>");
  });

  server.on("/save", HTTP_GET, [](AsyncWebServerRequest* request) {
    String wifi_ssid1 = request->getParam("wifi_ssid1")->value();
    String wifi_password1 = request->getParam("wifi_password1")->value();
    String wifi_ssid2 = request->getParam("wifi_ssid2")->value();
    String wifi_password2 = request->getParam("wifi_password2")->value();
    String wifi_ssid3 = request->getParam("wifi_ssid3")->value();
    String wifi_password3 = request->getParam("wifi_password3")->value();
    writeStringToEEPROM(0, wifi_ssid1);
    writeStringToEEPROM(50, wifi_password1);
    writeStringToEEPROM(100, wifi_ssid2);
    writeStringToEEPROM(150, wifi_password2);
    writeStringToEEPROM(200, wifi_ssid3);
    writeStringToEEPROM(250, wifi_password3);

    String mqtt_username = request->getParam("mqtt_username")->value();
    String mqtt_password = request->getParam("mqtt_password")->value();
    String user_uuid = request->getParam("user_uuid")->value();
    String device_uuid = request->getParam("device_uuid")->value();
    writeStringToEEPROM(300, mqtt_username);
    writeStringToEEPROM(350, mqtt_password);
    writeStringToEEPROM(400, user_uuid);
    writeStringToEEPROM(450, device_uuid);


    request->send(200, "text/html", "<!DOCTYPE html><html><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1, user-scalable=no\"><style>html{font-family: Lucida Console, Courier New, monospace;}span{color: red;}input[type=text],select {width: 100%;padding: 12px 20px;margin: 8px 0;display: inline-block;border: 1px solid #ccc;border-radius: 4px;box-sizing: border-box;}input[type=submit] {width: 100%;background-color: #4CAF50;color: white;padding: 14px 20px;margin: 8px 0;border: none;border-radius: 4px;cursor: pointer;}input[type=submit]:hover {background-color: #45a049;}input[type=button] {width: 100%;background-color: red;color: white;padding: 14px 20px;margin: 8px 0;border: none;border-radius: 4px;cursor: pointer;}input[type=button]:hover {background-color: red;}</style><body><h2>Setting Page</h2><div><form action=\"/save\"><h3>WIFI Setting</h3><label for=\"wifi_ssid1\">WIFI SSID 1</label><input type=\"text\" id=\"wifi_ssid1\" name=\"wifi_ssid1\" value=\"" + readStringFromEEPROM(0) + "\" placeholder=\"[max50digit]\"><label for=\"wifi_password1\">WIFI PASSWORD 1</label><input type=\"text\" id=\"wifi_password1\" name=\"wifi_password1\" value=\"" + readStringFromEEPROM(50) + "\" placeholder=\"[max50digit]\"> <label for=\"wifi_ssid2\">WIFI SSID 2</label><input type=\"text\" id=\"wifi_ssid2\" name=\"wifi_ssid2\" value=\"" + readStringFromEEPROM(100) + "\" placeholder=\"[max50digit]\"><label for=\"wifi_password2\">WIFI PASSWORD 2</label><input type=\"text\" id=\"wifi_password2\" name=\"wifi_password2\" value=\"" + readStringFromEEPROM(150) + "\" placeholder=\"[max50digit]\"> <label for=\"wifi_ssid3\">WIFI SSID 3</label><input type=\"text\" id=\"wifi_ssid3\" name=\"wifi_ssid3\" value=\"" + readStringFromEEPROM(200) + "\" placeholder=\"[max50digit]\"><label for=\"wifi_password3\">WIFI PASSWORD 3</label><input type=\"text\" id=\"wifi_password3\" name=\"wifi_password3\" value=\"" + readStringFromEEPROM(250) + "\" placeholder=\"[max50digit]\"><hr><h3>WIFICOM Setting</h3><label for=\"mqtt_username\">mqtt_username</label><input type=\"text\" id=\"mqtt_username\" name=\"mqtt_username\" value=\"" + readStringFromEEPROM(300) + "\" placeholder=\"[max50digit]\"><label for=\"mqtt_password\">mqtt_password</label><input type=\"text\" id=\"mqtt_password\" name=\"mqtt_password\" value=\"" + readStringFromEEPROM(350) + "\" placeholder=\"[max50digit]\"><label for=\"user_uuid\">user_uuid</label><input type=\"text\" id=\"user_uuid\" name=\"user_uuid\" value=\"" + readStringFromEEPROM(400) + "\" placeholder=\"[max50digit]\"><label for=\"device_uuid\">device_uuid</label><input type=\"text\" id=\"device_uuid\" name=\"device_uuid\" value=\"" + readStringFromEEPROM(450) + "\" placeholder=\"[max50digit]\"><span>Saved successfully</span><hr><input type=\"submit\" value=\"SAVE\"><input type=\"button\" value=\"REBOOT\" onclick=\"window.location.href=\'/reboot\'\"></form></div></body></html>");
  });

  server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "text/html", "<!DOCTYPE html><html><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1, user-scalable=no\"><style>html{font-family: Lucida Console, Courier New, monospace;}span{color: red;}</style><body><h2>Acom will reboot in 5 seconds</h2></body></html>");
    reboot = 1;
  });

  server.begin();
}


void wificom() {
  String wifi_ssid1 = readStringFromEEPROM(0);
  String wifi_password1 = readStringFromEEPROM(50);
  String wifi_ssid2 = readStringFromEEPROM(100);
  String wifi_password2 = readStringFromEEPROM(150);
  String wifi_ssid3 = readStringFromEEPROM(200);
  String wifi_password3 = readStringFromEEPROM(250);

  if ((wifi_ssid1 == "" && wifi_ssid2 == "" && wifi_ssid1 == "") || secrets_mqtt_username == "" || secrets_mqtt_password == "" || secrets_user_uuid == "" || secrets_device_uuid == "") {
    Serial.begin(115200);
    wifisetting();
    return;
  }

  wifiMulti.addAP(wifi_ssid1.c_str(), wifi_password1.c_str());
  wifiMulti.addAP(wifi_ssid2.c_str(), wifi_password2.c_str());
  wifiMulti.addAP(wifi_ssid3.c_str(), wifi_password3.c_str());

  StickCP2.Display.fillScreen(BLACK);
  StickCP2.Display.setCursor(10, 10);
  StickCP2.Display.print("Wificom");
  StickCP2.Display.setCursor(15, 30);
  StickCP2.Display.print("Try connecting wifi.");

  if (wifiMulti.run() != WL_CONNECTED) {
    StickCP2.Display.pushImage(0, 0, 240, 135, wififail);
    delay(5000);
    return;
  }

  Serial.println(mqtt_io_prefix);
  Serial.println(mqtt_topic_identifier);
  Serial.println(mqtt_topic_input);
  Serial.println(mqtt_topic_output);
  prog_index = 1;
}

void acommode() {
  prog_index = 2;
  StickCP2.Display.pushImage(0, 0, 240, 135, acomusb);
}
void PunchingStart(String name, String code) {
  Serial.println(name);
  Serial.println(code);
  StickCP2.Display.pushImage(0, 0, 240, 135, punching);

  StickCP2.Display.setCursor(30, 8);
  StickCP2.Display.print("Punching BAG");
  StickCP2.Display.setTextWrap(1);
  StickCP2.Display.setCursor(30, 28);
  StickCP2.Display.print("Title  ");
  StickCP2.Display.setCursor(30, 48);
  StickCP2.Display.print(name);
  StickCP2.Display.setCursor(30, 68);
  StickCP2.Display.print("Digirom  ");
  StickCP2.Display.setCursor(30, 88);
  StickCP2.Display.print(code);
  StickCP2.Display.setTextWrap(0);

  BaseDigiROM* digiromA = nullptr;

  String btcode = code + "\n";
  const char* commandA = btcode.c_str();

  DigiROMType rom_type = digiROMType(commandA);
  switch (rom_type.signal_type) {
    case kSignalTypeV:
    case kSignalTypeX:
    case kSignalTypeY:
      digiromA = new ClassicDigiROM(commandA);
      break;
    case kSignalTypeC:
      digiromA = new WordsDigiROM(commandA);
      break;
    default:
      break;
  }
  while (PunchingMODE == 3) {
    if (digiromA != nullptr) {
      active_screen();
      StickCP2.Display.setTextColor(0xA800);
      StickCP2.Display.fillRect(0, 112, 240, 23, 0x15);
      StickCP2.Display.drawString("Execute Command", 51, 116);
      StickCP2.Display.setTextColor(0xFFFF);
      controller.execute(*digiromA, 3000);
      digiromA->printResult(Serial);
      Serial.println();
      if (digiromA->turn() == 1) {
        StickCP2.Display.fillRect(0, 112, 240, 23, 0x0);
        delay(3000);
      }
    } else {
      delay(DMCOMM_INACTIVE_DELAY_MILLIS);
    }


    StickCP2.update();
    if (StickCP2.BtnB.pressedFor(500)) {
      PunchingMODE = 2;
      time_bt = millis();
    }
    if (StickCP2.BtnA.pressedFor(500)) {
      active_screen();
      StickCP2.Display.fillScreen(BLACK);
      StickCP2.Display.setCursor(30, 55);
      StickCP2.Display.setTextSize(2);
      StickCP2.Display.print("RESTART..");
      delay(1000);
      exit_c = 1;
      ESP.restart();
    }
  }

  sub_punch_index = 0;
  old_sub_punch_index = 1;
  displaySubMenu();

  Serial.println("Punching END");
}
void mqttcallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message from ");
  Serial.println(topic);
  String msg = "";
  int i = 0;
  while (i < length) {
    msg += (char)payload[i++];
  }

  DynamicJsonDocument jdata(1024);
  deserializeJson(jdata, msg);
  mqtt_digirom = jdata["digirom"].as<String>();
  application_id = jdata["application_id"].as<String>();
  hide_output = boolean(jdata["hide_output"]);
  api_response = boolean(jdata["api_response"]);
  ack_id = jdata["ack_id"].as<String>();

  if (ack_id != "" && ack_id != "null") {
    delay(100);
    String digiout = "{\"name\":\"espcom\", \"board\":\"" + boardname + "\", \"version\":\"" + version + "\", \"device_uuid\": \"" + secrets_device_uuid + "\", \"ack_id\": \"" + ack_id + "\", \"application_uuid\": null}";
    client.publish(mqtt_topic_output.c_str(), digiout.c_str());
  }
  if (!hide_output) {
    Serial.print("receive ");
    Serial.println(msg);
  }
  delay(100);
  StickCP2.Display.setCursor(0, 115);
  StickCP2.Display.setTextColor(RED, GREEN);
  if (mqtt_digirom == "p" || mqtt_digirom == "P") {
    StickCP2.Display.setTextColor(0x57EA);
    StickCP2.Display.fillRect(0, 112, 240, 23, 0xA800);
    StickCP2.Display.drawString("PAUSE", 96, 116);
    StickCP2.Display.setTextColor(0xFFFF);
  } else {
    StickCP2.Display.setTextColor(0xA800);
    StickCP2.Display.fillRect(0, 112, 240, 23, 0x15);
    StickCP2.Display.drawString("Got New Command", 51, 116);
    StickCP2.Display.setTextColor(0xFFFF);
  }
  StickCP2.Display.setTextColor(WHITE, BLACK);
  StickCP2.Speaker.tone(8000, 20);
  delay(10);
  StickCP2.Speaker.tone(8000, 20);
  delay(10);
}
