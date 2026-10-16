#pragma once
/* G2-02: Contrato compartido de acciones. No altera diccionario KORVO v2.
 * Copiar este archivo IDENTICO a KORVO y ESP32 Audio al migrarlas.
 * VERSION 1 es independiente de KORVO_PROTOCOL_DICTIONARY_VERSION 2.
 * Reservado: ninguna funcion de transporte se activa en G2-02.
 */
#include <stdint.h>
#define GATEWAY_ACTION_PROTOCOL_VERSION 1u
#define GATEWAY_ACTION_PROTOCOL_MAGIC 0x47324131u /* G2A1 */
typedef enum {
    G2_ACTION_NONE = 0,
    G2_ACTION_CALL_START = 1,
    G2_ACTION_CALL_END = 2,
    G2_ACTION_DOOR_UNLOCK = 3,
    G2_ACTION_AUDIO_START = 4,
    G2_ACTION_AUDIO_STOP = 5
} g2_action_id_t;
typedef enum {
    G2_RESULT_OK = 0,
    G2_RESULT_SIP_NOT_REGISTERED = 1,
    G2_RESULT_AUDIO_OFFLINE = 2,
    G2_RESULT_MIC_UNAVAILABLE = 3,
    G2_RESULT_OUTPUT_UNAVAILABLE = 4,
    G2_RESULT_GATEWAY_BUSY = 5,
    G2_RESULT_GATEWAY_OFFLINE = 6,
    G2_RESULT_DOOR_REJECTED = 7,
    G2_RESULT_DOOR_EXECUTION_FAILED = 8,
    G2_RESULT_AUDIO_INTERRUPTED = 9,
    G2_RESULT_PROTOCOL_MISMATCH = 10,
    G2_RESULT_TIMEOUT = 11,
    G2_RESULT_NOT_AUTHORIZED = 12,
    G2_RESULT_INTERNAL_ERROR = 13
} g2_action_result_t;
/* La HMI solo muestra MsgBox por resultado de una ACCION fallida.
 * Heartbeat/estados normales actualizan banderas, no generan MsgBox.
 * Los resultados de control deben autenticarse y correlacionarse con
 * identificador de accion; no usar este header como parser de red.
 */
