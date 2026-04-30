#ifndef SPECK10TRUM_WEBSERVER_H
#define SPECK10TRUM_WEBSERVER_H

#include <esp_err.h>
#include "spectrum/SpectrumBase.h"
#include "command_queue.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t webserver_start(SpectrumBase* spectrum);
void webserver_stop(void);
bool webserver_is_running(void);
esp_err_t webserver_ensure_started(SpectrumBase* spectrum);

// Block until a WebSocket client connects, or timeout expires.
bool webserver_wait_for_ws_client(uint32_t timeout_ms);

// Check if a WebSocket client is currently connected (non-blocking).
bool webserver_is_ws_client_connected(void);

// Expose command queue accessor for use in main/emulator
WebCommandQueue& webserver_get_command_queue();

#ifdef __cplusplus
}
#endif

#endif // SPECK10TRUM_WEBSERVER_H
