# ESP32-C6 IMU Measurement Module

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)](https://github.com/YOUR_USERNAME/ESP32_Vibra_Accel_inclio_Module)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.4-blue.svg)](https://docs.espressif.com/projects/esp-idf/en/latest/)
[![Platform](https://img.shields.io/badge/platform-ESP32--C6-red.svg)](https://www.espressif.com/en/products/socs/esp32-c6)

[![Donate via PayPal](https://img.shields.io/badge/Donate-PayPal-blue.svg)](https://paypal.me/hbqtechnology)
[![GitHub Sponsors](https://img.shields.io/github/sponsors/hbqtechnologycompany?style=social)](https://github.com/sponsors/hbqtechnologycompany)

A comprehensive high-quality IMU measurement system with web-based monitoring capabilities, featuring multiple sensors for various applications.

## 🎯 Features

- **Multi-Sensor Support**: IIS2MDC (Magnetometer), IIS3DWB (High-speed Accelerometer), ICM45686 (6-axis IMU), SCL3300 (Inclinometer)
- **High-Speed Data Collection**: Up to 26.7kHz sampling rate
- **Web-Based Monitoring**: Real-time data visualization and control
- **FIFO Buffering**: Prevents data loss during high-speed operations
- **APEX Features**: Advanced motion detection and gesture recognition
- **Multiple Communication Protocols**: I2C and SPI support
- **ESP32-C6 Platform**: WiFi 6 and Bluetooth 5.0 connectivity

## 📊 Sensor Specifications

| Sensor | Type | Communication | Max Sample Rate | Features |
|--------|------|---------------|-----------------|----------|
| IIS2MDC | Magnetometer | I2C | 100Hz | Temperature compensation |
| IIS3DWB | Accelerometer | SPI | 26.7kHz | FIFO, High-speed |
| ICM45686 | 6-axis IMU | SPI | 100Hz | APEX features, Gesture recognition |
| SCL3300 | Inclinometer | SPI | 1kHz | CRC protection, 4 modes |

## 🚀 Quick Start

### Prerequisites
- ESP-IDF v5.4 or later
- ESP32-C6 development board
- Required sensors (IIS2MDC, IIS3DWB, ICM45686, SCL3300)

### Installation

1. **Clone the repository**:
```bash
git clone https://github.com/YOUR_USERNAME/ESP32_Vibra_Accel_inclio_Module.git
cd ESP32_Vibra_Accel_inclio_Module
```

2. **Set up ESP-IDF**:
```bash
# Install ESP-IDF v5.4
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
git checkout v5.4
./install.sh esp32c6
. ./export.sh
```

3. **Build and flash**:
```bash
cd ESP32C6_IMU_WebMonitor
idf.py build
idf.py flash monitor
```

## 📁 Project Structure

```
ESP32_Vibra_Accel_inclio_Module/
├── ESP32C6_IIS2MDC/          # Magnetometer example
├── ESP32C6_IIS3DWBTR/        # High-speed accelerometer example
├── icm45686/                 # 6-axis IMU with APEX features
├── SCL3300/                  # Inclinometer example
├── ESP32C6_IMU_WebMonitor/   # Web-based monitoring system
├── DETAILED_GUIDE.md         # Comprehensive documentation
└── README.md                 # This file
```

## 🌐 Web Monitoring Interface

The web monitoring system provides:
- **Real-time Data Visualization**: Live charts and graphs
- **REST API**: JSON endpoints for data access
- **WebSocket Support**: Real-time data streaming
- **Configuration Interface**: Remote sensor configuration
- **Data Export**: CSV and JSON download options

### API Endpoints

- `GET /api/data` - Latest sensor data
- `GET /api/stats` - Buffer statistics
- `POST /api/config` - Configuration changes
- `GET /api/download?format=csv` - Data export

## 🔧 Configuration

### GPIO Configuration

| Sensor | SDA/SDI | SCL/SCK | CS | INT |
|--------|---------|---------|----|----|
| IIS2MDC | GPIO23 | GPIO22 | - | - |
| IIS3DWB | GPIO7 | GPIO6 | GPIO19 | - |
| ICM45686 | GPIO23 | GPIO18 | GPIO5 | GPIO4 |
| SCL3300 | GPIO7 | GPIO6 | GPIO20 | - |

### WiFi Configuration

Update WiFi credentials in `main.c`:
```c
#define WIFI_SSID      "YOUR_WIFI_SSID"
#define WIFI_PASS      "YOUR_WIFI_PASSWORD"
```

## 📈 Performance Optimization

- **DMA Usage**: All SPI transactions use DMA for maximum throughput
- **FIFO Management**: Smart watermark configuration prevents overflow
- **Task Priorities**: Optimized FreeRTOS task scheduling
- **Memory Management**: Efficient buffer management with circular buffers

## 🎯 Applications

### Industrial Monitoring
- Vibration analysis for machinery
- Structural health monitoring
- Tilt measurement for construction

### IoT and Wearables
- Fitness tracking
- Gesture recognition
- Motion detection

### Research and Development
- Motion analysis
- Sensor fusion algorithms
- Machine learning training data

## 🔧 Troubleshooting

### Common Issues

1. **I2C Communication Errors**
   - Check pullup resistors (4.7kΩ recommended)
   - Verify wiring connections
   - Reduce I2C clock speed if needed

2. **SPI Data Corruption**
   - Check clock polarity and phase settings
   - Verify CS signal timing
   - Ensure proper grounding

3. **FIFO Overflow**
   - Increase data processing frequency
   - Reduce FIFO watermark
   - Optimize data processing algorithms

4. **WiFi Connection Issues**
   - Check WiFi credentials
   - Verify signal strength
   - Check firewall settings

## 📚 Documentation

- [Detailed Guide](DETAILED_GUIDE.md) - Comprehensive setup and usage guide
- [API Documentation](docs/API.md) - REST API reference
- [Hardware Guide](docs/HARDWARE.md) - PCB and wiring information
- [Troubleshooting](docs/TROUBLESHOOTING.md) - Common issues and solutions

## 🤝 Contributing

We welcome contributions! Please see our [Contributing Guidelines](CONTRIBUTING.md) for details.

### Development Setup
1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Add tests if applicable
5. Submit a pull request

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

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

## 🙏 Acknowledgments

- ESP-IDF team for the excellent framework
- STMicroelectronics for sensor documentation
- InvenSense for ICM45686 driver
- Murata for SCL3300 support
- HBQ Technology for hardware support

## 📞 Contact

- **Issues**: [GitHub Issues](https://github.com/YOUR_USERNAME/ESP32_Vibra_Accel_inclio_Module/issues)
- **Discussions**: [GitHub Discussions](https://github.com/YOUR_USERNAME/ESP32_Vibra_Accel_inclio_Module/discussions)
- **Email**: your-email@example.com

---

**Made with ❤️ for the IoT and embedded systems community**