#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "temperature.h"
#include "logger.h"
#include "usb_handler.h"

static const char *TAG = "main";

// Configuration
#define LED_GPIO 8
#define LOG_INTERVAL_MS 60000  // 60 seconds
#define LED_BLINK_INTERVAL_MS 1000  // 1 second

// Task handles
static TaskHandle_t logging_task_handle = NULL;
static TaskHandle_t led_task_handle = NULL;
static TaskHandle_t usb_task_handle = NULL;

// LED blink task (status indicator)
static void led_task(void *pvParameters) {
    ESP_LOGI(TAG, "LED task started on core %d", xPortGetCoreID());
    
    // Configure LED GPIO
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << LED_GPIO),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    gpio_config(&io_conf);
    
    bool led_state = false;
    
    while (1) {
        led_state = !led_state;
        gpio_set_level(LED_GPIO, led_state);
        vTaskDelay(pdMS_TO_TICKS(LED_BLINK_INTERVAL_MS));
    }
}

// Temperature logging task
static void logging_task(void *pvParameters) {
    ESP_LOGI(TAG, "Logging task started on core %d", xPortGetCoreID());
    
    while (1) {
        // Read temperature
        float temperature = temperature_read();
        
        // Log temperature
        esp_err_t ret = logger_log_temperature(temperature);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to log temperature");
        } else {
            ESP_LOGI(TAG, "Temperature logged: %.2f°C (entries: %lu)", 
                     temperature, logger_get_entry_count());
        }
        
        // Wait for next log interval
        vTaskDelay(pdMS_TO_TICKS(LOG_INTERVAL_MS));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "ESP32-C6 Temperature Logger starting...");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Initialize temperature sensor (using simulated readings)
    // Note: GPIO pin parameter is not used for simulated sensor, but required by API
    ret = temperature_init(TEMP_SENSOR_SIMULATED, 4);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize temperature sensor");
        return;
    }
    
    // Initialize logger with LittleFS partition
    ret = logger_init("logdata");
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize logger");
        return;
    }
    
    // Initialize USB
    ret = usb_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize USB");
        return;
    }
    
    ESP_LOGI(TAG, "All subsystems initialized successfully");
    
    // Create LED blink task
    xTaskCreate(
        led_task,
        "led_task",
        2048,
        NULL,
        5,
        &led_task_handle
    );
    
    // Create logging task
    xTaskCreate(
        logging_task,
        "logging_task",
        4096,
        NULL,
        5,
        &logging_task_handle
    );
    
    // Create USB task
    xTaskCreate(
        usb_task,
        "usb_task",
        4096,
        NULL,
        5,
        &usb_task_handle
    );
    
    ESP_LOGI(TAG, "All tasks created successfully");
    ESP_LOGI(TAG, "System ready - LED blinking on GPIO %d", LED_GPIO);
    ESP_LOGI(TAG, "Temperature logging interval: %d seconds", LOG_INTERVAL_MS / 1000);
}
