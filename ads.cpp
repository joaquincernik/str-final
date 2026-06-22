#include "ads.h"
#include "shared.h"
#include "logging.h"
#include <Wire.h>
#include <Adafruit_ADS1X15.h>

#define FACTOR_VOLTAJE   513.6945800781f
#define FACTOR_CORRIENTE 0.0916f

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
    }
}

static float calcular_RMS(uint8_t canal) {
    int32_t t0 = micros();
    float Vsum = 0.0f;
    uint32_t conteo = 0;

    while (micros() - t0 < 20000) {
        Vsum += ads.computeVolts(ads.readADC_SingleEnded(canal));
        conteo++;
    }
    if (conteo == 0) return 0.0f;
    float zeroPoint = Vsum / conteo;

    t0 = micros();
    double sum_sq = 0.0;
    conteo = 0;
    while (micros() - t0 < 20000) {
        float v = ads.computeVolts(ads.readADC_SingleEnded(canal)) - zeroPoint;
        sum_sq += (double)(v * v);
        conteo++;
    }
    if (conteo == 0) return 0.0f;

    return sqrtf((float)(sum_sq / conteo));
}

void adsTask(void *pvParams) {
    ads_data_t data;
    static float voltaje_filt = 220.0f;
    static float corriente_filt = 0.0f;

    while (1) {
        float rms_sensor_V = calcular_RMS(0);
        taskYIELD(); //cede el paso para que otras tareas se ejecuten, particularmente los logs en telnet y demas
        float rms_sensor_A = calcular_RMS(1);

        float tension_instantanea = rms_sensor_V * FACTOR_VOLTAJE;
        float corriente_instantanea = rms_sensor_A / FACTOR_CORRIENTE;
        voltaje_filt = 0.6f * voltaje_filt + 0.4f * tension_instantanea;
        corriente_filt = 0.6f * corriente_filt + 0.4f * corriente_instantanea;

        if (corriente_filt < 0.015f) corriente_filt = 0.0f;

        data.voltaje = voltaje_filt;
        data.corriente = corriente_filt;
        xQueueSend(adsDataQueue, &data, 0);
        logPrintln("Tension: " + String(data.voltaje, 1) + "V, Corriente: " + String(data.corriente, 3) + "A");

        vTaskDelay(pdMS_TO_TICKS(600));
    }
}
