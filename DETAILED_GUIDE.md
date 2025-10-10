# DETAILED GUIDE - HIGH-QUALITY IMU MEASUREMENT MODULE

## 📋 PROJECT OVERVIEW

This project includes 4 different IMU modules running on ESP32-C6, each with unique characteristics and applications:

### 🎯 IMU Sensors Used:

1. **IIS2MDC** - 3-axis Magnetometer
2. **IIS3DWB** - High-speed 3-axis Accelerometer
3. **ICM45686** - 6-axis IMU (Accelerometer + Gyroscope) with APEX features
4. **SCL3300** - 3-axis Inclinometer

---

## 🔧 CIRCUIT AND HARDWARE ANALYSIS

### PCB Architecture:
- **MCU**: ESP32-C6 (WiFi 6, Bluetooth 5.0)
- **Communication**: I2C for IIS2MDC, SPI for other sensors
- **Power**: 3.3V, can be powered via USB or external battery
- **GPIO**: Optimally configured for each sensor

### Connection Diagram:

```
ESP32-C6
├── I2C Bus (SDA: GPIO23, SCL: GPIO22)
│   └── IIS2MDC (Magnetometer) - Addr: 0x1E
├── SPI Bus 1 (MISO: GPIO2, MOSI: GPIO7, CLK: GPIO6, CS: GPIO19)
│   └── IIS3DWB (Accelerometer) - CS: GPIO19
├── SPI Bus 2 (MISO: GPIO19, MOSI: GPIO23, CLK: GPIO18, CS: GPIO5)
│   └── ICM45686 (6-axis IMU) - CS: GPIO5, INT: GPIO4
└── SPI Bus 3 (MISO: GPIO2, MOSI: GPIO7, CLK: GPIO6, CS: GPIO20)
    └── SCL3300 (Inclinometer) - CS: GPIO20
```

---

## 📊 SENSOR CHARACTERISTICS

### 1. IIS2MDC - Magnetometer
- **Function**: 3-axis magnetic field measurement + temperature
- **Communication**: I2C (400kHz)
- **Resolution**: 1.5 mG/LSB
- **Sampling Rate**: 100Hz (adjustable)
- **Applications**: Digital compass, metal detection, orientation

### 2. IIS3DWB - High-speed Accelerometer
- **Function**: 3-axis acceleration measurement with FIFO
- **Communication**: SPI (high speed)
- **Sampling Rate**: 26.7kHz (maximum)
- **FIFO**: 32 samples watermark
- **Applications**: Vibration detection, motion analysis

### 3. ICM45686 - 6-axis IMU with APEX
- **Function**: Accelerometer + Gyroscope + APEX features
- **Communication**: SPI with DMA
- **Sampling Rate**: 100Hz (adjustable)
- **APEX features**: Tilt detection, Pedometer, Tap detection, Raise-to-wake
- **Applications**: IoT wearable, fitness tracking, gesture recognition

### 4. SCL3300 - Inclinometer
- **Function**: 3-axis tilt measurement + acceleration + temperature
- **Communication**: SPI with CRC protection
- **Accuracy**: ±0.1° (tilt angle)
- **Modes**: 4 different modes
- **Applications**: Tilt measurement, balance, structural monitoring

---

## 💻 CODE USAGE GUIDE

### Environment Setup:

1. **Install ESP-IDF v5.4+**:
```bash
# Clone ESP-IDF
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
git checkout v5.4
./install.sh esp32c6
. ./export.sh
```

2. **Clone Project**:
```bash
git clone <repository-url>
cd ESP32_Vibra_Accel_inclio_Module
```

### Build and Flash:

```bash
# Select project to build
cd ESP32C6_IIS2MDC  # or other project

# Configure
idf.py menuconfig

# Build
idf.py build

# Flash
idf.py flash monitor
```

### GPIO Configuration (if changes needed):

Open `main.c` file and modify the defines:
```c
// IIS2MDC
#define I2C_MASTER_SDA          23
#define I2C_MASTER_SCL          22

// IIS3DWB
#define PIN_NUM_MISO  2
#define PIN_NUM_MOSI  7
#define PIN_NUM_CLK   6
#define PIN_NUM_CS    19

// ICM45686
#define PIN_NUM_MISO       19
#define PIN_NUM_MOSI       23
#define PIN_NUM_CLK        18
#define PIN_NUM_CS         5
#define PIN_NUM_INT        4

// SCL3300
#define PIN_NUM_CS         20
```

---

