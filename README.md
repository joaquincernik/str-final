# Gestión de Tiempo Real con FreeRTOS

## Arquitectura General

El sistema ejecuta 6 tareas FreeRTOS más una ISR, todas creadas en `createRtTasks()` (`tasks.cpp:141`). El planificador de FreeRTOS utiliza **scheduling apropiativo (preemptive) con round-robin** entre tareas de igual prioridad. No se emplea tickless idle; el tick de FreeRTOS interrumpe periódicamente para decidir cambios de contexto.

Cada tarea se asigna a un núcleo específico mediante `xTaskCreatePinnedToCore()`, aprovechando la arquitectura dual-core del ESP32. El **Core 0** (pro-core) ejecuta las tareas críticas de sensado y captura; el **Core 1** (app-core) ejecuta las tareas de comunicación red y logging.

---

## Tareas (Hilos)

| Tarea | Prioridad | Core | Stack (bytes) | Comportamiento |
|-------|-----------|------|---------------|----------------|
| `SensorTask` | 5 (máxima) | 0 | 2048 | Bloqueada en un semáforo binario. La ISR la despierta al detectar un flanco en el sensor magnético (`CHANGE`). Aplica debounce de 200ms, filtra cambios redundantes, y encola eventos en `sensorEventQueue` y `mqttEventQueue`. |
| `CaptureTask` | 4 | 0 | 4096 | Bloqueada en `sensorEventQueue`. Al recibir un evento HIGH: enciende LED 150ms, captura foto (polling 20×100ms = 2s timeout), envía el buffer a `captureQueue`. En LOW: solo loguea. |
| `TelnetTask` | 3 | 1 | 4096 | Polling cada 10ms (`vTaskDelay(10)`). Procesa `telnet.loop()` y comandos entrantes. No es crítica en tiempo real. |
| `HttpTask` | 2 | 1 | 8192 | Bloqueada en `captureQueue`. Al recibir un frame, construye multipart/form-data y hace HTTP POST con 3 reintentos, 1s entre reintentos, 15s de timeout. Usa `WiFiClientSecure::setInsecure()`. |
| `MqttTask` | 1 (mínima) | 1 | 4096 | Bucle permanente: mantiene conexión MQTT (PubSubClient), recibe de `mqttEventQueue` con timeout de 100ms, publica `ABIERTA`/`CERRADA` en `estado-puerta` (QoS 0). También publica tensión y corriente desde `adsDataQueue`. |
| `AdsTask` | 1 | 1 | 4096 | Cada 600ms lee el ADC ADS1115 por I2C (cálculo RMS en ventana de 20ms), filtra con EMA, envía a `adsDataQueue`. |

---

## Política de Planificación (Scheduling)

FreeRTOS en ESP32 usa por defecto **`configUSE_PREEMPTION = 1`** y **`configUSE_TIME_SLICING = 1`**:

1. **Apropiativo por prioridad**: una tarea de mayor prioridad (ej. SensorTask, prio 5) desaloja inmediatamente a una de menor prioridad (ej. MqttTask, prio 1) en cuanto puede ejecutarse.
2. **Round-robin entre igual prioridad**: si varias tareas comparten prioridad, el tick las turna en tiempo compartido. En este proyecto no hay dos tareas con la misma prioridad, por lo que el round-robin no se aplica.
3. **Tareas bloqueadas**: la mayoría de las tareas pasan la mayor parte del tiempo bloqueadas en colas (`xQueueReceive` con `portMAX_DELAY`) o semáforos (`xSemaphoreTake`), lo que permite que el idle task consuma CPU o que el ESP32 entre en modo de bajo consumo.

No se utiliza `vTaskSuspend` / `vTaskResume` ni notificaciones directas; toda la sincronización se basa en primitivas de FreeRTOS (colas y semáforos).

---

## Tipo de Sistema en Tiempo Real: **Suave (Soft Real-Time)**

El sistema es de **tiempo real suave (soft real-time)** por las siguientes razones:

- La violación ocasional de un deadline no causa fallo catastrófico. Si la captura falla (timeout 2s), solo se pierde una foto. Si el HTTP POST falla tras 3 reintentos, se descarta el frame.
- No hay mecanismos de temporización explícitos (`xTimer`, `xTaskDelayUntil`) para garantizar periodicidad estricta.
- El sistema tolera la pérdida de eventos: si `sensorEventQueue` está llena (10 elementos), el nuevo evento se descarta y se loguea una advertencia.

