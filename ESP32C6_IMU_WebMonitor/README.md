# ESP32-C6 IMU Web Monitor

> [VI] Ứng dụng web giám sát IMU realtime trên ESP32‑C6. Dashboard HTML/JS tích hợp, stream dữ liệu qua WebSocket `/ws/data`. Các mục song ngữ có nhãn [VI].

A high-performance web-based monitoring system for multiple IMU sensors with real-time data visualization and control capabilities.

## 🌟 Features

[VI] Tính năng

- **Real-time Web Interface**: Live data visualization with charts and graphs
- **Multi-Sensor Support**: Simultaneous monitoring of 4 different IMU sensors
- **High-Speed Data Collection**: Up to 26.7kHz sampling rate with FIFO buffering
- **REST API**: JSON endpoints for data access and configuration
- **WebSocket Support**: Real-time data streaming to web clients
- **Data Export**: CSV and JSON download capabilities
- **Remote Configuration**: Web-based sensor configuration interface
- **Performance Monitoring**: Built-in statistics and performance metrics

## 🎯 Supported Sensors

| Sensor | Type | Max Sample Rate | Features |
|--------|------|-----------------|----------|
| IIS2MDC | Magnetometer | 100Hz | Temperature compensation, I2C |
| IIS3DWB | High-speed Accelerometer | 26.7kHz | FIFO, SPI, Vibration analysis |
| ICM45686 | 6-axis IMU | 100Hz | APEX features, Gesture recognition |
| SCL3300 | Inclinometer | 1kHz | CRC protection, 4 modes |

## 🚀 Quick Start

[VI] Bắt đầu nhanh

### Prerequisites
- ESP-IDF v5.4 or later
- ESP32-C6 development board
- Required IMU sensors
- WiFi network access

### Installation

1. **Clone and setup**:
```bash
git clone https://github.com/hbqtechnologycompany/ESP32-C6-Multi-Sensor-IMU-Module.git
cd ESP32-C6-Multi-Sensor-IMU-Module/ESP32C6_IMU_WebMonitor
```

2. **Configure WiFi**:
Edit `main/main.c` and update WiFi credentials:
```c
#define WIFI_SSID      "YOUR_WIFI_SSID"
#define WIFI_PASS      "YOUR_WIFI_PASSWORD"
```

3. **Build and flash**:
```bash
idf.py build
idf.py flash monitor
```

4. **Access web interface**:
Open browser and navigate to the ESP32's IP address (check serial monitor for IP)

## 🌐 Web Interface

[VI] Giao diện Web

### Dashboard Features
- **Real-time Charts**: Live visualization of sensor data
- **Multi-sensor Display**: Simultaneous monitoring of all sensors
- **Data Statistics**: Performance metrics and buffer statistics
- **Configuration Panel**: Remote sensor configuration
- **Data Export**: Download data in CSV or JSON format
- **WebSocket Streaming**: Low-latency push via `ws://<device-ip>/ws/data`

### API Endpoints

[VI] API

#### Data Access
- `GET /api/data` - Get latest sensor readings
- `GET /api/stats` - Get buffer and performance statistics
- `GET /api/download?format=csv` - Download data as CSV
- `GET /api/download?format=json` - Download data as JSON

#### Realtime Streaming
- `WS /ws/data` - WebSocket endpoint publishing compact JSON frames at ~50 Hz

Example message:
```json
{"t": 1234567890, "acc": {"x": 0.01, "y": -0.02, "z": 0.98}, "gyr": {"x": 0.1, "y": 0.0, "z": -0.1}}
```

#### Configuration
- `GET /api/config` - Get current configuration
- `POST /api/config` - Update configuration

Example configuration update:
```json
{
  "sampling_rate": 100,
  "fifo_watermark": 32,
  "enabled_sensors": {
    "magnetometer": true,
    "accelerometer": true,
    "imu_6axis": true,
    "inclinometer": true
  }
}
```

## 🔧 Configuration

[VI] Cấu hình

### Hardware Configuration

Update GPIO pins in `main/main.c` if needed:

```c
// IIS2MDC (I2C)
#define I2C_MASTER_SDA          23
#define I2C_MASTER_SCL          22

// IIS3DWB (SPI)
#define PIN_NUM_MISO_1          2
#define PIN_NUM_MOSI_1          7
#define PIN_NUM_CLK_1           6
#define PIN_NUM_CS_1            19

// ICM45686 (SPI)
#define PIN_NUM_MISO_2          19
#define PIN_NUM_MOSI_2          23
#define PIN_NUM_CLK_2           18
#define PIN_NUM_CS_2            5
#define PIN_NUM_INT_2           4

// SCL3300 (SPI)
#define PIN_NUM_MISO_3          2
#define PIN_NUM_MOSI_3          7
#define PIN_NUM_CLK_3           6
#define PIN_NUM_CS_3            20
```

