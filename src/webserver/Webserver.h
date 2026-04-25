#ifndef SPECK10TRUM_WEBSERVER_H
#define SPECK10TRUM_WEBSERVER_H

#include <esp_err.h>
#include "spectrum/SpectrumBase.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t webserver_start(SpectrumBase* spectrum);
void webserver_stop(void);

// Apply any pending reset/snapshot-load requested via the HTTP API.
// Must be called from the emulator task between frames so spectrum state
// is mutated without racing the running CPU.
void webserver_apply_pending(SpectrumBase* spectrum);

#ifdef __cplusplus
}
#endif

#endif // SPECK10TRUM_WEBSERVER_H
