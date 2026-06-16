#include "logging.h"
#include "shared.h"
#include <vector>

static WiFiServer telnetServer(23);
static std::vector<WiFiClient> telnetClients;
static SemaphoreHandle_t clientsMutex = NULL;

static void logToClients(const String &msg) {
    if (clientsMutex == NULL) return;
    xSemaphoreTake(clientsMutex, portMAX_DELAY);
    for (int i = telnetClients.size() - 1; i >= 0; i--) {
        if (telnetClients[i].connected()) {
            telnetClients[i].print(msg);
        } else {
            telnetClients[i].stop();
            telnetClients.erase(telnetClients.begin() + i);
        }
    }
    xSemaphoreGive(clientsMutex);
}

void logPrint(const String &msg) {
    Serial.print(msg);
    logToClients(msg);
}

void logPrintln(const String &msg) {
    Serial.println(msg);
    logToClients(msg + "\r\n");
}

void initTelnetServer() {
    clientsMutex = xSemaphoreCreateMutex();
    telnetServer.begin();
    telnetServer.setNoDelay(true);
    logPrintln("Servidor Telnet iniciado en puerto 23");
}

void telnetTask(void *pvParams) {
    while (1) {
        xSemaphoreTake(clientsMutex, portMAX_DELAY);

        WiFiClient newClient = telnetServer.available();
        if (newClient) {
            telnetClients.push_back(newClient);
            WiFiClient &c = telnetClients.back();
            c.println("Conectado a ESP32-CAM Telnet Log (Sensor de Apertura)");
            c.println("-----------------------------------------------------");
            Serial.println("Nuevo cliente Telnet conectado");
            c.print("Estado actual: ");
            c.println(digitalRead(SENSOR_PIN) == HIGH ? "ABIERTO" : "CERRADO");
        }

        for (int i = telnetClients.size() - 1; i >= 0; i--) {
            if (telnetClients[i].connected()) {
                if (telnetClients[i].available()) {
                    String cmd = telnetClients[i].readStringUntil('\n');
                    cmd.trim();
                    if (cmd.length() > 0) {
                        xSemaphoreGive(clientsMutex);
                        logPrint("Comando recibido: ");
                        logPrintln(cmd);
                        xSemaphoreTake(clientsMutex, portMAX_DELAY);
                    }
                }
            } else {
                telnetClients[i].stop();
                telnetClients.erase(telnetClients.begin() + i);
            }
        }

        xSemaphoreGive(clientsMutex);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
