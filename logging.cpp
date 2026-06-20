#include "logging.h"
#include "shared.h"
#include <ESPTelnetStream.h>

static ESPTelnetStream telnet;

void logPrint(const String &msg) {
    Serial.print(msg);
    telnet.print(msg);
}

void logPrintln(const String &msg) {
    Serial.println(msg);
    telnet.println(msg);
}

void initTelnetServer() {
    telnet.onConnect([](String ip) {
        Serial.println("Nuevo cliente Telnet conectado desde " + ip);
        telnet.println("Conectado a ESP32-CAM Telnet Log (Sensor de Apertura)");
        telnet.println("-----------------------------------------------------");
    });
    telnet.begin(23);
    logPrintln("Servidor Telnet iniciado en puerto 23");
}

void telnetTask(void *pvParams) {
    while (1) {
        telnet.loop();

        if (telnet.available()) {
            String cmd = telnet.readStringUntil('\n');
            cmd.trim();
            if (cmd.length() > 0) {
                logPrint("Comando recibido: ");
                logPrintln(cmd);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
