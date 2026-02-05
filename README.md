# ESP32-C6 Temperature Logger

## Project Description

The ESP32-C6 Temperature Logger is an implementation designed to log temperature data consistently with advanced features like USB Mass Storage functionality and web access. This project provides a user-friendly experience for monitoring temperature readings through a web dashboard.

## Features
- Continuous logging of temperature data
- USB Mass Storage for easy data access
- Web Serial API for direct interactions
- Dual-partition firmware updates for reliable performance

## Architecture

The project architecture utilizes a dual-partition scheme which allows for seamless firmware updates without interrupting ongoing logging. The partition layout includes:

- **Partition Table**:
  - App1: Main application (Current firmware)
  - App2: Secondary application (New firmware after update)
  - Storage: File system for logged data

## Hardware Requirements
- **ESP32-C6** module
- Temperature sensor (e.g., DHT22, DS18B20)
- USB cable for power and data communication

## ESP-IDF Setup Instructions
1. Install the ESP-IDF framework by following the official [ESP-IDF installation guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html).
2. Set the IDF_PATH environment variable to the path of your ESP-IDF installation:
   ```bash
   export IDF_PATH=~/esp/esp-idf
   ```
3. Install the required Python packages:
   ```bash
   pip install -r $IDF_PATH/requirements.txt
   ```

## Building and Flashing Commands
To build the project, navigate to the project directory and execute:
```bash
idf.py build
```
To flash the firmware to the ESP32-C6, run:
```bash
idf.py -p (YOUR PORT) flash
```
Replace `(YOUR PORT)` with the appropriate serial port (e.g., `/dev/ttyUSB0`).

## Configuration via menuconfig
Run the following command to open the configuration menu:
```bash
idf.py menuconfig
```
In this menu, you can set various parameters including Wi-Fi credentials, sensor settings, and logging behavior. 

## Sensor Wiring Reference
- **DHT22 Sensor Wiring**:
  - VCC -> 3.3V
  - GND -> GND
  - DATA -> GPIO Pin (e.g., GPIO 4)

## Usage Instructions for Accessing Web Dashboard
1. Connect the ESP32-C6 to your local Wi-Fi network via the `menuconfig` options.
2. Once connected, access the web dashboard by navigating to `http://<ESP32_IP_ADDRESS>` in your supported web browser.

## Supported Browsers
- Google Chrome
- Microsoft Edge

## Serial Command Protocol Documentation
- `GET /temp`: Retrieve the current temperature
- `GET /logs`: Retrieve the logged temperature data

## Troubleshooting
- **Issue**: Unable to connect to Wi-Fi
  - **Solution**: Ensure your Wi-Fi credentials are correct in `menuconfig`.
- **Issue**: No data logged
  - **Solution**: Check the wiring of the temperature sensor and ensure it is properly connected.

## License and Author Information
- **License**: MIT License
- **Author**: Xander Hendriks (xanderhendriks@example.com)