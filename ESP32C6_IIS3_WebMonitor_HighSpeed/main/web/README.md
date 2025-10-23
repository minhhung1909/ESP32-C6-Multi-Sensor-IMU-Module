# Web Interface Files

This directory contains the separated web interface for the IIS3DWB monitor.

## File Structure

```
web/
├── index.html   - Main HTML structure
├── style.css    - All CSS styling
├── app.js       - JavaScript application logic
└── README.md    - This file
```

## Deployment

### Option 1: SPIFFS (Recommended)
Upload these files to `/spiffs/web/` directory on ESP32-C6:

```bash
# Build SPIFFS partition
idf.py partition-table-flash

# Flash files to SPIFFS
# (Configure partitions.csv to include SPIFFS)
```

### Option 2: Embedded Fallback
If files are not found in SPIFFS, the server will serve embedded HTML from `root_handler()` in `web_server.c`.

## Development

Edit these files for easier development:
- **index.html** - Add/remove UI elements
- **style.css** - Modify appearance
- **app.js** - Update chart behavior, WebSocket handling

After editing, upload to SPIFFS or rebuild firmware with embedded version.

## Features

- Real-time WebSocket data streaming
- Chart.js visualization (200 points)
- Pause/Resume control
- Full-scale range selection (±2g/±4g/±8g/±16g)
- Auto-scaling chart option
- Event log with timestamps
- Responsive metrics display

## API Endpoints

- `GET /` - Main dashboard
- `GET /api/data` - Latest sensor data (JSON)
- `GET /api/stats` - Buffer statistics (JSON)
- `GET /api/config` - Current configuration (JSON)
- `POST /api/config` - Update configuration (pause, full_scale)
- `GET /api/download?format=csv|json` - Download data
- `WebSocket /ws/data` - Real-time streaming
