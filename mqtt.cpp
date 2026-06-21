#include "mqtt.h"
#include "shared.h"
#include "logging.h"
#include "wifi_config.h"
#include <WiFi.h>
#include <PubSubClient.h>

static WiFiClient wifiClient;
static PubSubClient mqttClient;
static char mqttClientId[32];

static void mqttCallback(char* topic, uint8_t* payload, unsigned int length) {
    String t = String(topic);
    if (t == "foco/control") {
        String msg = "";
        for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
        if (msg == "1") {
            digitalWrite(ENABLE_PIN, HIGH);
            delay(50);
            digitalWrite(RELAY_PIN, RELAY_ON);
            logPrintln("Relé: ENCENDIDO por MQTT");
        } else {
            digitalWrite(RELAY_PIN, RELAY_OFF);
            digitalWrite(ENABLE_PIN, LOW);
            logPrintln("Relé: APAGADO por MQTT");
        }
    }
}

void mqttTask(void *pvParams) {
    sensor_event_t evt;

    pinMode(ENABLE_PIN, OUTPUT);
    digitalWrite(ENABLE_PIN, LOW);
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, RELAY_OFF);
    logPrintln("Relé + Convertidor inicializados (APAGADO)");

    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(mqttClientId, sizeof(mqttClientId), "esp-%02X%02X%02X%02X%02X%02X",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    logPrintln("MQTT client ID: " + String(mqttClientId));

    mqttClient.setClient(wifiClient);
    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);

    while (1) {
        if (!mqttClient.connected()) {
            logPrintln("MQTT: Intentando conectar al broker de internet...");
            if (mqttClient.connect(mqttClientId)) {
                logPrintln("MQTT: ¡Conectado con éxito!");
                mqttClient.subscribe("foco/control");
                logPrintln("MQTT: Suscrito a foco/control");
            } else {
                logPrintln("MQTT: Falló conexión, reintentando en 5s. Estado: " + String(mqttClient.state()));
                delay(5000);
                continue;
            }
        }

        mqttClient.loop();

        if (xQueueReceive(mqttEventQueue, &evt, pdMS_TO_TICKS(100)) == pdTRUE) {
            const char *payload = (evt.state == HIGH) ? "ABIERTA" : "CERRADA";

            if (mqttClient.publish(MQTT_TOPIC, payload)) {
                logPrintln("MQTT publicado en la nube: " + String(payload));
            } else {
                logPrintln("MQTT: Error al publicar");
            }
        }

        ads_data_t ads;
        if (xQueueReceive(adsDataQueue, &ads, 0) == pdTRUE) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%.2f", ads.voltaje);
            mqttClient.publish("tension", buf);
            snprintf(buf, sizeof(buf), "%.2f", ads.corriente);
            mqttClient.publish("corriente", buf);
        }
    }
}


void initMqtt() {
    xTaskCreatePinnedToCore(mqttTask, "MqttTask", 4096, NULL, 1, NULL, 1);
    logPrintln("MQTT Task creada");
}
