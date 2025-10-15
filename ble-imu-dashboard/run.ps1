# Quick launcher for BLE IMU Dashboard on Windows (PowerShell)

Write-Host ""
Write-Host "===================================" -ForegroundColor Cyan
Write-Host "  BLE IMU Dashboard Launcher" -ForegroundColor Cyan
Write-Host "===================================" -ForegroundColor Cyan
Write-Host ""

if (-Not (Test-Path "venv\Scripts\Activate.ps1")) {
    Write-Host "[ERROR] Virtual environment not found!" -ForegroundColor Red
    Write-Host ""
    Write-Host "Please run setup first:"
    Write-Host "  py -m venv venv"
    Write-Host "  .\venv\Scripts\Activate.ps1"
    Write-Host "  pip install -r requirements.txt"
    Write-Host ""
    pause
    exit 1
}

Write-Host "[INFO] Activating virtual environment..." -ForegroundColor Green
& "venv\Scripts\Activate.ps1"

Write-Host "[INFO] Starting BLE IMU Dashboard..." -ForegroundColor Green
python main.py

Write-Host ""
Write-Host "[INFO] Application closed." -ForegroundColor Yellow
