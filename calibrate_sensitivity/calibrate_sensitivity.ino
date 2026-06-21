#include "wifi_config.h"
#include <WiFi.h>
#include <ESPTelnetStream.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include "esp32-hal-ledc.h"

#define ACTUAL_VOLTAGE    227.5f

#define START_SENSITIVITY 0.0f
#define STOP_SENSITIVITY  1000.0f
#define STEP_SENSITIVITY  0.25f
#define TOLERANCE         1.0f

#define MAX_TOLERANCE (ACTUAL_VOLTAGE + TOLERANCE)
#define MIN_TOLERANCE (ACTUAL_VOLTAGE - TOLERANCE)

#define LED_LEDC_CHANNEL 2
#define LED_PIN 4

static Adafruit_ADS1115 ads;
static ESPTelnetStream telnet;

static void logPrint(const String &msg) {
    Serial.print(msg);
    telnet.print(msg);
}

static void logPrintln(const String &msg) {
    Serial.println(msg);
    telnet.println(msg);
}

static void initWiFi() {
    IPAddress local_IP(LOCAL_IP);
    IPAddress gateway(GATEWAY);
    IPAddress subnet(SUBNET);
    IPAddress dns(DNS_PRIMARY);

    if (!WiFi.config(local_IP, gateway, subnet, dns)) {
        logPrintln("ERROR: Fallo configurar IP fija");
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

    ledcSetup(LED_LEDC_CHANNEL, 5000, 8);
    ledcAttachPin(LED_PIN, LED_LEDC_CHANNEL);
    for (int i = 0; i < 4; i++) {
        ledcWrite(LED_LEDC_CHANNEL, 255);
        delay(150);
        ledcWrite(LED_LEDC_CHANNEL, 0);
        delay(150);
    }
    logPrintln("Flash LED: 4 parpadeos = conectado");
}

static float calcular_Canal_RMS(uint8_t canal) {
    float suma = 0.0f;
    float suma_cuadrados = 0.0f;
    uint32_t conteo_muestras = 0;

    uint32_t tiempo_inicio = millis();
    while ((millis() - tiempo_inicio) < 40) {
        float v = ads.computeVolts(ads.readADC_SingleEnded(canal));
        suma += v;
        suma_cuadrados += v * v;
        conteo_muestras++;
    }

    if (conteo_muestras == 0) return 0.0f;

    float media = suma / conteo_muestras;
    float varianza = (suma_cuadrados / conteo_muestras) - (media * media);
    if (varianza < 0.0f) varianza = 0.0f;

    return sqrtf(varianza);
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    initWiFi();

    telnet.begin(23);
    logPrintln("Telnet iniciado en puerto 23");

    Wire.begin(15, 14);
    Wire.setClock(400000);
    delay(10);

    if (!ads.begin(0x48)) {
        logPrintln("ERROR: ADS1115 no detectado en 0x48");
        while (1) delay(100);
    }
    ads.setGain(GAIN_TWOTHIRDS);
    ads.setDataRate(RATE_ADS1115_860SPS);
    logPrintln("ADS1115: PGA ±6.144V, 860SPS");

    logPrint("Tension real (referencia): ");
    logPrint(String(ACTUAL_VOLTAGE));
    logPrintln(" V");

    float sens = START_SENSITIVITY;
    float voltaje = 0.0f;

    if (START_SENSITIVITY == 0.0f) {
        float rms_raw = calcular_Canal_RMS(0);
        sens = ACTUAL_VOLTAGE / rms_raw;
        logPrint("Sensibilidad estimada inicial: ");
        logPrintln(String(sens, 4));
    }

    logPrintln("Barriendo sensibilidad...");

    while (1) {
        if (sens > STOP_SENSITIVITY) {
            logPrintln("ERROR: No se pudo determinar sensibilidad dentro del limite");
            ledcWrite(LED_LEDC_CHANNEL, 0);
            return;
        }

        telnet.loop();

        voltaje = calcular_Canal_RMS(0) * sens;
        logPrint(String(sens, 4));
        logPrint(" => ");
        logPrintln(String(voltaje, 4));

        if (voltaje >= MIN_TOLERANCE && voltaje <= MAX_TOLERANCE) {
            logPrintln("----- ENCONTRADO -----");
            logPrint("SENSITIVITY = ");
            logPrintln(String(sens, 10));
            logPrint("Voltaje medido: ");
            logPrint(String(voltaje, 4));
            logPrintln(" V");
            return;
        }

        sens += STEP_SENSITIVITY;
        delay(50);
    }
}

void loop() {
    telnet.loop();
    delay(10);
}
