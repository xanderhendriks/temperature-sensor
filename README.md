# ESP32-C6 Temperature Sensor Logger

A temperature logging solution for ESP32-C6-DevKit-C1 that provides continuous temperature monitoring with a web-based dashboard accessible via USB.

## Features

- 🌡️ Continuous temperature logging (no interruptions)
- 📊 Real-time graphing via web dashboard
- 💾 Dual-partition architecture for simultaneous logging and data access
- 🔌 USB-powered with Mass Storage + Serial interfaces
- 📈 Chart.js-based visualizations with multiple time ranges
- 💻 No network required - completely offline solution
- 🔄 Automatic circular buffer for long-term logging

## Architecture

The system uses a dual-partition approach:

- **Partition 1 (USB Mass Storage)**: Contains the web dashboard (index.html, Chart.js, etc.) - appears as a USB drive
- **Partition 2 (LittleFS)**: Internal storage for continuous temperature logging

When you plug in the USB cable:
1. The device appears as a USB drive on your computer
2. Open `index.html` from the drive in Chrome/Edge browser
3. Click "Connect" to establish Web Serial connection
4. The dashboard fetches data from the ESP32 and displays graphs
5. Temperature logging continues uninterrupted in the background

## Hardware Requirements

- ESP32-C6-DevKit-C1
- Temperature sensor (one of):
  - DHT22 (recommended for beginners)
  - DS18B20 (waterproof option)
  - BME280 (includes humidity and pressure)
- USB-C cable (for power and data)
- Connecting wires

## Project Structure

```
temperature-sensor/
├── firmware/                 # ESP32-C6 firmware
│   ├── src/
│   │   ├── main.cpp         # Main application
│   │   ├── temperature.cpp  # Temperature sensor interface
│   │   ├── logger.cpp       # Data logging to flash
│   │   ├── usb_msc.cpp      # USB Mass Storage handling
│   │   └── serial_handler.cpp # Serial command protocol
│   ├── data/                # Files for USB Mass Storage partition
│   │   ├── index.html
│   │   ├── app.js
│   │   ├── chart.min.js
│   │   └── style.css
│   ├── platformio.ini       # PlatformIO configuration
│   └── partitions.csv       # Partition table
├── docs/
│   ├── wiring.md           # Sensor wiring diagrams
│   └─�� setup.md            # Setup and flashing instructions
└── README.md

```

## Quick Start

1. **Clone the repository**
   ```bash
   git clone https://github.com/xanderhendriks/temperature-sensor.git
   cd temperature-sensor
   ```

2. **Install PlatformIO** (if not already installed)
   - [PlatformIO IDE](https://platformio.org/install/ide?install=vscode) for VS Code

3. **Connect your sensor** (see [docs/wiring.md](docs/wiring.md))

4. **Build and flash**
   ```bash
   cd firmware
   pio run --target upload
   ```

5. **Access the dashboard**
   - Plug in USB cable
   - Open the USB drive that appears
   - Open `index.html` in Chrome or Edge browser
   - Click "Connect" and select the ESP32 serial port

## Supported Browsers

The Web Serial API requires:
- ✅ Chrome 89+
- ✅ Edge 89+
- ✅ Opera 76+
- ❌ Firefox (not yet supported)
- ❌ Safari (not yet supported)

## Configuration

Edit `firmware/src/config.h` to customize:
- Logging interval (default: 60 seconds)
- Temperature sensor type
- GPIO pins
- Storage capacity
- Time zone

## Serial Protocol

The ESP32 responds to commands over USB CDC Serial:

| Command | Response | Description |
|---------|----------|-------------|
| `GET_DATA` | JSON with all logged data | Retrieve complete log |
| `GET_LATEST,N` | JSON with last N entries | Recent data only |
| `GET_CURRENT` | Current temperature | Real-time reading |
| `GET_RANGE,start,end` | JSON with time range | Filtered data |
| `CLEAR_DATA` | Confirmation | Clear all logs |

Response format:
```json
{
  "status": "ok",
  "data": [
    {"time": "2026-02-05T10:00:00Z", "temp": 22.5},
    {"time": "2026-02-05T10:01:00Z", "temp": 22.6}
  ]
}
```

## Storage

With 4MB flash:
- **Partition 1**: 512KB (web dashboard)
- **Partition 2**: ~3MB (temperature logs)
- **Capacity**: ~100,000 log entries
- **Duration**: ~69 days at 60-second intervals

Older entries are automatically deleted when storage is full (circular buffer).

## Troubleshooting

**USB drive doesn't appear:**
- Check USB cable supports data (not just power)
- Try different USB port
- Check Device Manager (Windows) or System Information (Mac)

**Can't connect via Web Serial:**
- Use Chrome or Edge browser
- Grant serial port permissions when prompted
- Close other applications using the serial port

**No temperature readings:**
- Check sensor wiring
- Verify correct GPIO pin in config.h
- Check sensor type matches configuration

## License

MIT License - see LICENSE file for details

## Contributing

Contributions welcome! Please open an issue or submit a pull request.

## Author

Xander Hendriks (@xanderhendriks)