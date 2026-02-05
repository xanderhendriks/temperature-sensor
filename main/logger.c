#include "logger.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_littlefs.h"

static const char *TAG = "logger";
static const char *BASE_MOUNT_PATH = "/littlefs";
static const char *LOG_FILE_PATH = "/littlefs/temp_log.csv";
static uint32_t entry_count = 0;
static bool logger_initialized = false;

esp_err_t logger_init(const char *partition_label) {
    ESP_LOGI(TAG, "Initializing logger with partition: %s", partition_label);
    
    esp_vfs_littlefs_conf_t conf = {
        .base_path = BASE_MOUNT_PATH,
        .partition_label = partition_label,
        .format_if_mount_failed = true,
        .dont_mount = false,
    };
    
    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find LittleFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize LittleFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }
    
    size_t total = 0, used = 0;
    ret = esp_littlefs_info(partition_label, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get LittleFS partition information");
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }
    
    // Check if log file exists and count entries
    FILE *f = fopen(LOG_FILE_PATH, "r");
    if (f != NULL) {
        char line[128];
        entry_count = 0;
        while (fgets(line, sizeof(line), f) != NULL) {
            entry_count++;
        }
        fclose(f);
        
        // Subtract 1 for header line if it exists
        if (entry_count > 0) {
            entry_count--;
        }
        
        ESP_LOGI(TAG, "Found existing log file with %d entries", entry_count);
    } else {
        // Create new log file with header
        f = fopen(LOG_FILE_PATH, "w");
        if (f != NULL) {
            fprintf(f, "Timestamp,Temperature_C\n");
            fclose(f);
            entry_count = 0;
            ESP_LOGI(TAG, "Created new log file");
        } else {
            ESP_LOGE(TAG, "Failed to create log file");
            return ESP_FAIL;
        }
    }
    
    logger_initialized = true;
    return ESP_OK;
}

esp_err_t logger_log_temperature(float temperature) {
    if (!logger_initialized) {
        ESP_LOGE(TAG, "Logger not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Check if rotation is needed
    if (entry_count >= MAX_LOG_ENTRIES) {
        ESP_LOGW(TAG, "Maximum log entries reached (%d), rotating log", MAX_LOG_ENTRIES);
        
        // Backup old log
        char backup_path[64];
        snprintf(backup_path, sizeof(backup_path), "%s.old", LOG_FILE_PATH);
        
        // Remove old backup if it exists
        remove(backup_path);
        
        // Rename current log to backup
        rename(LOG_FILE_PATH, backup_path);
        
        // Create new log file with header
        FILE *f = fopen(LOG_FILE_PATH, "w");
        if (f != NULL) {
            fprintf(f, "Timestamp,Temperature_C\n");
            fclose(f);
            entry_count = 0;
            ESP_LOGI(TAG, "Log file rotated successfully");
        } else {
            ESP_LOGE(TAG, "Failed to create new log file after rotation");
            return ESP_FAIL;
        }
    }
    
    // Append to log file
    FILE *f = fopen(LOG_FILE_PATH, "a");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open log file for writing");
        return ESP_FAIL;
    }
    
    // Get current timestamp (seconds since boot for now)
    int64_t timestamp = esp_timer_get_time() / 1000000;
    
    fprintf(f, "%lld,%.2f\n", timestamp, temperature);
    fclose(f);
    
    entry_count++;
    
    ESP_LOGD(TAG, "Logged temperature: %.2f°C (entry %d)", temperature, entry_count);
    
    return ESP_OK;
}

esp_err_t logger_get_data(char *buffer, size_t buffer_size, size_t *data_len) {
    if (!logger_initialized) {
        ESP_LOGE(TAG, "Logger not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    FILE *f = fopen(LOG_FILE_PATH, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open log file for reading");
        return ESP_FAIL;
    }
    
    size_t bytes_read = fread(buffer, 1, buffer_size - 1, f);
    buffer[bytes_read] = '\0';
    fclose(f);
    
    if (data_len != NULL) {
        *data_len = bytes_read;
    }
    
    ESP_LOGI(TAG, "Retrieved %d bytes of log data", bytes_read);
    
    return ESP_OK;
}

uint32_t logger_get_entry_count(void) {
    return entry_count;
}

esp_err_t logger_clear_data(void) {
    if (!logger_initialized) {
        ESP_LOGE(TAG, "Logger not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGW(TAG, "Clearing all log data");
    
    // Remove old backup if it exists
    char backup_path[64];
    snprintf(backup_path, sizeof(backup_path), "%s.old", LOG_FILE_PATH);
    remove(backup_path);
    
    // Remove current log file
    remove(LOG_FILE_PATH);
    
    // Create new log file with header
    FILE *f = fopen(LOG_FILE_PATH, "w");
    if (f != NULL) {
        fprintf(f, "Timestamp,Temperature_C\n");
        fclose(f);
        entry_count = 0;
        ESP_LOGI(TAG, "Log data cleared successfully");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to create new log file");
        return ESP_FAIL;
    }
}
