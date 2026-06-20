#include "wifi_config.h"
#include <WiFi.h>

#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"
#include "app_httpd.h"
#include "shared.h"
#include "logging.h"
#include "tasks.h"
#include "mqtt.h"
#include "ads.h"
#include "esp_camera.h"

static camera_config_t cameraConfig = {
    .pin_pwdn = PWDN_GPIO_NUM,
    .pin_reset = RESET_GPIO_NUM,
    .pin_xclk = XCLK_GPIO_NUM,
    .pin_sscb_sda = SIOD_GPIO_NUM,
    .pin_sscb_scl = SIOC_GPIO_NUM,
    .pin_d7 = Y9_GPIO_NUM,
    .pin_d6 = Y8_GPIO_NUM,
    .pin_d5 = Y7_GPIO_NUM,
    .pin_d4 = Y6_GPIO_NUM,
    .pin_d3 = Y5_GPIO_NUM,
    .pin_d2 = Y4_GPIO_NUM,
    .pin_d1 = Y3_GPIO_NUM,
    .pin_d0 = Y2_GPIO_NUM,
    .pin_vsync = VSYNC_GPIO_NUM,
    .pin_href = HREF_GPIO_NUM,
    .pin_pclk = PCLK_GPIO_NUM,
    .xclk_freq_hz = 20000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,
    .pixel_format = PIXFORMAT_JPEG,
    .frame_size = FRAMESIZE_VGA,
    .jpeg_quality = 12,
    .fb_count = 2,
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
};

static void initWiFi() {
    IPAddress local_IP(LOCAL_IP);
    IPAddress gateway(GATEWAY);
    IPAddress subnet(SUBNET);
    IPAddress dns(DNS_PRIMARY);

    if (!WiFi.config(local_IP, gateway, subnet, dns)) {
        logPrintln("ERROR: Fallo configurar IP fija");
    } else {
        logPrintln("IP fija configurada: " + local_IP.toString());
    }

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    logPrint("Conectando a WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        logPrint(".");
    }
    logPrintln("");
    logPrintln("WiFi conectado!");
    logPrint("IP: ");
    logPrintln(WiFi.localIP().toString());
}

static void initCamera() {
    esp_err_t err = esp_camera_init(&cameraConfig);
    if (err != ESP_OK) {
        logPrintln("ERROR: Fallo init cámara: " + String(err));
        return;
    }
    logPrintln("Cámara inicializada");

    sensor_t *s = esp_camera_sensor_get();
    s->set_framesize(s, FRAMESIZE_VGA);

    setupLedFlash(LED_GPIO_NUM);
    led_duty = CONFIG_LED_MAX_INTENSITY;
    logPrintln("LED flash configurado");

    /*startCameraServer();
    logPrintln("Servidor HTTP de cámara iniciado");*/
}

static void sendInitialState() {
    int initialState = digitalRead(SENSOR_PIN);
    sensor_event_t initEvt = { (uint8_t)initialState, millis() };
    xQueueSend(sensorEventQueue, &initEvt, portMAX_DELAY);
    xQueueSend(mqttEventQueue, &initEvt, portMAX_DELAY);
}

void setup() {
    Serial.begin(115200);
    logPrintln("Iniciando ESP32-CAM FreeRTOS...");

    initWiFi();
    initCamera();
    initTelnetServer();
    initAds();
    pinMode(SENSOR_PIN, INPUT_PULLUP);
    logPrintln("Sensor GPIO configurado como INPUT_PULLUP");
    createRtTasks();
    initMqtt();
    sendInitialState();

    vTaskDelete(NULL);
}

void loop() {
    vTaskDelete(NULL);
}
