#include <BLEDevice.h>
#include <BLEServer.h>
//#include <BLEUtils.h>
//#include <BLE2902.h>
#include <M5StickCPlus.h>
#include <driver/i2s.h>

//#include <Wire.h>


//change to unique BLE server name
#define bleServerName "CSC2106-BLE#2201141"
#define PIN_CLK 0
#define PIN_DATA 34
#define READ_LEN (2 * 256)
#define GAIN_FACTOR 3
#define MAX_READING 8000 * 10  // allows for 5 sec audio recording

uint8_t BUFFER[READ_LEN] = { 0 };
int16_t* adcBuffer = NULL;

char mode = '0'; // on by defualt
bool homeButtonDepressed = false;
bool resetButtonDepressed = false;

// Response Strings
const char* response1 = "0";
const char* response2 = "1";
const char* response3 = "3";
const char* response4 = "4";

void mic_record_task();
void i2sInit();

bool deviceConnected = false;

// See the following for generating UUIDs:
// https://www.uuidgenerator.net/
#define SERVICE_UUID "01234567-0123-4567-89ab-0123456789ab"

// response Characteristic and Descriptor
BLECharacteristic responseCharacteristics("01234567-0123-4567-89ab-0123456789cd", BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ);
BLEDescriptor responseDescriptor(BLEUUID((uint16_t)0x2902));

// Audio Characteristic and Descriptor
BLECharacteristic audioCharacteristics("01234567-0123-4567-89ab-01234567aaaa", BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ);
BLEDescriptor audioDescriptor(BLEUUID((uint16_t)0x2902));

// mode Char and Desc
BLECharacteristic modeCharacteristics("01234567-0123-4567-89ab-01234567ffff", BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_WRITE);
BLEDescriptor modeDescriptor(BLEUUID((uint16_t)0x2902));

//Setup callbacks onConnect and onDisconnect
class MyServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer, esp_ble_gatts_cb_param_t* param ) {
    deviceConnected = true;
    Serial.println("MyServerCallbacks::Connected...");
    // 7.5ms -10ms connection interval, 100 slave latency, 2s timeout 
    pServer->updateConnParams(param->connect.remote_bda, 6, 8, 100, 2000);
    // pServer->getAdvertising()->start();
  };
  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    Serial.println("MyServerCallbacks::Disconnected...");
    pServer->getAdvertising()->start();
  }
};

class MyLEDCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic, esp_ble_gatts_cb_param_t* param){
    mode = *(pCharacteristic->getData());
    Serial.printf("Mode callback: %c\t\n", mode);
  }
};

void setup() {
  // Start serial communication 
  Serial.begin(115200);
  pinMode(M5_BUTTON_HOME, INPUT);
  pinMode(M5_BUTTON_RST, INPUT);

  // put your setup code here, to run once:
  M5.begin();
  M5.Lcd.setRotation(3);
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(0, 0, 2);
  M5.Lcd.printf("BLE Server", 0);
  i2sInit();

  // BLE.setConnectionInterval(0x0006, 0x0c80); // 7.5 ms minimum, 4 s maximum

  // Create the BLE Device
  BLEDevice::init(bleServerName);

  // Create the BLE Server
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create the BLE Service
  BLEService *bleService = pServer->createService(SERVICE_UUID);

  // Create BLE Characteristics and Create a BLE Descriptor
  // Temperature
  bleService->addCharacteristic(&responseCharacteristics);
  responseDescriptor.setValue({0,1});
  responseCharacteristics.addDescriptor(&responseDescriptor);

  // Audio
  bleService->addCharacteristic(&audioCharacteristics);
  audioDescriptor.setValue({0,1});
  audioCharacteristics.addDescriptor(&audioDescriptor);
    
  // LED
  bleService->addCharacteristic(&modeCharacteristics);
  modeDescriptor.setValue({0,1});
  modeCharacteristics.addDescriptor(&modeDescriptor); 
  modeCharacteristics.setCallbacks(new MyLEDCallbacks()); 

  // Start the service
  bleService->start();

  // Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pServer->getAdvertising()->start();
  Serial.println("Waiting a client connection to notify...");
}


void mic_record_task() {
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.printf("Audio recording starting\n");
  size_t bytesread;
  uint32_t totalread = 0;
  void* AUDIO_BUFFER = malloc(MAX_READING + READ_LEN);
  memset(AUDIO_BUFFER, 0, MAX_READING + READ_LEN);
  M5.Lcd.printf("Audio assignment complete\n");
  while (totalread < MAX_READING) {
    i2s_read(I2S_NUM_0, (char*)BUFFER, READ_LEN, &bytesread,
             (100 / portTICK_RATE_MS));
    adcBuffer = (int16_t*)BUFFER;
    memcpy(AUDIO_BUFFER + totalread, BUFFER, bytesread);
    totalread += bytesread;
    // vTaskDelay(1);
  }
  M5.Lcd.printf("Audio recording complete %d\n", totalread);
  M5.Lcd.printf("sample at 0 is %d\n", *(int16_t*)AUDIO_BUFFER);
  unsigned long now = millis();
  // This takes between 100ms to 300ms
  // need to split up here
  size_t bytes_sent = 0;
  uint8_t send_buffer[500];
  while (bytes_sent < totalread){
    size_t to_copy = min(500, totalread - bytes_sent);
    memcpy(send_buffer, AUDIO_BUFFER + bytes_sent, to_copy);
    
    audioCharacteristics.setValue(send_buffer, to_copy);
    audioCharacteristics.notify();
    bytes_sent += to_copy;
    delay(30);

  }
  
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

void loop() {
  if (deviceConnected) {
    if (digitalRead(M5_BUTTON_HOME) == LOW && !homeButtonDepressed){
      homeButtonDepressed = true;
      if (mode == '2'){
        responseCharacteristics.setValue(response3);
        responseCharacteristics.notify();

        mic_record_task();
        
        responseCharacteristics.setValue(response4);
        responseCharacteristics.notify();
      }
      else {
        responseCharacteristics.setValue(response1);
        responseCharacteristics.notify();
      }
    }else if (digitalRead(M5_BUTTON_HOME) == HIGH) {
      homeButtonDepressed = false;
    }
    
    if (digitalRead(M5_BUTTON_RST) == LOW && !resetButtonDepressed) {
      resetButtonDepressed = true;
      responseCharacteristics.setValue(response2);
      responseCharacteristics.notify();
    } else if (digitalRead(M5_BUTTON_RST) == HIGH) {
      resetButtonDepressed = false;
    }
  }
}
