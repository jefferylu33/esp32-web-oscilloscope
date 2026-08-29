#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLE2902.h>

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

#define SAMPLES_PER_PACKET 120
#define ADC_PIN 34 
const float DIVIDER_RATIO = (100000.0 + 20000.0) / 20000.0; // 6.0

BLECharacteristic *pCharacteristic;
bool deviceConnected = false;
uint16_t packet_index = 0;

// 定義二進位封包結構
#pragma pack(push, 1)
struct WaveformPacket {
  uint16_t header = 0x55AA;
  uint16_t index;
  uint16_t count;
  uint16_t samples[SAMPLES_PER_PACKET];
} packet;
#pragma pack(pop)

// 藍芽回呼
class ServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) { 
    deviceConnected = true; 
    Serial.println("\n>>> [BLE] 手機網頁已成功連線！開始高速串流傳輸 <<<");
  }
  void onDisconnect(BLEServer* pServer) { 
    deviceConnected = false;
    Serial.println("\n>>> [BLE] 手機已斷線，重新等待連線中... <<<");
    pServer->getAdvertising()->start(); 
  }
};

// 序列埠更新計時器變數
unsigned long lastSerialPrint = 0;
const unsigned long SERIAL_INTERVAL = 500; // 每 500ms 刷新一次序列埠資訊

void setup() {
  Serial.begin(115200);
  
  analogReadResolution(12);
  analogSetPinAttenuation(ADC_PIN, ADC_11db);

  BLEDevice::init("ESP32_Oscilloscope");
  BLEDevice::setMTU(512);

  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_NOTIFY
                    );
  pCharacteristic->addDescriptor(new BLE2902());
  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->start();

  Serial.println("==================================================");
  Serial.println("  ESP32 機車電壓即時示波器 - 監控模式已啟動");
  Serial.println("==================================================");
}

void loop() {
  unsigned long sampleStartMicros = micros();

  // 1. 高速連續採樣 120 點
  for (int i = 0; i < SAMPLES_PER_PACKET; i++) {
    packet.samples[i] = (uint16_t)analogReadMilliVolts(ADC_PIN);
    delayMicroseconds(100); // 100µs 間隔
  }

  unsigned long sampleEndMicros = micros();
  unsigned long samplingDuration = sampleEndMicros - sampleStartMicros; // 採樣 120 點耗時 (µs)

  // 2. 若 BLE 已連線則發送 Notify 封包
  if (deviceConnected) {
    packet.index = packet_index++;
    packet.count = SAMPLES_PER_PACKET;

    pCharacteristic->setValue((uint8_t*)&packet, sizeof(packet));
    pCharacteristic->notify();
  }

  // 3. 定時於序列埠視窗刷新數據 (不阻塞高速採樣)
  unsigned long currentMillis = millis();
  if (currentMillis - lastSerialPrint >= SERIAL_INTERVAL) {
    lastSerialPrint = currentMillis;

    // 取最後一個採樣點計算即時電壓
    uint16_t lastRawMv = packet.samples[SAMPLES_PER_PACKET - 1];
    float pinVoltage = lastRawMv / 1000.0;
    float batteryVoltage = pinVoltage * DIVIDER_RATIO;

    // 格式化輸出
    Serial.printf("[%08lu ms] | 電瓶電壓: %5.2f V | ADC腳位: %4.2f V | 採樣120點耗時: %5lu µs | 藍芽狀態: %s\n",
                  currentMillis,
                  batteryVoltage,
                  pinVoltage,
                  samplingDuration,
                  deviceConnected ? "已連線 (傳輸中)" : "等待連線");
  }
}