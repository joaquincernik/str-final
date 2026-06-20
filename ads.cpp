#include "ads.h"
#include "shared.h"
#include "logging.h"
#include <Wire.h>
#include <Adafruit_ADS1X15.h>

#define SENSITIVITY      680.0f
#define FACTOR_CORRIENTE 11.1f

static Adafruit_ADS1115 ads;

void initAds() {
    Wire.begin(15, 14);
    Wire.setClock(400000);
    delay(10);

    if (!ads.begin(0x48)) {
        logPrintln("ERROR: ADS1115 no detectado en 0x48");
    } else {
        logPrintln("ADS1115 detectado en 0x48");
        ads.setGain(GAIN_TWOTHIRDS);
        ads.setDataRate(RATE_ADS1115_860SPS);
        logPrintln("ADS1115: PGA ±6.144V, 860SPS");
    }
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

void adsTask(void *pvParams) {
    ads_data_t data;
    static float voltaje_filt = 220.0f;
    static float corriente_filt = 0.0f;

    while (1) {
        float rms_sensor_V = calcular_Canal_RMS(0);
        float tension_instantanea = rms_sensor_V * SENSITIVITY;

        float rms_sensor_A = calcular_Canal_RMS(1);
        float corriente_instantanea = rms_sensor_A * FACTOR_CORRIENTE;

        voltaje_filt = 0.8f * voltaje_filt + 0.2f * tension_instantanea;
        corriente_filt = 0.8f * corriente_filt + 0.2f * corriente_instantanea;

        if (corriente_filt < 0.05f) corriente_filt = 0.0f;

        data.voltaje = voltaje_filt;
        data.corriente = corriente_filt;
        xQueueSend(adsDataQueue, &data, 0);

        logPrintln("Tensión: " + String(data.voltaje, 1) + "V, Corriente: " + String(data.corriente, 3) + "A");

        vTaskDelay(pdMS_TO_TICKS(920));
    }
}
