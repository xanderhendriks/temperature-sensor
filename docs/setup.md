# Setup and Installation Guide

## Prerequisites
Before you begin, ensure you have PlatformIO installed on your system. You can download it from the [PlatformIO website](https://platformio.org/install).

## Hardware Assembly Steps
1. Gather the necessary components:
   - Temperature sensor (e.g., DHT22)
   - Microcontroller (e.g., Arduino Uno)
   - Jumper wires
   - Breadboard (optional)

2. Connect the temperature sensor to the microcontroller:
   - Connect the VCC pin of the sensor to the 5V pin on the microcontroller.
   - Connect the GND pin of the sensor to the Ground (GND) pin on the microcontroller.
   - Connect the data pin of the sensor to a digital pin (e.g., D2) on the microcontroller.

## Software Configuration
Edit the `config.h` file in your project to configure the sensor selection and GPIO pins:
```c
// config.h

#define SENSOR_TYPE DHT22  // Select your temperature sensor
#define DHT_PIN D2         // Define the GPIO pin connected to the sensor
```

## Building and Uploading Firmware
Use the following PlatformIO commands to build and upload the firmware:
```bash
# Build the project
pio run

# Upload the firmware to the microcontroller
pio upload
```

## Accessing the Web Dashboard via USB Mass Storage
After uploading the firmware, connect the microcontroller to your computer via USB. The device should mount as a USB Mass Storage device, allowing you to access the web dashboard.

## Troubleshooting Common Issues
- **Sensor not responding:** Check your wiring and ensure that the sensor is powered.
- **Compilation errors:** Ensure the correct libraries are installed in PlatformIO.

## Tips for Long-Term Operation
- Regularly check connections and clean dust off components to ensure proper operation.
- Keep the firmware updated to take advantage of new features and fixes.

---