@echo off
REM Quick launcher for BLE IMU Dashboard on Windows

echo.
echo ===================================
echo   BLE IMU Dashboard Launcher
echo ===================================
echo.

if not exist "venv\Scripts\activate.bat" (
    echo [ERROR] Virtual environment not found!
    echo.
    echo Please run setup first:
    echo   py -m venv venv
    echo   .\venv\Scripts\Activate.ps1
    echo   pip install -r requirements.txt
    echo.
    pause
    exit /b 1
)

echo [INFO] Activating virtual environment...
call venv\Scripts\activate.bat

echo [INFO] Starting BLE IMU Dashboard...
python main.py

echo.
echo [INFO] Application closed.
pause
