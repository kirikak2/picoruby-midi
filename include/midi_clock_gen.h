/*
 * PicoRuby MIDI - OS-free MIDI Clock generator
 *
 * Tracks BPM/period and emits a 24 PPQ MIDI clock byte each time
 * midi_clock_gen_tick(now_us) is called past the next scheduled
 * emission point. The core has no esp_timer / FreeRTOS dependency:
 * the port (or a Ruby poll loop) drives it.
 */

#ifndef MIDI_CLOCK_GEN_DEFINED_H_
#define MIDI_CLOCK_GEN_DEFINED_H_

#include <stdint.h>
#include <stdbool.h>
#include "midi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Send callback. Emits one MIDI Timing Clock byte (0xF8) over whatever
 * transport(s) the port wires up. NULL disables emission.
 */
typedef void (*midi_clock_gen_send_fn)(void);

/* Reset internal state to defaults (120 BPM, not running). */
void midi_clock_gen_init(void);

/* Same as init(); kept for symmetry with the other core modules. */
void midi_clock_gen_deinit(void);

/* Set BPM. Clamped to [20.0, 300.0]. Updates the internal period. */
void midi_clock_gen_set_bpm(float bpm);

/* Currently configured BPM. */
float midi_clock_gen_get_bpm(void);

/* Currently configured 24 PPQ period in microseconds. */
uint64_t midi_clock_gen_get_period_us(void);

/* Register the send callback for clock-byte emission. */
void midi_clock_gen_set_send_callback(midi_clock_gen_send_fn cb);

/* Register a user callback invoked once per emitted tick (after the
 * clock byte has been sent). NULL disables.
 */
void midi_clock_gen_set_user_callback(midi_clock_callback_t cb);

/* Mark the clock as running and anchor the next emission at now_us. */
void midi_clock_gen_start(uint64_t now_us);

/* Mark the clock as stopped. */
void midi_clock_gen_stop(void);

/* Whether the clock is currently running. */
bool midi_clock_gen_is_running(void);

/* Drive the clock. If running and now_us has reached the next scheduled
 * emission point, emits one clock byte via the send callback, then
 * invokes the user callback, and re-anchors the next emission at
 * now_us + period_us.
 */
void midi_clock_gen_tick(uint64_t now_us);

#ifdef __cplusplus
}
#endif

#endif /* MIDI_CLOCK_GEN_DEFINED_H_ */
