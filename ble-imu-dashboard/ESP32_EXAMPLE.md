# ESP32 Example Code (Arduino/PlatformIO)

Đây là code mẫu cho ESP32-C6 để stream IMU data qua BLE.

## File: `esp32_imu_ble_server.ino`

```cpp
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// BLE Service & Characteristic UUIDs (tự generate hoặc dùng standard)
#define SERVICE_UUID        "fff0"  // Custom service
#define CHARACTERISTIC_UUID "fff1"  // Notify characteristic

BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;

// Callback khi có client connect/disconnect
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        deviceConnected = true;
        Serial.println("Client connected");
    };

    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
        Serial.println("Client disconnected");
    }
};

void setup() {
    Serial.begin(115200);
    Serial.println("Starting BLE IMU Server...");

    // Tạo BLE Device với tên
    BLEDevice::init("ESP32-IMU-Sensor");

    // Tạo BLE Server
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    // Tạo BLE Service
    BLEService *pService = pServer->createService(SERVICE_UUID);

    // Tạo BLE Characteristic với thuộc tính NOTIFY
    pCharacteristic = pService->createCharacteristic(
                        CHARACTERISTIC_UUID,
                        BLECharacteristic::PROPERTY_READ   |
                        BLECharacteristic::PROPERTY_NOTIFY
                      );

    // Add descriptor cho Notify
    pCharacteristic->addDescriptor(new BLE2902());

    // Start service
    pService->start();

    // Start advertising
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(false);
    pAdvertising->setMinPreferred(0x0);
    BLEDevice::startAdvertising();
    
    Serial.println("BLE Server started, waiting for connections...");
}

void loop() {
    // Nếu có client connect
    if (deviceConnected) {
        // Fake IMU data (thay bằng đọc từ sensor thật)
        float x = sin(millis() / 1000.0) * 100.0;  // -100 to +100
        float y = cos(millis() / 1000.0) * 100.0;
        float z = sin(millis() / 500.0) * 50.0;    // -50 to +50

        // Pack 3 float32 vào buffer (12 bytes, little-endian)
        uint8_t data[12];
        memcpy(&data[0], &x, 4);
        memcpy(&data[4], &y, 4);
        memcpy(&data[8], &z, 4);

        // Send qua BLE Notify
        pCharacteristic->setValue(data, 12);
        pCharacteristic->notify();

        Serial.printf("Sent: X=%.2f Y=%.2f Z=%.2f\n", x, y, z);

        delay(50);  // 20 Hz sampling rate
    }

    // Reconnect logic
    if (!deviceConnected && oldDeviceConnected) {
        delay(500);
        pServer->startAdvertising();
        Serial.println("Start advertising again...");
        oldDeviceConnected = deviceConnected;
    }
    
    if (deviceConnected && !oldDeviceConnected) {
        oldDeviceConnected = deviceConnected;
    }
}
```

---

## Với IIS3DWB (Real sensor):

```cpp
#include <Wire.h>
#include "SparkFun_LIS3DH.h"  // hoặc library phù hợp

LIS3DH myIMU;

void loop() {
    if (deviceConnected) {
        // Đọc accelerometer
        float x = myIMU.readFloatAccelX();  // g units
        float y = myIMU.readFloatAccelY();
        float z = myIMU.readFloatAccelZ();

        // Pack và send
        uint8_t data[12];
        memcpy(&data[0], &x, 4);
        memcpy(&data[4], &y, 4);
        memcpy(&data[8], &z, 4);

        pCharacteristic->setValue(data, 12);
        pCharacteristic->notify();

        delay(20);  // 50 Hz
    }
}
```

---

## Format với Sensor ID:

```cpp
void sendSensorData(uint8_t sensorID, float x, float y, float z) {
    uint8_t data[13];
    data[0] = sensorID;  // 0x01 = IIS3DWB, 0x02 = ICM45686, etc.
    memcpy(&data[1], &x, 4);
    memcpy(&data[5], &y, 4);
    memcpy(&data[9], &z, 4);
    
    pCharacteristic->setValue(data, 13);
    pCharacteristic->notify();
}

// Usage:
sendSensorData(0x01, accel_x, accel_y, accel_z);
```

---

## PlatformIO config:

```ini
[env:esp32c6]
platform = espressif32
board = esp32-c6-devkitc-1
framework = arduino
monitor_speed = 115200
lib_deps = 
    h2zero/NimBLE-Arduino@^1.4.0
```

---

✅ Upload code này lên ESP32, rồi dùng dashboard để connect & visualize!
