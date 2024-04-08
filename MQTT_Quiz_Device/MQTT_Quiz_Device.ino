/*
*******************************************************************************
* Copyright (c) 2021 by M5Stack
*                  Equipped with M5StickC-Plus sample source code
*                             M5StickC-Plus
* Visit for more information: https://docs.m5stack.com/en/core/m5stickc_plus
*
* Describe: MQTT
* Date: 2021/11/5
*******************************************************************************
*/
#include "M5StickCPlus.h"
#include <WiFi.h>
#include <driver/i2s.h>
#include <MQTT.h>

#define PIN_CLK 0
#define PIN_DATA 34
#define READ_LEN (2 * 256)
#define GAIN_FACTOR 3
#define MAX_READING 8000 * 10  // allows for 5 sec audio recording

int16_t* adcBuffer = NULL;

WiFiClient espClient;
MQTTClient client;

// Configure the name and password of the connected wifi and your MQTT Serve
// host.
const char* ssid = "MJ_MkIV";
const char* password = "NekoGirls";
const char* mqtt_server = "192.168.43.120";

const char* nodeID = "2201411";
const char* statusTopic = "2201411/status";
const char* responseTopic = "2201411/responses";
const char* audioTopic = "2201411/audio";
// Response Strings
const char* response1 = "0";
const char* response2 = "1";
const char* response3 = "3";
const char* response4 = "4";

uint8_t BUFFER[READ_LEN] = { 0 };
bool homeButtonDepressed = false;
bool resetButtonDepressed = false;
char mode = '0';
unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE (50)
char msg[MSG_BUFFER_SIZE];
int value = 6;

void mic_record_task();
void i2sInit();
void setupWifi();
void callback(MQTTClient* client, char topic[], char bytes[], int length);
void reConnect();

void setup() {
  M5.begin();
  M5.Lcd.setRotation(3);
  pinMode(M5_BUTTON_HOME, INPUT);
  pinMode(M5_BUTTON_RST, INPUT);
  // pinMode(M5_LED, OUTPUT);
  // digitalWrite(M5_LED, 0);

  setupWifi();
  i2sInit();
  client.begin(mqtt_server, espClient);  // Sets the server details.
  client.onMessageAdvanced(callback);    // Sets the message callback function.
}

void loop() {
  if (!client.connected()) {
    reConnect();
  }
  client.loop();  // This function is called periodically to allow clients to
                  // process incoming messages and maintain connections to the
                  // server.

  if (digitalRead(M5_BUTTON_HOME) == LOW && !homeButtonDepressed) {
    homeButtonDepressed = true;
    if (mode == '2'){
      mic_record_task();
    }
    client.publish(responseTopic, response1, false, 1);
  } else if (digitalRead(M5_BUTTON_HOME) == HIGH) {
    homeButtonDepressed = false;
  }

  if (digitalRead(M5_BUTTON_RST) == LOW && !resetButtonDepressed) {
    resetButtonDepressed = true;
    client.publish(responseTopic, response2, false, 1);
  } else if (digitalRead(M5_BUTTON_RST) == HIGH) {
    resetButtonDepressed = false;
  }
}

void mic_record_task() {
  client.loop();
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.printf("Audio recording starting\n");
  size_t bytesread;
  uint32_t totalread = 0;
  void* AUDIO_BUFFER = malloc(MAX_READING + READ_LEN);
  memset(AUDIO_BUFFER, 0, MAX_READING + READ_LEN);
  M5.Lcd.printf("Audio assignment complete\n");
  client.loop();
  while (totalread < MAX_READING) {
    i2s_read(I2S_NUM_0, (char*)BUFFER, READ_LEN, &bytesread,
             (100 / portTICK_RATE_MS));
    adcBuffer = (int16_t*)BUFFER;
    memcpy(AUDIO_BUFFER + totalread, BUFFER, bytesread);
    totalread += bytesread;
    client.loop();
    // vTaskDelay(1);
  }
  M5.Lcd.printf("Audio recording complete %d\n", totalread);
  M5.Lcd.printf("sample at 0 is %d\n", *(int16_t*)AUDIO_BUFFER);
  unsigned long now = millis();
  // This takes between 100ms to 300ms
  client.loop();
  client.publish(audioTopic, (char*)AUDIO_BUFFER, totalread);
  M5.Lcd.printf("it takes %dms to send data\n", millis() - now);
  free(AUDIO_BUFFER);
}

void i2sInit() {
  // Sample rate is the audio sample rate, 8k for speech minimum
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM),
    .sample_rate = 8000,
    .bits_per_sample =
      I2S_BITS_PER_SAMPLE_16BIT,  // is fixed at 12bit, stereo, MSB
    .channel_format = I2S_CHANNEL_FMT_ALL_RIGHT,
#if ESP_IDF_VERSION > ESP_IDF_VERSION_VAL(4, 1, 0)
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
#else
    .communication_format = I2S_COMM_FORMAT_I2S,
#endif
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 2,
    .dma_buf_len = 128,
  };

  i2s_pin_config_t pin_config;

#if (ESP_IDF_VERSION > ESP_IDF_VERSION_VAL(4, 3, 0))
  pin_config.mck_io_num = I2S_PIN_NO_CHANGE;
#endif

  pin_config.bck_io_num = I2S_PIN_NO_CHANGE;
  pin_config.ws_io_num = PIN_CLK;
  pin_config.data_out_num = I2S_PIN_NO_CHANGE;
  pin_config.data_in_num = PIN_DATA;

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
  i2s_set_clk(I2S_NUM_0, 8000, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
}

void setupWifi() {
  delay(10);
  M5.Lcd.printf("Connecting to %s", ssid);
  WiFi.mode(WIFI_STA);         // Set the mode to WiFi station mode.
  WiFi.begin(ssid, password);  // Start Wifi connection.

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    M5.Lcd.print(".");
  }
  M5.Lcd.printf("\nSuccess\n");
}

void callback(MQTTClient* client, char topic[], char bytes[], int length) {
  // Only listening to mode, so no need to check topic
  if (length < 1) return;
  mode = bytes[0];
}

void reConnect() {
  while (!client.connected()) {
    M5.Lcd.print("Attempting MQTT connection...");
    // Attempt to connect.
    client.setWill(statusTopic, "0", true, 1);
    if (client.connect(nodeID, "user", "passwd")) {
      M5.Lcd.printf("\nSuccess\n");
      // Once connected, publish an announcement to the topic.
      // client.sessionPresent(); // need check if i disconnected someone
      client.publish(statusTopic, "1", true, 1);
      client.subscribe("mode");
    } else {
      M5.Lcd.print("failed, rc=");
      // M5.Lcd.print(client.state());
      M5.Lcd.println("try again in 5 seconds");
      delay(5000);
    }
  }
}