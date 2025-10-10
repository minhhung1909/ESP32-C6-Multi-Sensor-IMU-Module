# BLE IMU Viewer Guide (PC)

This guide explains how to build a cross‑platform BLE viewer for the ESP32‑C6 IMU BLE Streamer. It covers packet format, recommended stack, environment setup, and reference code.

## 1. Overview
- Device: ESP32‑C6 running `ESP32C6_IMU_BLEStreamer`
- BLE: 2M PHY, DLE, MTU 247
- Service UUID: `0x1815` (placeholder)
- Characteristic UUID (Notify): `0x2A58` (placeholder)
- Device Name: `IMU-BLE`

## 2. Packet Format (binary, little‑endian)
Each frame is 16 bytes:
```
uint32  t_us        // timestamp in microseconds (lower 32 bits)
int16   ax, ay, az  // accelerometer
int16   gx, gy, gz  // gyroscope
```
Batch multiple frames (N×16) into a single notification up to 244 bytes.

Default q15 scaling:
- Acc: g = raw / 16384.0  (±2 g)
- Gyro: dps = raw / 131.072 (±250 dps)

## 3. Recommended PC Stack
- Python 3.10+
- BLE library: Bleak
- Plotting: PyQtGraph (fast) or Matplotlib

Install on Windows/Linux/macOS:
```
pip install bleak pyqtgraph numpy
```
Linux prerequisites: BlueZ (e.g., `sudo apt install bluetooth bluez`).

## 4. Reference Viewer (Python)
The minimal viewer scans for `IMU-BLE`, subscribes to notifications, parses 16‑byte frames, and renders live charts.

Save as `pc_viewer.py`:
```python
import asyncio, struct, numpy as np
from bleak import BleakScanner, BleakClient
import pyqtgraph as pg
from pyqtgraph.Qt import QtCore, QtWidgets

SERVICE_UUID = "00001815-0000-1000-8000-00805f9b34fb"
CHAR_UUID    = "00002a58-0000-1000-8000-00805f9b34fb"
DEVICE_NAME  = "IMU-BLE"
Q15_ACC = 16384.0
Q15_GYR = 131.072

class Viewer:
    def __init__(self):
        self.app = QtWidgets.QApplication([])
        self.win = pg.GraphicsLayoutWidget(show=True, title="IMU BLE Viewer")
        self.win.resize(1000, 600)
        self.plt_acc = self.win.addPlot(row=0, col=0, title="Accelerometer (g)")
        self.cur_ax = self.plt_acc.plot(pen=pg.mkPen('#e74c3c'))
        self.cur_ay = self.plt_acc.plot(pen=pg.mkPen('#27ae60'))
        self.cur_az = self.plt_acc.plot(pen=pg.mkPen('#2980b9'))
        self.win.addItem(pg.LabelItem(" "), row=1, col=0)
        self.plt_gyr = self.win.addPlot(row=2, col=0, title="Gyroscope (dps)")
        self.cur_gx = self.plt_gyr.plot(pen=pg.mkPen('#8e44ad'))
        self.cur_gy = self.plt_gyr.plot(pen=pg.mkPen('#f39c12'))
        self.cur_gz = self.plt_gyr.plot(pen=pg.mkPen('#2c3e50'))
        self.max_pts = 1000
        self.ax = np.zeros(self.max_pts); self.ay = np.zeros(self.max_pts); self.az = np.zeros(self.max_pts)
        self.gx = np.zeros(self.max_pts); self.gy = np.zeros(self.max_pts); self.gz = np.zeros(self.max_pts)
        self.idx = 0
    def update_curves(self):
        n = min(self.idx, self.max_pts)
        self.cur_ax.setData(self.ax[:n]); self.cur_ay.setData(self.ay[:n]); self.cur_az.setData(self.az[:n])
        self.cur_gx.setData(self.gx[:n]); self.cur_gy.setData(self.gy[:n]); self.cur_gz.setData(self.gz[:n])
    def push_sample(self, ax, ay, az, gx, gy, gz):
        i = self.idx % self.max_pts
        self.ax[i]=ax; self.ay[i]=ay; self.az[i]=az
        self.gx[i]=gx; self.gy[i]=gy; self.gz[i]=gz
        self.idx += 1
        if self.idx % 5 == 0: self.update_curves()
async def find_device():
    devices = await BleakScanner.discover(timeout=3.0)
    for d in devices:
        if (d.name or "").startswith(DEVICE_NAME):
            return d
    return None

def parse_notify(data: bytes, viewer: Viewer):
    rec_sz = 16
    for off in range(0, len(data) - (len(data) % rec_sz), rec_sz):
        t_us, ax, ay, az, gx, gy, gz = struct.unpack_from("<Ihhhhhh", data, off)
        viewer.push_sample(ax/ Q15_ACC, ay/ Q15_ACC, az/ Q15_ACC,
                           gx/ Q15_GYR, gy/ Q15_GYR, gz/ Q15_GYR)

async def run():
    viewer = Viewer()
    dev = await find_device()
    if not dev:
        print("Device not found"); return
    async with BleakClient(dev) as client:
        svcs = await client.get_services()
        ch = svcs.get_service(SERVICE_UUID).get_characteristic(CHAR_UUID)
        await client.start_notify(ch, lambda _, data: parse_notify(data, viewer))
        timer = QtCore.QTimer(); timer.timeout.connect(lambda: None); timer.start(50)
        viewer.app.exec_()
        await client.stop_notify(ch)

if __name__ == "__main__":
    asyncio.run(run())
```

Run:
```
python pc_viewer.py
```

## 5. Throughput Tips
- Keep total payload < 200–300 kbps for stability.
- Batch multiple frames per notification (e.g., 6–12 frames = 96–192B).
- Connection interval ~7.5–15 ms; reduce UI update rate.
- Prefer 2M PHY and MTU 247 on client.

## 6. CSV Logging (optional)
Append parsed samples to a CSV file periodically. Example:
```
# inside parse_notify()
# with open('imu_log.csv','a') as f: f.write(f"{t_us},{ax_g},{ay_g},{az_g},{gx_d},{gy_d},{gz_d}\n")
```

## 7. Web Bluetooth (optional)
If you prefer a browser-only viewer, use Web Bluetooth and Chart.js. The parsing logic is the same; read 16‑byte records from the ArrayBuffer.

## 8. Troubleshooting
- No device: ensure advertising and name `IMU-BLE`.
- No data: client must enable notifications (CCCD) and the ESP32 must be streaming.
- Choppy UI: lower redraw rate; batch more frames; use PyQtGraph.

---
For production, change UUIDs to your vendor-specific values and add authentication/encryption as needed.
