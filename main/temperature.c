#include "temperature.h"
#include <stdlib.h>
#include <math.h>
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "temperature";
static temperature_sensor_type_t current_sensor_type = TEMP_SENSOR_SIMULATED;
static int sensor_gpio = -1;

// Simulated temperature parameters (for testing without actual sensor)
static float simulated_base_temp = 22.0f;
static uint32_t simulated_cycle_count = 0;

esp_err_t temperature_init(temperature_sensor_type_t sensor_type, int gpio_pin) {
    current_sensor_type = sensor_type;
    sensor_gpio = gpio_pin;
    
    ESP_LOGI(TAG, "Initializing temperature sensor type: %d on GPIO %d", sensor_type, gpio_pin);
    
    switch (sensor_type) {
        case TEMP_SENSOR_DHT22:
            ESP_LOGW(TAG, "DHT22 sensor driver not implemented, using simulated readings");
            current_sensor_type = TEMP_SENSOR_SIMULATED;
            break;
            
        case TEMP_SENSOR_DS18B20:
            ESP_LOGW(TAG, "DS18B20 sensor driver not implemented, using simulated readings");
            current_sensor_type = TEMP_SENSOR_SIMULATED;
            break;
            
        case TEMP_SENSOR_BME280:
            ESP_LOGW(TAG, "BME280 sensor driver not implemented, using simulated readings");
            current_sensor_type = TEMP_SENSOR_SIMULATED;
            break;
            
        case TEMP_SENSOR_SIMULATED:
            ESP_LOGI(TAG, "Using simulated temperature sensor");
            break;
            
        default:
            ESP_LOGE(TAG, "Unknown sensor type");
            return ESP_ERR_INVALID_ARG;
    }
    
    return ESP_OK;
}

float temperature_read(void) {
    float temperature = 0.0f;
    
    if (current_sensor_type == TEMP_SENSOR_SIMULATED) {
        // Generate simulated temperature with realistic variation
        // Base temperature cycles slowly (sine wave over ~30 minutes)
        // Add some random noise for realism
        simulated_cycle_count++;
        float cycle_factor = sin(simulated_cycle_count * 0.01f);
        float random_noise = ((float)(rand() % 100) / 100.0f - 0.5f) * 0.5f;
        
        temperature = simulated_base_temp + (cycle_factor * 3.0f) + random_noise;
        
        ESP_LOGD(TAG, "Simulated temperature: %.2f°C", temperature);
    } else {
        // Actual sensor reading would go here
        // For now, return simulated value
        temperature = simulated_base_temp;
        ESP_LOGW(TAG, "Sensor not implemented, returning default: %.2f°C", temperature);
    }
    
    return temperature;
}

const char* temperature_get_sensor_name(void) {
    switch (current_sensor_type) {
        case TEMP_SENSOR_DHT22:
            return "DHT22";
        case TEMP_SENSOR_DS18B20:
            return "DS18B20";
        case TEMP_SENSOR_BME280:
            return "BME280";
        case TEMP_SENSOR_SIMULATED:
            return "Simulated";
        default:
            return "Unknown";
    }
}
