# BLE-IMU Dashboard 🧭

Open-source Python GUI for streaming and visualizing IMU data via BLE (ESP32-C6).

## Features
- 🔍 Scan for nearby BLE devices
- 🔗 Connect & discover characteristics automatically
- 📡 Subscribe to Notify characteristic (no UUID hardcode)
- 📊 Realtime plotting of X/Y/Z sensor data
- 🧩 Modular design: add IIS3DWB, ICM45686, SCL3300, IIS2MDC decoders easily

## Installation (Windows)

### 1. Install Python 3.11+
Download from [python.org](https://www.python.org/downloads/)

### 2. Create virtual environment
```powershell
python -m venv venv
.\venv\Scripts\Activate.ps1
```

### 3. Install dependencies
```powershell
pip install -r requirements.txt
```

## Run
```powershell
python main.py
```

## Usage
1. Click **🔍 Scan** to discover BLE devices
2. Select your ESP32 device from the list
3. Click **🔗 Connect**
4. Choose a NOTIFY characteristic from the dropdown
5. Click **▶ Start Notify** to begin streaming
6. Watch realtime X/Y/Z plots update

## Packet Format
The default parser expects:
- 12 bytes: 3 x float32 (little-endian) for X, Y, Z values
- Or 1 byte sensor ID + 12 bytes of data

Customize `core/packet_parser.py` for your specific sensor protocol.

## Project Structure
```
ble-imu-dashboard/
├── core/              # BLE logic & data handling
│   ├── ble_client.py
│   ├── packet_parser.py
│   ├── data_buffer.py
│   └── exporter.py
├── ui/                # PyQt6 GUI
│   ├── main_window.py
│   ├── plot_widget.py
│   └── style.qss
├── main.py            # Entry point
└── requirements.txt
```

## Troubleshooting

### Bluetooth not available
- Make sure Bluetooth is enabled on your PC
- On Windows, check Device Manager for Bluetooth adapter

### No devices found
- Ensure your ESP32 is advertising
- Move closer to the device
- Check if other apps are connected to it

### Permission denied (Linux)
```bash
sudo setcap cap_net_raw+eip $(eval readlink -f `which python`)
```

## License
MIT © 2025 Minh Hung

## Contributing
Pull requests welcome! Feel free to add:
- New sensor parsers
- Data export formats
- Signal processing filters
- Multi-device support
