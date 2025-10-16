"""
Demo script to test BLE scanning without GUI
Useful for debugging BLE adapter and checking available devices
"""

import asyncio
from bleak import BleakScanner

async def scan_devices():
    print("\n" + "="*50)
    print("  BLE Scanner - Testing Bluetooth Adapter")
    print("="*50)
    print("\n[INFO] Scanning for 5 seconds...\n")
    
    try:
        devices = await BleakScanner.discover(timeout=5.0)
        
        if not devices:
            print("❌ No devices found!")
            print("\nPossible reasons:")
            print("  - Bluetooth is disabled")
            print("  - No BLE devices nearby")
            print("  - Bluetooth adapter issue")
            return
        
        print(f"✅ Found {len(devices)} device(s):\n")
        
        for i, device in enumerate(devices, 1):
            name = device.name or "(Unknown)"
            addr = device.address
            rssi = device.rssi or "N/A"
            
            print(f"{i}. {name}")
            print(f"   Address: {addr}")
            print(f"   RSSI: {rssi} dBm")
            print(f"   Metadata: {device.metadata}")
            print()
            
    except Exception as e:
        print(f"❌ Error during scanning: {e}")
        print("\nMake sure:")
        print("  - Bluetooth is enabled on your PC")
        print("  - You have necessary permissions")
        print("  - No other app is blocking Bluetooth")

if __name__ == "__main__":
    print("\nPress Ctrl+C to stop\n")
    try:
        asyncio.run(scan_devices())
    except KeyboardInterrupt:
        print("\n\n[INFO] Scan cancelled by user")
    
    print("\n" + "="*50)
    input("\nPress Enter to exit...")
