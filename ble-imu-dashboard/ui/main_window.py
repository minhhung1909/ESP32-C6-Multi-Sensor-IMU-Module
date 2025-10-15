from PyQt6.QtWidgets import (
    QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QPushButton, QLabel, QListWidget, QListWidgetItem,
    QComboBox, QMessageBox
)
from PyQt6.QtCore import QTimer
from core.ble_client import BLEHandler, DeviceInfo, CharInfo
from .plot_widget import PlotWidget
import asyncio, struct, numpy as np

class IMUDashboard(QMainWindow):
    def __init__(self, loop: asyncio.AbstractEventLoop):
        super().__init__()
        self.loop = loop
        self.setWindowTitle("BLE IMU Dashboard (Open Source)")
        self.resize(1000, 650)

        # --- BLE core
        self.ble = BLEHandler(self.on_ble_data,
                              preferred_services=["fff0", "180d", "181a"])

        # --- UI layout
        top_bar = QWidget()
        top_layout = QHBoxLayout(top_bar)
        self.btn_scan = QPushButton("🔍 Scan")
        self.btn_connect = QPushButton("🔗 Connect")
        self.btn_disconnect = QPushButton("❌ Disconnect")
        self.lbl_status = QLabel("Idle")
        top_layout.addWidget(self.btn_scan)
        top_layout.addWidget(self.btn_connect)
        top_layout.addWidget(self.btn_disconnect)
        top_layout.addStretch(1)
        top_layout.addWidget(self.lbl_status)

        left_panel = QWidget()
        left_layout = QVBoxLayout(left_panel)
        self.list_devices = QListWidget()
        self.combo_chars = QComboBox()
        self.btn_notify = QPushButton("▶ Start Notify")
        self.btn_stop = QPushButton("⏹ Stop Notify")
        left_layout.addWidget(QLabel("Devices"))
        left_layout.addWidget(self.list_devices)
        left_layout.addWidget(QLabel("Characteristics"))
        left_layout.addWidget(self.combo_chars)
        left_layout.addWidget(self.btn_notify)
        left_layout.addWidget(self.btn_stop)
        left_layout.addStretch(1)

        right_panel = QWidget()
        right_layout = QVBoxLayout(right_panel)
        self.plot_x = PlotWidget("X Axis", 'r')
        self.plot_y = PlotWidget("Y Axis", 'g')
        self.plot_z = PlotWidget("Z Axis", 'b')
        right_layout.addWidget(self.plot_x)
        right_layout.addWidget(self.plot_y)
        right_layout.addWidget(self.plot_z)

        root = QWidget()
        root_layout = QVBoxLayout(root)
        body = QHBoxLayout()
        body.addWidget(left_panel, 1)
        body.addWidget(right_panel, 2)
        root_layout.addWidget(top_bar)
        root_layout.addLayout(body)
        self.setCentralWidget(root)

        # --- signals
        self.btn_scan.clicked.connect(lambda: asyncio.ensure_future(self.do_scan()))
        self.btn_connect.clicked.connect(lambda: asyncio.ensure_future(self.do_connect()))
        self.btn_disconnect.clicked.connect(lambda: asyncio.ensure_future(self.do_disconnect()))
        self.btn_notify.clicked.connect(lambda: asyncio.ensure_future(self.do_notify()))
        self.btn_stop.clicked.connect(lambda: asyncio.ensure_future(self.ble.stop_notify()))

        self.devices: list[DeviceInfo] = []
        self.chars: list[CharInfo] = []
        self.parser = None

        self.timer = QTimer(self)
        self.timer.timeout.connect(self.refresh_plots)
        self.timer.start(30)

    # ========== BLE Actions ==========
    async def do_scan(self):
        self.set_status("Scanning...")
        try:
            self.devices = await self.ble.scan()
            self.list_devices.clear()
            for d in self.devices:
                self.list_devices.addItem(QListWidgetItem(f"{d.name} [{d.address}] RSSI:{d.rssi}"))
            self.set_status(f"Found {len(self.devices)} devices.")
        except Exception as e:
            self.error(f"Scan failed: {e}")

    async def do_connect(self):
        row = self.list_devices.currentRow()
        if row < 0:
            self.error("Select a device first.")
            return
        dev = self.devices[row]
        try:
            await self.ble.connect(dev.address)
            self.set_status(f"Connected: {dev.name}")
            await self.populate_chars()
        except Exception as e:
            self.error(f"Connect failed: {e}")

    async def populate_chars(self):
        try:
            self.chars = await self.ble.discover_notify_chars()
        except Exception as e:
            self.error(f"Discovery failed: {e}")
            return
        self.combo_chars.clear()
        for c in self.chars:
            self.combo_chars.addItem(f"{c.char_uuid} (svc {c.service_uuid})")
        self.set_status(f"{len(self.chars)} NOTIFY chars found.")

    async def do_disconnect(self):
        await self.ble.disconnect()
        self.combo_chars.clear()
        self.set_status("Disconnected.")

    async def do_notify(self):
        idx = self.combo_chars.currentIndex()
        if idx < 0:
            self.error("Select a characteristic first.")
            return
        try:
            await self.ble.start_notify(self.chars[idx].char_uuid)
            self.set_status("Streaming...")
        except Exception as e:
            self.error(f"Start notify failed: {e}")

    # ========== Data path ==========
    def on_ble_data(self, data: bytes):
        """Simple example: decode 3 float32 => x, y, z."""
        try:
            if len(data) >= 12:
                x, y, z = struct.unpack("<3f", data[:12])
            else:
                val = int.from_bytes(data[:4], "little", signed=True)
                x = y = z = val
        except Exception:
            x = y = z = 0.0
        self.plot_x.append(x)
        self.plot_y.append(y)
        self.plot_z.append(z)

    def refresh_plots(self):
        self.plot_x.refresh()
        self.plot_y.refresh()
        self.plot_z.refresh()

    # ========== Helpers ==========
    def set_status(self, text):
        self.lbl_status.setText(f"Status: {text}")

    def error(self, msg):
        QMessageBox.critical(self, "Error", msg)
        self.set_status("Error")