## 🚀 PERFORMANCE OPTIMIZATION

### 1. I2C Optimization:
- Use 400kHz speed for IIS2MDC
- Enable internal pullup
- Use DMA if available

### 2. SPI Optimization:
- Use DMA for all SPI transactions
- Optimize buffer size
- Use smart FIFO watermark

### 3. Task Optimization:
- Use FreeRTOS tasks with appropriate priority
- Implement interrupt-driven data collection
- Use semaphores for synchronization

### 4. Memory Optimization:
- Use PSRAM if available
- Optimize stack size for tasks
- Use static allocation when possible

---

## 📈 MONITORING AND DEBUG

### Logging:
```c
// Enable verbose logging
esp_log_level_set("*", ESP_LOG_DEBUG);

// Sensor-specific logging
ESP_LOGI("SENSOR", "Data: X=%.2f, Y=%.2f, Z=%.2f", x, y, z);
```

### Performance monitoring:
```c
// Measure execution time
int64_t start_time = esp_timer_get_time();
// ... sensor operations ...
int64_t end_time = esp_timer_get_time();
ESP_LOGI("PERF", "Operation took %lld us", end_time - start_time);
```

---

## 🔧 TROUBLESHOOTING

### Common Issues:

1. **I2C timeout**:
   - Check SDA/SCL connections
   - Check pullup resistors
   - Reduce I2C speed

2. **SPI communication error**:
   - Check MISO/MOSI/CLK/CS connections
   - Check SPI mode
   - Check clock speed

3. **FIFO overflow**:
   - Increase FIFO read frequency
   - Reduce watermark
   - Optimize processing time

4. **Memory issues**:
   - Increase heap size
   - Use PSRAM
   - Optimize buffer sizes

---

## 📚 REFERENCE DOCUMENTATION

### Datasheets:
- [IIS2MDC Datasheet](https://www.st.com/resource/en/datasheet/iis2mdc.pdf)
- [IIS3DWB Datasheet](https://www.st.com/resource/en/datasheet/iis3dwb.pdf)
- [ICM45686 Datasheet](https://invensense.tdk.com/products/motion-tracking/6-axis/icm-45686/)
- [SCL3300 Datasheet](https://www.murata.com/en-global/products/sensor/inclinometer/overview/lineup/scl3300)

### ESP-IDF Documentation:
- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/)
- [I2C Driver](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/api-reference/peripherals/i2c.html)
- [SPI Driver](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/api-reference/peripherals/spi_master.html)

---

## 🎯 REAL-WORLD APPLICATIONS

### 1. Vibration Monitoring System:
- Use IIS3DWB for vibration detection
- High frequency (26.7kHz) for high accuracy
- FIFO to prevent data loss

### 2. Digital Compass:
- Use IIS2MDC for magnetometer
- Combine with accelerometer for orientation calculation
- Automatic temperature compensation

### 3. Fitness Tracker:
- Use ICM45686 with APEX features
- Pedometer, tilt detection
- Low power mode

### 4. Industrial Tilt Measurement:
- Use SCL3300 for high accuracy
- 4 different operating modes
- CRC protection for reliability

---

## 🔮 FUTURE DEVELOPMENT

### Features to Add:
1. **Web-based monitoring** with real-time charts
2. **Machine Learning** for pattern recognition
3. **Cloud connectivity** with AWS IoT/Azure
4. **Mobile app** for remote control
5. **Data logging** with SD card
6. **Multi-sensor fusion** algorithms

### Optimizations:
1. **Power management** for battery operation
2. **Sleep modes** for energy saving
3. **Edge computing** for real-time processing
4. **Wireless updates** (OTA)

---

## 💰 SUPPORT & PURCHASE

### Buy IMU Kit:
**Order**: [HBQ Technology Store](https://store.hbqsolution.com/)

**Contact Information**:
- Email: contact@hbqsolution.com | hbqsolution@gmail.com
- Phone: (+84) 035 719 1643 | (+84) 094 850 7979
- Address: 31, Đường số 8, Cityland Garden Hill, P. An Nhơn, TP HCM

### Support the Project:
- **PayPal Donate**: [Donate via PayPal](https://paypal.me/hbqtechnology)
- **GitHub Sponsors**: [Support on GitHub](https://github.com/sponsors/hbqtechnologycompany)

### Community:
- **GitHub Issues**: Report bugs and request features
- **Discussions**: Join community discussions
- **Documentation**: Contribute to documentation

---

*This guide is regularly updated. Please check the repository for the latest information.*
