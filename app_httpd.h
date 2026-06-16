#ifndef APP_HTTPD_H
#define APP_HTTPD_H

#include "esp_http_server.h"

#define CONFIG_LED_MAX_INTENSITY 255

extern httpd_handle_t stream_httpd;
extern httpd_handle_t camera_httpd;
extern int led_duty;
extern bool isStreaming;

void startCameraServer();
void setupLedFlash(int pin);
void enable_led(bool en);

#endif
