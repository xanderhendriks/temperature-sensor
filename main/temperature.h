#ifndef TEMPERATURE_H
#define TEMPERATURE_H

#include <stdint.h>
#include "esp_err.h"

// Temperature sensor types
typedef enum {
    TEMP_SENSOR_DHT22,
    TEMP_SENSOR_DS18B20,
    TEMP_SENSOR_BME280,
    TEMP_SENSOR_SIMULATED
} temperature_sensor_type_t;

// Initialize temperature sensor
esp_err_t temperature_init(temperature_sensor_type_t sensor_type, int gpio_pin);

// Read temperature (in Celsius)
float temperature_read(void);

// Get sensor type string
const char* temperature_get_sensor_name(void);

#endif // TEMPERATURE_H
