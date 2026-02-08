# Temp Logger (Zephyr + B-L4S5I-IOT01A)

This app exposes a USB Mass Storage disk with an `index.htm` file and a USB CDC interface for commands. The HTML uses Web Serial to request data from the device.

## Build

```bash
west build -b b_l4s5i_iot01a app
west flash
```

Use the USB-OTG port (not the ST-LINK port).

## USB Commands

- `GET_DATA` - Stream the CSV log
- `GET_CURRENT` - Read current temperature
- `INFO` - Show entry count
- `CLEAR_DATA` - Reset the log file

## Notes

- The USB MSC disk is backed by a RAM disk. Logs and HTML content reset on power cycle.
- If you want persistence, I can switch the MSC disk to QSPI flash.
