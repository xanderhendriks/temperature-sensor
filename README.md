# Temperature & Humidity Logger (Zephyr + B-L4S5I-IOT01A)

A temperature and humidity logging solution for the ST B-L4S5I-IOT01A board built with Zephyr RTOS. The device exposes a USB composite device with Mass Storage (web dashboard) and CDC ACM Serial (data commands), providing a completely offline monitoring solution.

## Features

- Continuous temperature and humidity logging from two sensors (HTS221 + SHT31)
- Real-time graphing via embedded web dashboard (no external dependencies)
- USB Mass Storage exposes `index.htm` — open in a browser to view data
- USB CDC ACM Serial interface for commands
- Persistent log storage on QSPI flash via LittleFS (survives power cycles)
- Circular buffer with 2048-entry capacity
- RTC-backed timestamps (set via USB command)
- Configurable time-range filtering and per-sensor toggle in the dashboard

## Architecture

The firmware runs as a Zephyr RTOS application with two background threads:

- **Logger thread**: reads both sensors every 60 seconds and appends to a circular buffer (starts after RTC time is set)
- **USB CDC thread**: listens for commands over USB serial and responds with data

Storage is split across two filesystems:

- **RAM disk (FAT)**: mounted as USB Mass Storage, holds `index.htm` — resets on power cycle
- **LittleFS on QSPI flash** (MX25R6435F, 256 KB partition): persists the log buffer across reboots

When you plug in the USB-OTG cable:
1. The device appears as a USB drive on your computer
2. Open `index.htm` from the drive in Chrome or Edge
3. Click "Connect" to establish a Web Serial connection
4. Use "Set Time" to sync the device RTC
5. Click "Get Data" to fetch and graph logged readings

## Hardware

- **Board**: [B-L4S5I-IOT01A](https://www.st.com/en/evaluation-tools/b-l4s5i-iot01a.html) (STM32L4S5)
- **On-board sensor**: HTS221 (temperature + humidity, I2C2)
- **External sensor**: SHT31 on Arduino header D14 (SDA, PB9) / D15 (SCL, PB8) (I2C1, address 0x44)
- **Flash**: MX25R6435F 8 MB QSPI (partitioned for store + LittleFS)
- **USB**: USB-OTG port (not the ST-LINK port)

## Project Structure

```
temperature-sensor/
├── app/
│   ├── src/
│   │   └── main.c            # Application code (sensors, USB, logging, web UI)
│   ├── app.overlay            # Devicetree overlay (RAM disk, sensors, LittleFS partition)
│   ├── CMakeLists.txt         # Build configuration
│   └── prj.conf               # Kconfig options
├── patches/
│   └── zephyr/
│       └── usb-composite-fixes.patch
├── Dockerfile                 # Dev container with Zephyr SDK 0.17.4
├── west.yml                   # West manifest (Zephyr v4.3.0)
└── README.md
```

## Quick Start

1. **Clone the repository**
   ```bash
   mkdir temperature-sensor-workspace
   cd temperature-sensor-workspace
   git clone https://github.com/xanderhendriks/temperature-sensor.git
   cd temperature-sensor
   ```

2. **Open the project in VSCode**
   - Open VSCode and select **File->Open Folder**
   - Select the temperature-sensor folder inside the temperature-sensor-workspace and click **Select Folder**
   - Click **Reopen in container**
   - Open a terminal

3. **Set up the Zephyr workspace**
   ```bash
   west init -l .
   west update
   west patch apply
   ```

4. **Build and flash**
   ```bash
   west build -b b_l4s5i_iot01a app
   west flash
   ```

   Use the USB-OTG port (not the ST-LINK port) for the composite USB device.

5. **Access the dashboard**
   - Connect the USB-OTG port to your computer
   - Open the USB drive that appears
   - Open `index.htm` in Chrome or Edge
   - Click "Connect" and select the serial port
   - Click "Set Time" to sync the RTC, then "Get Data" to view logs

## Supported Browsers

The Web Serial API requires:
- Chrome 89+
- Edge 89+
- Opera 76+

Firefox and Safari do not support Web Serial.

## USB Commands

The device accepts commands over USB CDC ACM serial (115200 baud):

| Command | Response | Description |
|---------|----------|-------------|
| `GET_DATA` | CSV log data | Stream the full log as CSV |
| `GET_CURRENT` | Sensor readings | Current temperature and humidity from both sensors |
| `INFO` | Entry count | Show number of logged entries |
| `CLEAR_DATA` | `OK` | Delete all log entries |
| `SET_TIME <epoch>` | `OK <ISO timestamp>` | Set the RTC to Unix epoch seconds |
| `GET_TIME` | ISO timestamp | Read current RTC time |

CSV response format:
```
Timestamp,HTS221_Temp_C,HTS221_Hum_pct,SHT31_Temp_C,SHT31_Hum_pct
1738944600,22.50,45.0,22.80,43.2
1738944660,22.51,45.1,22.79,43.3
```

## Storage

- **RAM log**: 2048 entries in a circular buffer
- **LittleFS partition**: 256 KB on QSPI flash for persistent backup
- **Logging interval**: 60 seconds
- **Duration**: ~34 hours at capacity before oldest entries are overwritten

The log buffer is persisted to LittleFS after every new entry and restored on boot.

## Development

A Dockerfile is provided with the Zephyr SDK 0.17.4 and all build dependencies. The project uses West with Zephyr v4.3.0 (see [west.yml](west.yml)).

A patch for Zephyr USB composite device support is included in [patches/zephyr/](patches/zephyr/).

## Troubleshooting

**USB drive doesn't appear:**
- Make sure you are using the USB-OTG port, not the ST-LINK port
- Check that the USB cable supports data (not charge-only)
- Try a different USB port

**Can't connect via Web Serial:**
- Use Chrome or Edge
- Grant serial port permissions when prompted
- Close other applications using the serial port

**No temperature readings:**
- HTS221 is on-board and should work out of the box; if not detected, simulated values are used
- SHT31 requires external wiring to the Arduino I2C1 header: D14 (SDA) / D15 (SCL)

**Logging not starting:**
- The logger thread waits for the RTC to be set — click "Set Time" in the dashboard or send `SET_TIME <epoch>` via serial