No es un sistema **hard real-time** porque no existen deadlines de los que dependa la seguridad o integridad del sistema.

---

## Sincronía vs Asincronía: **Event-Driven Asíncrono**

El sistema es completamente **asíncrono y manejado por eventos (event-driven)**:

1. **ISR → SensorTask**: el sensor magnético genera una interrupción por flanco (`CHANGE`). La ISR libera un semáforo binario (`sensorSemaphore`). Este es el único punto de entrada de eventos externos.
2. **SensorTask → CaptureTask**: los eventos se propagan mediante `sensorEventQueue` (cola de mensajes con `sensor_event_t{state, timestamp_ms}`).
3. **SensorTask → MqttTask**: paralelamente, el mismo evento se envía a `mqttEventQueue`.
4. **CaptureTask → HttpTask**: los punteros a `camera_fb_t` se envían mediante `captureQueue`.

No existe un loop central sincrónico ni polling de sensores (salvo `TelnetTask` y `AdsTask`, que son las únicas tareas basadas en temporización periódica).

### Task | Tipo
|------|------|
| `SensorTask` | Event-driven (semáforo desde ISR) |
| `CaptureTask` | Event-driven (cola de mensajes) |
| `HttpTask` | Event-driven (cola de mensajes) |
| `MqttTask` | Híbrido: event-driven (cola) + polling MQTT (loop + delay 100ms) |
| `TelnetTask` | Time-driven (polling cada 10ms) |
| `AdsTask` | Time-driven (delay fijo de 600ms) |

---

## Memoria Compartida

No se utiliza memoria compartida protegida por mutex entre las tareas de la cadena sensor→captura→HTTP. En su lugar, los datos se transfieren **exclusivamente mediante colas de mensajes** (`QueueHandle_t`), lo que garantiza:

- **Copia de datos** (`sensor_event_t` se copia por valor en la cola).
- **Transferencia de ownership** (el puntero `camera_fb_t*` se pasa de `CaptureTask` a `HttpTask`; `HttpTask` es responsable de liberar el buffer con `esp_camera_fb_return()`).
- **Sin condiciones de carrera**: las colas de FreeRTOS son intrínsecamente thread-safe.

El único caso de memoria compartida es la variable global `led_duty` (declarada en `app_httpd.h`), que es un `int` atómico en ESP32 (escritura/lectura de 32 bits es atómica en Xtensa) accedida desde `CaptureTask` y desde los handlers HTTP en `app_httpd.cpp`. No hay protección explícita porque las escrituras son atómicas y no hay incrementos compuestos.

No hay regiones de memoria crítica compartida (pool de buffers, DMA, etc.) más allá del driver interno de la cámara.

---

## Primitivas de Sincronización

| Primitiva | Tipo | Propósito | Creada en |
|-----------|------|-----------|-----------|
| `sensorSemaphore` | Semáforo binario | Despertar a `SensorTask` desde la ISR. Modelo productor-consumer (ISR → tarea). | `tasks.cpp:142` |
| `sensorEventQueue` | Cola (10 × `sensor_event_t`) | Enviar eventos de sensor desde `SensorTask` → `CaptureTask`. | `tasks.cpp:143` |
| `captureQueue` | Cola (10 × `camera_fb_t*`) | Transferir buffers de cámara desde `CaptureTask` → `HttpTask`. | `tasks.cpp:144` |
| `mqttEventQueue` | Cola (5 × `sensor_event_t`) | Enviar eventos desde `SensorTask` → `MqttTask`. | `tasks.cpp:145` |
| `adsDataQueue` | Cola (5 × `ads_data_t`) | Enviar lecturas del ADS1115 desde `AdsTask` → `MqttTask`. | `tasks.cpp:146` |

No se usan mutexes en el código de aplicación (el `clientsMutex` en `logging.cpp` es interno de la librería `ESPTelnetStream` y no se expone).

### Patrón de uso de colas

- **`xQueueSend` con timeout finito** (100–1000ms) en lugar de `portMAX_DELAY` para evitar deadlocks si la cola destino está llena. `tasks.cpp:40,43,68`.
- **`xQueueReceive` con `portMAX_DELAY`** en las tareas consumer (`CaptureTask`, `HttpTask`) para bloquear indefinidamente hasta que lleguen datos. `tasks.cpp:52,90`.
- **`mqttEventQueue`** usa `xQueueReceive` con timeout de 100ms en `MqttTask`, combinando recepción de eventos con mantenimiento de conexión MQTT. `mqtt.cpp:65`.

