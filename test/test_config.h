// Central test/debug configuration.
// Keep all local diagnostic switches here so they can be toggled from one place.
#ifndef TEST_CONFIG_H
#define TEST_CONFIG_H

// If the symbol isn't provided by the build system, default to 0 (disabled).
#ifndef RUN_ALL_TESTS
#define RUN_ALL_TESTS 0
#endif

// Keyboard path diagnostics (WebSocket receive, queue dispatch, row updates).
#ifndef KEYBOARD_DEBUG
#define KEYBOARD_DEBUG 1
#endif

// Display performance diagnostics (FPS + CPU/video timing log).
#ifndef DISPLAY_FPS_DEBUG
#define DISPLAY_FPS_DEBUG 0
#endif

// Audio diagnostics stats logging (periodic write/peak summary).
#ifndef AUDIO_DIAGNOSTICS_DEBUG
#define AUDIO_DIAGNOSTICS_DEBUG 0
#endif

// Memory monitor diagnostics (MemMon task)
#ifndef MEMMON_DEBUG
#define MEMMON_DEBUG 0
#endif

#endif // TEST_CONFIG_H
