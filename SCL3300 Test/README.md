# SCL3300 Test — Dual Monitor Guide (Wi‑Fi & BLE)

> [EN] This test project shows how to run the SCL3300 inclinometer and how to develop two monitoring applications: a Wi‑Fi web dashboard and a BLE viewer.
>
> [VI] Dự án test này hướng dẫn chạy cảm biến SCL3300 và phát triển 2 ứng dụng giám sát: dashboard qua Wi‑Fi và viewer qua BLE.

---

## 1) Build & Flash / [VI] Build & Flash

```
idf.py set-target esp32c6
idf.py build
idf.py flash monitor
```

---

## 2) Wi‑Fi Web Monitor (WebSocket) / [VI] Giám sát qua Wi‑Fi (WebSocket)

### EN
- Use the project `ESP32C6_IMU_WebMonitor` in this repository as the reference implementation.
- After flashing that project, open your browser at `http://<device-ip>/` to view the built‑in HTML/JS dashboard (Chart.js) rendering real‑time charts.
- WebSocket endpoint: `ws://<device-ip>/ws/data`. REST endpoints: `/api/data`, `/api/stats`, `/api/config`, `/api/download`.
- To add SCL3300 values to the dashboard, ensure `imu_manager_read_inclinometer()` is enabled and pushes angle/accel/temperature into the circular buffer.

### VI
- Sử dụng dự án `ESP32C6_IMU_WebMonitor` làm mẫu hoàn chỉnh.
- Sau khi nạp, mở trình duyệt tới `http://<ip-thiet-bi>/` để xem dashboard HTML/JS (Chart.js) hiển thị realtime.
- WebSocket: `ws://<ip>/ws/data`. REST: `/api/data`, `/api/stats`, `/api/config`, `/api/download`.
- Để hiển thị dữ liệu SCL3300, bật hàm `imu_manager_read_inclinometer()` và đẩy angle/accel/temperature vào buffer.

---

## 3) BLE Viewer / [VI] Viewer qua BLE

### EN
- Use the project `ESP32C6_IMU_BLEStreamer` as a BLE 5 streamer reference (2M PHY + DLE).
- Characteristic (Notify) batches compact records. Default per‑record layout (little‑endian): `t_us:uint32, ax:int16, ay:int16, az:int16, gx:int16, gy:int16, gz:int16`.
- PC viewer reference is provided in `docs/BLE_Viewer_Guide.md` (Python + Bleak + PyQtGraph).
- To stream SCL3300 angles instead of gyro, change the packer to place `angle_x/y/z` (scaled to int16) in the accel or a dedicated field.

### VI
- Tham khảo dự án `ESP32C6_IMU_BLEStreamer` để stream BLE 5 (2M PHY + DLE).
- Định dạng notify mặc định (little‑endian) cho mỗi bản ghi: `t_us:uint32, ax:int16, ay:int16, az:int16, gx:int16, gy:int16, gz:int16`.
- Viewer trên PC xem `docs/BLE_Viewer_Guide.md` (Python + Bleak + PyQtGraph).
- Nếu muốn gửi góc nghiêng SCL3300 thay con quay, sửa packer để đưa `angle_x/y/z` (chuẩn hoá về int16) vào khung dữ liệu.

---

## 4) Sensor Notes / [VI] Ghi chú cảm biến

### EN
- SCL3300 uses SPI with CRC; ensure proper CS/SCLK/MOSI/MISO mapping and supply. Choose mode per datasheet and set operating mode (e.g., Mode 1 for high accuracy).
- Typical read flow: issue command → read 16‑bit words → verify CRC → convert to degrees/°C.

### VI
- SCL3300 dùng SPI có CRC; map đúng CS/SCLK/MOSI/MISO và nguồn. Chọn chế độ theo datasheet (ví dụ Mode 1 cho độ chính xác cao).
- Quy trình: gửi lệnh → đọc word 16‑bit → kiểm CRC → chuyển đổi về độ/°C.

---

## 5) Next Steps / [VI] Bước tiếp theo

### EN
- Integrate this test with the WebMonitor by enabling the inclinometer path in `imu_manager.c`.
- For BLE, decide on the payload mapping (angles vs accelerations) and keep total bitrate under ~200–300 kbps.

### VI
- Tích hợp test này vào WebMonitor bằng cách bật đường đọc inclinometer trong `imu_manager.c`.
- Với BLE, xác định ánh xạ dữ liệu (góc hay gia tốc) và giữ tổng băng thông < ~200–300 kbps.