---

## ISR (Interrupt Service Routine)

```c
static void IRAM_ATTR sensor_isr() {
    BaseType_t higherTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(sensorSemaphore, &higherTaskWoken);
    if (higherTaskWoken) {
        portYIELD_FROM_ISR();
    }
}
```

- Adjunta al pin del sensor magnético en flanco `CHANGE` mediante `attachInterrupt()` (`tasks.cpp:148`).
- No usa `gpio_install_isr_service()` de ESP-IDF para evitar conflicto con `attachInterrupt` de Arduino y el driver de cámara.
- La ISR es mínima: solo libera un semáforo. Todo el procesamiento (debounce, lectura de pin, encolado) se delega a `SensorTask`.
- Incluye `portYIELD_FROM_ISR()` condicional para que el planificador ejecute inmediatamente `SensorTask` si tiene mayor prioridad que la tarea interrumpida.

---

## Medición de Corriente y Tensión (ADS1115)

### Hardware

Se utiliza un **ADS1115** (ADC de 16 bits, I2C) conectado en `0x48` por I2C (pines 15-SDA, 14-SCL, clock 400kHz). Mide dos canales:

| Canal | Señal | Acondicionamiento |
|-------|-------|-------------------|
| A0    | Tensión de red | Divisor resistivo, factor = 513.69 |
| A1    | Corriente | Sensor de efecto Hall (ACS712 o similar), factor = 0.0916 |

### Cálculo RMS (ventana deslizante de 20ms)

La función `calcular_RMS()` en `ads.cpp:25-48` implementa un **algoritmo RMS de dos pasadas** sobre una ventana fija de **20ms** para capturar exactamente un ciclo de 50Hz:

1. **Primera pasada** — recorre ~20ms leyendo muestras a 860 SPS (la tasa máxima del ADS1115), acumulando valores de tensión para calcular el `zeroPoint` (offset DC promedio).

2. **Segunda pasada** — recorre otros ~20ms restando el offset DC a cada muestra, elevando al cuadrado, sumando, y finalmente calculando `sqrt(sum_sq / count)`.

```c
float zeroPoint = Vsum / conteo;
// ...
double sum_sq = 0.0;
while (micros() - t0 < 20000) {
    float v = ads.computeVolts(ads.readADC_SingleEnded(canal)) - zeroPoint;
    sum_sq += (double)(v * v);
    conteo++;
}
return sqrtf((float)(sum_sq / conteo));
```

### Conversión a ingeniería

```c
#define FACTOR_VOLTAJE   513.6945800781f  // rms_sensor_V * 513.69 = Vrms
#define FACTOR_CORRIENTE   0.0916f         // rms_sensor_A / 0.0916 = Arms
```

### Filtrado EMA (Exponential Moving Average)

En `adsTask()` (`ads.cpp:50-73`), cada 600ms se aplica un filtro EMA suave para eliminar ruido transitorio:

```c
voltaje_filt   = 0.6f * voltaje_filt   + 0.4f * tension_instantanea;
corriente_filt = 0.6f * corriente_filt + 0.4f * corriente_instantanea;
if (corriente_filt < 0.015f) corriente_filt = 0.0f;  // umbral de histéresis
```

Los resultados se publican por MQTT en los tópicos `tension` y `corriente` con 1 decimal para tensión y 3 para corriente.

## Consideraciones sobre el Tiempo Real

- **Latencia ISR → SensorTask**: mínima: la ISR libera el semáforo y hace `portYIELD`. Como `SensorTask` tiene la máxima prioridad (5) en el Core 0, se ejecuta inmediatamente después de la ISR.
- **Debounce de 200ms**: evita falsos positivos por rebote mecánico del sensor. Esto establece un límite inferior en la separación mínima entre eventos detectables (~200ms).
- **Timeout de captura**: `esp_camera_fb_get()` se invoca con polling de 20 iteraciones × 100ms = 2s. Si no hay fotograma disponible (ej. la cámara ya está siendo usada por el stream HTTP), se descarta el evento.
- **Colas acotadas**: `captureQueue` (10) y `sensorEventQueue` (10) definen el backlog máximo. Si el servidor HTTP está caído, se pueden acumular hasta 10 frames sin pérdida.
- **Sin jitter garantizado**: no se usa `xTaskDelayUntil()` para períodos exactos. `AdsTask` usa `vTaskDelay(600)` que tiene jitter dependiente de la carga del sistema.
