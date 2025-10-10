# ESP32-C6 IMU BLE Streamer

> [VI] Bộ phát BLE 5 (2M PHY + DLE) cho dữ liệu IMU nén (int16) theo gói 244B. Tối ưu cho băng thông BLE và mức tiêu thụ pin. Các mục song ngữ có nhãn [VI].

A minimal BLE 5 (2M PHY + DLE) GATT server that streams compressed IMU data (int16) in compact 244-byte notifications. Optimized to fit BLE bandwidth (downsampled IIS3DWB) and suitable for battery-powered use.

## Features

[VI] Tính năng
- BLE 5, 2M PHY, Data Length Extension, MTU 247
- One GATT characteristic (Notify) for high-throughput streaming
- Batching multiple IMU samples per notification (up to 244B)
- Configurable sensor priorities/ODR via `imu_ble_config_t`
- Example producer packs `t_us + acc(x,y,z) + gyro(x,y,z)` as int16

## Directory

[VI] Cấu trúc thư mục
```
ESP32C6_IMU_BLEStreamer/
├── CMakeLists.txt
└── main/
    ├── CMakeLists.txt
    ├── ble_stream.c/.h        # BLE GATT service (notify)
    ├── imu_ble.c/.h           # Sensor aggregator/packer
    └── main.c                 # App entry, configuration
```

## Build & Flash

[VI] Build & Flash
```
idf.py set-target esp32c6
cd ESP32C6_IMU_BLEStreamer
idf.py build flash monitor
```

## Configuration (main.c)

[VI] Cấu hình (main.c)
`imu_ble_config_t cfg` controls which sensors to enable and BLE-friendly ODRs:
```
.enable_iis2mdc  // magnetometer
.enable_iis3dwb  // accelerometer (downsampled for BLE)
.enable_icm45686 // accel+gyro
.enable_scl3300  // inclinometer
.iis3dwb_odr_hz = 800     // reduce from 26.7 kHz
.icm45686_odr_hz = 400
.packet_interval_ms = 20  // ~50 Hz bursts
```
Adjust according to your bandwidth/power needs. For highest stability, keep total throughput < 200–300 kbps.

## GATT Layout

[VI] Cấu trúc GATT
- Service UUID: `0x1815` (placeholder; use your custom UUID in production)
- Characteristic UUID: `0x2A58` (placeholder; Notify only)
- CCCD supported (enable notifications from client)
- Device Name: `IMU-BLE`

BLE settings:
- Preferred PHY: 2M
- Local MTU: 247
- Connection interval: ~7.5 ms (min/max = 6)

## Packet Format (binary, little-endian)

[VI] Định dạng gói (nhị phân, little‑endian)
Each sample: 16 bytes
```
uint32  t_us        // timestamp in microseconds (lower 32 bits)
int16   ax, ay, az  // accelerometer (g scaled to q15)
int16   gx, gy, gz  // gyroscope (dps scaled to q15)
```
- Batching: multiple samples appended back-to-back up to 244 bytes per notification.
- Scaling (default):
  - Acc: q15 ≈ value_g * 16384 (±2 g)
  - Gyro: q15 ≈ dps * 131.072 (±250 dps)
Change scaling to match your sensor ranges.

## Client Notes

[VI] Ghi chú phía client
- Subscribe to notifications on the single characteristic.
- Expect packets of variable length up to 244B, containing N×16B frames.
- Reconstruct by reading 16-byte records in order.
- For iOS/Android/WebBLE, set MTU 247 and request 2M PHY (if supported) for best throughput.

## Power Considerations

[VI] Ghi chú năng lượng
- BLE streaming @ 100–400 Hz (6-axis) typically 1–5 mA average on ESP32-C6 (order of magnitude; depends on interval/PHY).
- Significantly lower than Wi‑Fi realtime.
- For CR‑cell operation, keep duty cycle low (burst + sleep), or consider Li‑ion/LiPo for continuous streaming.

## Replacing the Simulator with Real Sensors

[VI] Thay dữ liệu mô phỏng bằng cảm biến thực
`imu_ble.c` currently simulates data. To stream real measurements:
1. Initialize IIS3DWB / ICM45686 with BLE-friendly ODRs in `imu_ble_init`.
2. Replace the simulated waveforms in `producer_task` with actual reads.
3. Convert to q15 using the provided `f2q15` helper and call `pack_and_notify`.

## Troubleshooting

[VI] Xử lý sự cố
- No notifications: ensure client enabled CCC and that `Notify EN` appears in logs.
- Low throughput: verify 2M PHY, MTU 247, and DLE enabled; increase connection interval slightly; batch more samples per notify.
- Packet loss: reduce ODR or notify frequency; avoid CPU-heavy operations in main loop.

## License

[VI] Giấy phép
MIT (see project root LICENSE)