### Software Configuration

#### Sampling Rates
- **Magnetometer**: 100Hz (adjustable)
- **Accelerometer**: 26.7kHz (maximum)
- **IMU 6-axis**: 100Hz (adjustable)
- **Inclinometer**: 1kHz (adjustable)

#### Buffer Configuration
- **Buffer Size**: 1000 samples (configurable)
- **FIFO Watermark**: 32 samples (configurable)
- **Overwrite Mode**: Enabled (prevents data loss)

## 📊 Performance Optimization

[VI] Tối ưu hiệu năng

### High-Speed Data Collection
- **DMA Usage**: All SPI transactions use DMA
- **FIFO Management**: Smart watermark configuration
- **Task Scheduling**: Optimized FreeRTOS task priorities
- **Memory Management**: Efficient circular buffer implementation

### Web Server Optimization
- **Chunked Transfer**: Efficient data streaming
- **Compression**: Gzip compression for static files
- **Caching**: Browser caching for static resources
- **WebSocket**: Real-time data streaming

## 🔍 Monitoring and Debugging

[VI] Giám sát & Gỡ lỗi

### Built-in Statistics
- Total samples collected
- Dropped samples count
- Buffer overflow events
- Average processing time
- Memory usage statistics

### Logging
Enable debug logging:
```c
esp_log_level_set("*", ESP_LOG_DEBUG);
```

### Performance Monitoring
```c
// Measure operation time
int64_t start_time = esp_timer_get_time();
// ... operation ...
int64_t end_time = esp_timer_get_time();
ESP_LOGI("PERF", "Operation took %lld us", end_time - start_time);
```

## 🚀 Quick Start for Web Dashboard

[VI] Bắt đầu nhanh với Dashboard Web

1. Flash firmware and connect ESP32-C6 to your WiFi.
2. Open a browser and navigate to `http://<device-ip>/`.
3. The built-in HTML/JS dashboard (served from firmware) will render a realtime accelerometer chart using WebSocket.
4. Advanced UI can be served from SPIFFS by uploading your own `index.html` to `/spiffs`.

## 🛠️ Troubleshooting

### Common Issues

1. **WiFi Connection Failed**
   - Check WiFi credentials
   - Verify signal strength
   - Check firewall settings

2. **Sensor Initialization Failed**
   - Check wiring connections
   - Verify GPIO configuration
   - Check power supply

3. **Web Interface Not Loading**
   - Check IP address in serial monitor
   - Verify SPIFFS partition
   - Check web server status

4. **High CPU Usage**
   - Reduce sampling rates
   - Optimize data processing
   - Check task priorities

### Debug Commands

Monitor system status:
```bash
# Check free heap
idf.py monitor

# Check task status
# Look for task stack usage in logs

# Check WiFi status
# Look for IP address in startup logs
```

## 📈 Data Analysis

### Real-time Visualization
The web interface provides:
- **Time-series Charts**: Live sensor data plotting
- **Multi-axis Display**: Simultaneous sensor monitoring
- **Statistical Analysis**: Mean, variance, peak detection
- **Frequency Analysis**: FFT for vibration analysis

### Data Export
- **CSV Format**: Compatible with Excel, MATLAB, Python
- **JSON Format**: Structured data for web applications
- **Real-time Streaming**: WebSocket for live data

## 🔮 Future Enhancements

### Planned Features
- **Machine Learning**: Pattern recognition and anomaly detection
- **Cloud Integration**: AWS IoT, Azure IoT Hub support
- **Mobile App**: Native mobile application
- **Advanced Analytics**: Statistical analysis and reporting
- **Multi-device Support**: Network of synchronized sensors

### Performance Improvements
- **Edge Computing**: On-device data processing
- **Compression**: Data compression for storage
- **Caching**: Intelligent data caching
- **Load Balancing**: Multiple web server instances

## 💰 Support & Purchase

### Buy the CM5 Gateway Kit
**Store**: [HBQ Technology Store](https://store.hbqsolution.com/)

**Contact Information**:
- Email: contact@hbqsolution.com | hbqsolution@gmail.com
- Phone: (+84) 035 719 1643 | (+84) 094 850 7979
- Address: 31, Đường số 8, Cityland Garden Hill, P. An Nhơn, TP HCM

### Support the Project
- **PayPal Donate**: [Donate via PayPal](https://paypal.me/hbqtechnology)
- **GitHub Sponsors**: [Support on GitHub](https://github.com/sponsors/hbqtechnologycompany)

## 📚 Documentation

- [Main Project README](../README.md)
- [Detailed Guide](../DETAILED_GUIDE.md)
- [API Documentation](docs/API.md)
- [Hardware Guide](docs/HARDWARE.md)

## 🤝 Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Add tests if applicable
5. Submit a pull request

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](../LICENSE) file for details.

---

**Built for high-performance IMU monitoring with modern web technologies**
