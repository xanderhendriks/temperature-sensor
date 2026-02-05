#include "usb_handler.h"
#include "logger.h"
#include "temperature.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "tinyusb.h"
#include "tusb_cdc_acm.h"

static const char *TAG = "usb_handler";

#define USB_RX_BUF_SIZE 512
#define USB_TX_BUF_SIZE 4096

static uint8_t rx_buf[USB_RX_BUF_SIZE];
static char tx_buf[USB_TX_BUF_SIZE];

// USB CDC ACM callback
static void tinyusb_cdc_rx_callback(int itf, cdcacm_event_t *event) {
    // CDC RX callback is called from ISR context, so we just signal the task
    // Actual processing happens in usb_task
}

esp_err_t usb_init(void) {
    ESP_LOGI(TAG, "Initializing USB...");
    
    // Configure TinyUSB
    tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,  // Use default descriptor
        .string_descriptor = NULL,  // Use default string descriptor
        .external_phy = false,
        .configuration_descriptor = NULL,
    };
    
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    
    tinyusb_config_cdcacm_t acm_cfg = {
        .usb_dev = TINYUSB_USBDEV_0,
        .cdc_port = TINYUSB_CDC_ACM_0,
        .rx_unread_buf_sz = 512,
        .callback_rx = &tinyusb_cdc_rx_callback,
        .callback_rx_wanted_char = NULL,
        .callback_line_state_changed = NULL,
        .callback_line_coding_changed = NULL
    };
    
    ESP_ERROR_CHECK(tusb_cdc_acm_init(&acm_cfg));
    
    ESP_LOGI(TAG, "USB initialized successfully");
    
    return ESP_OK;
}

static void handle_command(const char *cmd, size_t cmd_len) {
    ESP_LOGI(TAG, "Received command: %.*s", (int)cmd_len, cmd);
    
    // Remove trailing newline/carriage return
    while (cmd_len > 0 && (cmd[cmd_len - 1] == '\n' || cmd[cmd_len - 1] == '\r')) {
        cmd_len--;
    }
    
    if (cmd_len == 0) {
        return;
    }
    
    // GET_DATA command - return logged temperature data
    if (strncmp(cmd, "GET_DATA", 8) == 0) {
        ESP_LOGI(TAG, "Processing GET_DATA command");
        
        size_t data_len = 0;
        esp_err_t ret = logger_get_data(tx_buf, sizeof(tx_buf) - 1, &data_len);
        
        if (ret == ESP_OK) {
            tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, (uint8_t*)tx_buf, data_len);
            tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);
            ESP_LOGI(TAG, "Sent %d bytes of log data", data_len);
        } else {
            const char *err_msg = "ERROR: Failed to retrieve log data\n";
            tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, (uint8_t*)err_msg, strlen(err_msg));
            tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);
        }
    }
    // GET_CURRENT command - return current temperature
    else if (strncmp(cmd, "GET_CURRENT", 11) == 0) {
        ESP_LOGI(TAG, "Processing GET_CURRENT command");
        
        float temp = temperature_read();
        int len = snprintf(tx_buf, sizeof(tx_buf), "%.2f\n", temp);
        
        tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, (uint8_t*)tx_buf, len);
        tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);
        ESP_LOGI(TAG, "Sent current temperature: %.2f°C", temp);
    }
    // INFO command - return system information
    else if (strncmp(cmd, "INFO", 4) == 0) {
        ESP_LOGI(TAG, "Processing INFO command");
        
        uint32_t entry_count = logger_get_entry_count();
        const char *sensor_name = temperature_get_sensor_name();
        
        int len = snprintf(tx_buf, sizeof(tx_buf),
                          "ESP32-C6 Temperature Logger\n"
                          "Sensor: %s\n"
                          "Log entries: %lu\n"
                          "Max entries: %d\n",
                          sensor_name, entry_count, MAX_LOG_ENTRIES);
        
        tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, (uint8_t*)tx_buf, len);
        tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);
        ESP_LOGI(TAG, "Sent system info");
    }
    // CLEAR_DATA command - clear all logged data
    else if (strncmp(cmd, "CLEAR_DATA", 10) == 0) {
        ESP_LOGI(TAG, "Processing CLEAR_DATA command");
        
        esp_err_t ret = logger_clear_data();
        
        if (ret == ESP_OK) {
            const char *msg = "OK: Log data cleared\n";
            tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, (uint8_t*)msg, strlen(msg));
            tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);
            ESP_LOGI(TAG, "Log data cleared successfully");
        } else {
            const char *err_msg = "ERROR: Failed to clear log data\n";
            tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, (uint8_t*)err_msg, strlen(err_msg));
            tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);
        }
    }
    // Unknown command
    else {
        const char *err_msg = "ERROR: Unknown command. Available: GET_DATA, GET_CURRENT, INFO, CLEAR_DATA\n";
        tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, (uint8_t*)err_msg, strlen(err_msg));
        tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);
        ESP_LOGW(TAG, "Unknown command received");
    }
}

void usb_task(void *pvParameters) {
    ESP_LOGI(TAG, "USB task started on core %d", xPortGetCoreID());
    
    while (1) {
        // Check if USB CDC is connected
        if (tud_cdc_connected()) {
            // Check for received data
            size_t rx_size = 0;
            esp_err_t ret = tinyusb_cdcacm_read(TINYUSB_CDC_ACM_0, rx_buf, sizeof(rx_buf) - 1, &rx_size);
            
            if (ret == ESP_OK && rx_size > 0) {
                rx_buf[rx_size] = '\0';  // Null-terminate
                handle_command((char*)rx_buf, rx_size);
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
