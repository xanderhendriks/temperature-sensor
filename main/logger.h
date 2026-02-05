#ifndef LOGGER_H
#define LOGGER_H

#include <stdint.h>
#include "esp_err.h"

// Maximum entries per log file before rotation
#define MAX_LOG_ENTRIES 100000

// Initialize the logger system
esp_err_t logger_init(const char *partition_label);

// Log a temperature reading
esp_err_t logger_log_temperature(float temperature);

// Get log data (returns CSV format)
esp_err_t logger_get_data(char *buffer, size_t buffer_size, size_t *data_len);

// Get current log entry count
uint32_t logger_get_entry_count(void);

// Clear all log data
esp_err_t logger_clear_data(void);

#endif // LOGGER_H
