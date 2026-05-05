/*
 * PicoRuby MIDI - OS-free MIDI Clock generator core
 *
 * Holds BPM/period and the next-emission anchor, and emits a clock byte
 * via a registered send callback whenever the caller's tick(now_us) has
 * advanced past the anchor. No esp_timer / FreeRTOS dependency.
 */

#include <stddef.h>

#include "../include/midi_clock_gen.h"

#define MIDI_PPQ           24
#define MIDI_BPM_MIN       20.0f
#define MIDI_BPM_MAX      300.0f
#define MIDI_BPM_DEFAULT  120.0f

static struct {
    bool running;
    float bpm;
    uint64_t period_us;
    uint64_t next_emit_us;
} g_clock = {
    .running = false,
    .bpm = MIDI_BPM_DEFAULT,
    .period_us = 20833,   /* 60_000_000 / 120 / 24 */
    .next_emit_us = 0,
};

static midi_clock_gen_send_fn g_send_cb = NULL;
static midi_clock_callback_t  g_user_cb = NULL;

static uint64_t bpm_to_period_us(float bpm)
{
    if (bpm < MIDI_BPM_MIN) bpm = MIDI_BPM_MIN;
    if (bpm > MIDI_BPM_MAX) bpm = MIDI_BPM_MAX;
    return (uint64_t)(60000000.0f / bpm / (float)MIDI_PPQ);
}

void midi_clock_gen_init(void)
{
    g_clock.running = false;
    g_clock.bpm = MIDI_BPM_DEFAULT;
    g_clock.period_us = bpm_to_period_us(MIDI_BPM_DEFAULT);
    g_clock.next_emit_us = 0;
}

void midi_clock_gen_deinit(void)
{
    g_clock.running = false;
    g_clock.next_emit_us = 0;
}

void midi_clock_gen_set_bpm(float bpm)
{
    if (bpm < MIDI_BPM_MIN) bpm = MIDI_BPM_MIN;
    if (bpm > MIDI_BPM_MAX) bpm = MIDI_BPM_MAX;
    g_clock.bpm = bpm;
    g_clock.period_us = bpm_to_period_us(bpm);
}

float midi_clock_gen_get_bpm(void)
{
    return g_clock.bpm;
}

uint64_t midi_clock_gen_get_period_us(void)
{
    return g_clock.period_us;
}

void midi_clock_gen_set_send_callback(midi_clock_gen_send_fn cb)
{
    g_send_cb = cb;
}

void midi_clock_gen_set_user_callback(midi_clock_callback_t cb)
{
    g_user_cb = cb;
}

void midi_clock_gen_start(uint64_t now_us)
{
    g_clock.running = true;
    /* Emit immediately on the first tick after start. */
    g_clock.next_emit_us = now_us;
}

void midi_clock_gen_stop(void)
{
    g_clock.running = false;
}

bool midi_clock_gen_is_running(void)
{
    return g_clock.running;
}

void midi_clock_gen_tick(uint64_t now_us)
{
    if (!g_clock.running) return;
    if (now_us < g_clock.next_emit_us) return;

    if (g_send_cb) g_send_cb();
    if (g_user_cb) g_user_cb();

    /* Re-anchor relative to now to bound drift if the caller is jittery
     * or skipped ticks; precise periodic timers (e.g. esp_timer) still
     * land within a microsecond of period_us each call. */
    g_clock.next_emit_us = now_us + g_clock.period_us;
}
