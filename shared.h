#ifndef SHARED_H
#define SHARED_H

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#define SENSOR_PIN 14

typedef struct {
    uint8_t state;
    uint32_t timestamp_ms;
} sensor_event_t;

extern SemaphoreHandle_t sensorSemaphore;
extern QueueHandle_t sensorEventQueue;
extern QueueHandle_t captureQueue;

#endif
