#include "usb_handler.h"
#include "logger.h"
#include "temperature.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "driver/usb_serial_jtag.h"

static const char *TAG = "usb_handler";

#define USB_RX_BUF_SIZE 512
#define USB_TX_BUF_SIZE 4096

static uint8_t rx_buf[USB_RX_BUF_SIZE];
static char tx_buf[USB_TX_BUF_SIZE];

static void usb_serial_write(const char *data, size_t len) {
    if (!usb_serial_jtag_is_connected()) {
        return;
    }

    usb_serial_jtag_write_bytes(data, len, pdMS_TO_TICKS(100));
    usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(100));
}

esp_err_t usb_init(void) {
    ESP_LOGI(TAG, "Initializing USB...");
    
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    cfg.rx_buffer_size = USB_RX_BUF_SIZE;
    cfg.tx_buffer_size = USB_TX_BUF_SIZE;

    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&cfg));
    
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
            usb_serial_write(tx_buf, data_len);
            ESP_LOGI(TAG, "Sent %d bytes of log data", data_len);
        } else {
            const char *err_msg = "ERROR: Failed to retrieve log data\n";
            usb_serial_write(err_msg, strlen(err_msg));
        }
    }
    // GET_CURRENT command - return current temperature
    else if (strncmp(cmd, "GET_CURRENT", 11) == 0) {
        ESP_LOGI(TAG, "Processing GET_CURRENT command");
        
        float temp = temperature_read();
        int len = snprintf(tx_buf, sizeof(tx_buf), "%.2f\n", temp);
        
        usb_serial_write(tx_buf, len);
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
        
        usb_serial_write(tx_buf, len);
        ESP_LOGI(TAG, "Sent system info");
    }
    // CLEAR_DATA command - clear all logged data
    else if (strncmp(cmd, "CLEAR_DATA", 10) == 0) {
        ESP_LOGI(TAG, "Processing CLEAR_DATA command");
        
        esp_err_t ret = logger_clear_data();
        
        if (ret == ESP_OK) {
            const char *msg = "OK: Log data cleared\n";
            usb_serial_write(msg, strlen(msg));
            ESP_LOGI(TAG, "Log data cleared successfully");
        } else {
            const char *err_msg = "ERROR: Failed to clear log data\n";
            usb_serial_write(err_msg, strlen(err_msg));
        }
    }
    // Unknown command
    else {
        const char *err_msg = "ERROR: Unknown command. Available: GET_DATA, GET_CURRENT, INFO, CLEAR_DATA\n";
        usb_serial_write(err_msg, strlen(err_msg));
        ESP_LOGW(TAG, "Unknown command received");
    }
}

void usb_task(void *pvParameters) {
    ESP_LOGI(TAG, "USB task started on core %d", xPortGetCoreID());
    
    while (1) {
        // Check if USB Serial/JTAG is connected
        if (usb_serial_jtag_is_connected()) {
            int rx_size = usb_serial_jtag_read_bytes(rx_buf, sizeof(rx_buf) - 1, pdMS_TO_TICKS(20));
            if (rx_size > 0) {
                rx_buf[rx_size] = '\0';
                handle_command((char*)rx_buf, rx_size);
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
