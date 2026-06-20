#ifndef SHARED_H
#define SHARED_H

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#define SENSOR_PIN 1

#define RELAY_PIN 12
#define RELAY_ON LOW
#define RELAY_OFF HIGH

typedef struct {
    uint8_t state;
    uint32_t timestamp_ms;
} sensor_event_t;

typedef struct {
    float voltaje;
    float corriente;
} ads_data_t;

extern SemaphoreHandle_t sensorSemaphore;
extern QueueHandle_t sensorEventQueue;
extern QueueHandle_t captureQueue;
extern QueueHandle_t mqttEventQueue;
extern QueueHandle_t adsDataQueue;

#endif
