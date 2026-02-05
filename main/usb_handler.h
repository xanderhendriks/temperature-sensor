#ifndef USB_HANDLER_H
#define USB_HANDLER_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

esp_err_t usb_init(void);
void usb_task(void *pvParameters);

#endif
