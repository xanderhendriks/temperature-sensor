# ESP-IDF Build Instructions

This project is configured to build with ESP-IDF v5.0 or later.

## Prerequisites

1. Install ESP-IDF following the official guide:
   https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/

2. Set up the ESP-IDF environment:
   ```bash
   . $HOME/esp/esp-idf/export.sh
   ```

## Building the Project

1. Navigate to the project directory:
   ```bash
   cd temperature-sensor
   ```

2. Build the project:
   ```bash
   idf.py build
   ```

## Flashing to ESP32-C6

1. Connect your ESP32-C6 device via USB

2. Flash the firmware:
   ```bash
   idf.py -p /dev/ttyUSB0 flash
   ```
   (Replace `/dev/ttyUSB0` with your actual serial port)

3. Monitor the output:
   ```bash
   idf.py -p /dev/ttyUSB0 monitor
   ```

## Project Features

- **Dual-core operation**: Logging on Core 0, USB handling on Core 1
- **Temperature logging**: Logs temperature every 60 seconds to LittleFS partition
- **LED indicator**: Blinks on GPIO 8 to show system is running
- **USB Serial/JTAG interface**: Commands available:
  - `GET_DATA` - Retrieve all logged data in CSV format
  - `GET_CURRENT` - Get current temperature reading
  - `INFO` - Display system information
  - `CLEAR_DATA` - Clear all logged data
- **Log rotation**: Automatically rotates logs at 100,000 entries
- **Simulated sensor**: Uses simulated temperature readings (actual sensor drivers are TODO)

## Configuration

Edit `sdkconfig.defaults` to modify default configurations, or run:
```bash
idf.py menuconfig
```

## Partition Layout

See `partitions.csv` for the memory layout:
- NVS: Non-volatile storage
- PHY_INIT: PHY initialization data
- FACTORY: Main application
- WEBDATA: FAT filesystem (reserved for future web dashboard)
- LOGDATA: LittleFS for temperature logs
