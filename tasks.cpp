#include "tasks.h"
#include "shared.h"
#include "logging.h"
#include "app_httpd.h"
#include "ads.h"
#include "esp_camera.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

SemaphoreHandle_t sensorSemaphore = NULL;
QueueHandle_t sensorEventQueue = NULL;
QueueHandle_t captureQueue = NULL;
QueueHandle_t mqttEventQueue = NULL;
QueueHandle_t adsDataQueue = NULL;

static void IRAM_ATTR sensor_isr() {
    BaseType_t higherTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(sensorSemaphore, &higherTaskWoken);
    if (higherTaskWoken) { //significa que se abrio la puerta
        portYIELD_FROM_ISR();  //despertamos al sensor task
    }
}

static void sensorTask(void *pvParams) {
    sensor_event_t evt;
    uint32_t lastValidTime = 0;
    int lastState = -1;

    while (1) {
        //microsegundos -> ticks pdMS_TO_TICKS(1000)
        if (xSemaphoreTake(sensorSemaphore, pdMS_TO_TICKS(1000)) == pdTRUE) {
            //esto ocurre solamnte cuando el semaforo nos da la senal , que se genera en la interrupcion
            uint32_t now = millis();
            if (now - lastValidTime < 200) continue;
            lastValidTime = now;

            evt.state = digitalRead(SENSOR_PIN);
            if (evt.state == lastState) continue;
            lastState = evt.state;

            evt.timestamp_ms = now;
            if (xQueueSend(sensorEventQueue, &evt, pdMS_TO_TICKS(100)) != pdTRUE) {
                //espera 100ms hasta que se libere la cola, si no descarta el evento 
                //si puede meter el evento a la cola devuelve pdTRUE
                logPrintln("WARN: sensorEventQueue llena, descartando evento");
            }
            xQueueSend(mqttEventQueue, &evt, pdMS_TO_TICKS(100));
        }
    }
}

static void captureTask(void *pvParams) {
    sensor_event_t evt;

    while (1) {
        if (xQueueReceive(sensorEventQueue, &evt, portMAX_DELAY) == pdTRUE) {
            if (evt.state == HIGH) {
                logPrintln("Sensor: ABIERTO — capturando foto...");

                led_duty = CONFIG_LED_MAX_INTENSITY;
                enable_led(true);
                vTaskDelay(pdMS_TO_TICKS(150));

                camera_fb_t *fb = NULL;
                for (int retry = 0; retry < 20; retry++) {
                    fb = esp_camera_fb_get();
                    if (fb) break;
                    vTaskDelay(pdMS_TO_TICKS(100));
                }

                if (fb) {
                    if (xQueueSend(captureQueue, &fb, pdMS_TO_TICKS(1000)) != pdTRUE) {
                        logPrintln("ERROR: captureQueue llena, descartando frame");
                        esp_camera_fb_return(fb);
                    }
                } else {
                    logPrintln("ERROR: Fallo captura de cámara (timeout 2s)");
                }

                enable_led(false);
            } else {
                logPrintln("Sensor: CERRADO");
            }
        }
    }
}

static void httpTask(void *pvParams) {
    camera_fb_t *fb;
    const char *boundary = "----ESP32CAM";
    const char *url = "https://cernikiw3.chickenkiller.com/foto";

    while (1) {
        if (xQueueReceive(captureQueue, &fb, portMAX_DELAY) == pdTRUE) {
            String head = "--" + String(boundary) + "\r\n"
                "Content-Disposition: form-data; name=\"file\"; filename=\"photo.jpg\"\r\n"
                "Content-Type: image/jpeg\r\n\r\n";

            String tail = "\r\n--" + String(boundary) + "--\r\n";

            size_t bodyLen = head.length() + fb->len + tail.length();
            uint8_t *body = (uint8_t *)malloc(bodyLen);
            if (!body) {
                logPrintln("ERROR: malloc falló para body HTTP");
                esp_camera_fb_return(fb);
                continue;
            }

            memcpy(body, head.c_str(), head.length());
            memcpy(body + head.length(), fb->buf, fb->len);
            memcpy(body + head.length() + fb->len, tail.c_str(), tail.length());

            int httpCode = -1;
            for (int attempt = 0; attempt < 3 && httpCode <= 0; attempt++) {
                if (attempt > 0) {
                    logPrintln("Reintento " + String(attempt + 1) + "/3...");
                    vTaskDelay(pdMS_TO_TICKS(1000));
                }

                WiFiClientSecure client;
                client.setInsecure();

                HTTPClient http;
                http.begin(client, url);
                http.setTimeout(15000);
                http.addHeader("Content-Type", "multipart/form-data; boundary=" + String(boundary));
                httpCode = http.sendRequest("POST", body, bodyLen);

                if (httpCode > 0) {
                    String response = http.getString();
                    logPrintln("HTTP " + String(httpCode) + ": " + response);
                } else {
                    logPrintln("HTTP error (intento " + String(attempt + 1) + "): " + http.errorToString(httpCode));
                }

                http.end();
            }

            free(body);
            esp_camera_fb_return(fb);
        }
    }
}

void createRtTasks() {
    sensorSemaphore = xSemaphoreCreateBinary();
    sensorEventQueue = xQueueCreate(20, sizeof(sensor_event_t));
    captureQueue = xQueueCreate(10, sizeof(camera_fb_t *));
    mqttEventQueue = xQueueCreate(5, sizeof(sensor_event_t));
    adsDataQueue = xQueueCreate(5, sizeof(ads_data_t));

    attachInterrupt(digitalPinToInterrupt(SENSOR_PIN), sensor_isr, CHANGE);
    logPrintln("Interrupción GPIO configurada");

    xTaskCreatePinnedToCore(sensorTask,  "SensorTask",  2048, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(captureTask, "CaptureTask", 4096, NULL, 4, NULL, 0);
    xTaskCreatePinnedToCore(httpTask,    "HttpTask",    8192, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(telnetTask,  "TelnetTask",  4096, NULL, 3, NULL, 1);
    xTaskCreatePinnedToCore(adsTask,     "AdsTask",     4096, NULL, 1, NULL, 1);
    logPrintln("Tareas FreeRTOS creadas. Sistema listo.");
}
