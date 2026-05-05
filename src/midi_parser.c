/*
 * PicoRuby MIDI - OS-free MIDI byte parser
 *
 * Pure C, no FreeRTOS / esp_log / esp_timer dependencies. The caller passes
 * `now_us` for external BPM tracking on incoming clock bytes.
 */

#include <stdlib.h>
#include <string.h>

#include "../include/midi_parser.h"

#define MIDI_PPQ 24

struct midi_parser {
    midi_event_source_t source;

    /* Running-status raw parser (used by feed_raw). */
    uint8_t last_status;
    uint8_t buffer[3];
    uint8_t buffer_pos;
    uint8_t expected_bytes;

    /* SysEx accumulator. The byte buffer is malloc'd lazily on F0 so the
     * struct itself stays small even for many parser instances. */
    bool sysex_active;
    bool sysex_truncated;
    uint16_t sysex_len;
    uint8_t *sysex_buf;

    /* External BPM tracker. Reader (BPM accessor) and writer (parser feed)
     * may run on different cores; volatile to keep the latest publish
     * visible without an explicit barrier. */
    int64_t clock_timestamps[MIDI_EXTERNAL_CLOCK_SAMPLES];
    volatile int clock_timestamp_index;
    volatile int clock_timestamp_count;
    volatile float external_bpm;
};

static void sysex_reset(midi_parser_t *p)
{
    if (p->sysex_buf) {
        free(p->sysex_buf);
        p->sysex_buf = NULL;
    }
    p->sysex_active = false;
    p->sysex_truncated = false;
    p->sysex_len = 0;
}

static bool sysex_begin(midi_parser_t *p)
{
    sysex_reset(p);
    p->sysex_buf = (uint8_t *)malloc(MIDI_SYSEX_MAX_LEN);
    if (!p->sysex_buf) {
        return false;
    }
    p->sysex_active = true;
    return true;
}

static void sysex_append(midi_parser_t *p, uint8_t b)
{
    if (!p->sysex_buf) return;
    if (p->sysex_len < MIDI_SYSEX_MAX_LEN) {
        p->sysex_buf[p->sysex_len++] = b;
    } else {
        p->sysex_truncated = true;
    }
}

/* Transfer the in-progress SysEx buffer to *out (no extra malloc/memcpy). */
static void finalize_sysex(midi_parser_t *p, midi_event_t *out)
{
    out->type = MIDI_EVENT_SYSEX;
    out->source = p->source;
    out->channel = 0;
    out->data1 = 0;
    out->data2 = 0;
    out->value = p->sysex_truncated ? 1 : 0;

    if (p->sysex_buf && p->sysex_len > 0) {
        out->sysex_data = p->sysex_buf;
        out->sysex_len = p->sysex_len;
        p->sysex_buf = NULL;
    } else {
        out->sysex_data = NULL;
        out->sysex_len = 0;
        if (p->sysex_buf) {
            free(p->sysex_buf);
            p->sysex_buf = NULL;
        }
    }

    p->sysex_active = false;
    p->sysex_truncated = false;
    p->sysex_len = 0;
}

static uint8_t expected_data_bytes(uint8_t status)
{
    switch (status & 0xF0) {
        case 0x80: case 0x90: case 0xA0: case 0xB0: case 0xE0: return 2;
        case 0xC0: case 0xD0: return 1;
        case 0xF0:
            switch (status) {
                case 0xF2: return 2;
                case 0xF1: case 0xF3: return 1;
                default: return 0;
            }
        default: return 0;
    }
}

static void track_external_clock(midi_parser_t *p, uint64_t now_us)
{
    int idx = p->clock_timestamp_index;
    p->clock_timestamps[idx] = (int64_t)now_us;
    p->clock_timestamp_index = (idx + 1) % MIDI_EXTERNAL_CLOCK_SAMPLES;
    if (p->clock_timestamp_count < MIDI_EXTERNAL_CLOCK_SAMPLES) {
        p->clock_timestamp_count++;
    }

    if (p->clock_timestamp_count >= MIDI_EXTERNAL_CLOCK_MIN_SAMPLES) {
        int oldest_idx, newest_idx;
        if (p->clock_timestamp_count < MIDI_EXTERNAL_CLOCK_SAMPLES) {
            oldest_idx = 0;
            newest_idx = p->clock_timestamp_count - 1;
        } else {
            oldest_idx = p->clock_timestamp_index;
            newest_idx =
                (p->clock_timestamp_index + MIDI_EXTERNAL_CLOCK_SAMPLES - 1)
                % MIDI_EXTERNAL_CLOCK_SAMPLES;
        }
        int64_t total =
            p->clock_timestamps[newest_idx] - p->clock_timestamps[oldest_idx];
        int n = p->clock_timestamp_count - 1;
        if (total > 0 && n > 0) {
            float avg = (float)total / (float)n;
            p->external_bpm = 60000000.0f / (avg * (float)MIDI_PPQ);
        }
    }
}

midi_parser_t *midi_parser_new(midi_event_source_t source)
{
    midi_parser_t *p = (midi_parser_t *)calloc(1, sizeof(midi_parser_t));
    if (!p) return NULL;
    p->source = source;
    return p;
}

void midi_parser_free(midi_parser_t *p)
{
    if (!p) return;
    if (p->sysex_buf) free(p->sysex_buf);
    free(p);
}

void midi_parser_reset(midi_parser_t *p)
{
    if (!p) return;
    p->last_status = 0;
    p->buffer_pos = 0;
    p->expected_bytes = 0;
    sysex_reset(p);
    midi_parser_reset_external_clock(p);
}

void midi_parser_reset_external_clock(midi_parser_t *p)
{
    if (!p) return;
    p->clock_timestamp_index = 0;
    p->clock_timestamp_count = 0;
    p->external_bpm = 0.0f;
}

float midi_parser_get_external_bpm(const midi_parser_t *p)
{
    return p ? p->external_bpm : 0.0f;
}

bool midi_parser_feed_raw(midi_parser_t *p, uint8_t byte, uint64_t now_us,
                          midi_event_t *out)
{
    if (!p || !out) return false;

    out->sysex_data = NULL;
    out->sysex_len = 0;

    /* Real-time messages (0xF8-0xFF) can interleave anywhere and must
     * never disturb the running parser or SysEx accumulator. */
    if (byte >= 0xF8) {
        out->source = p->source;
        out->channel = 0;
        out->data1 = 0;
        out->data2 = 0;
        out->value = 0;
        switch (byte) {
            case 0xF8:
                track_external_clock(p, now_us);
                return false;  /* clock not queued */
            case 0xFA: out->type = MIDI_EVENT_START;        return true;
            case 0xFC: out->type = MIDI_EVENT_STOP;         return true;
            case 0xFB: out->type = MIDI_EVENT_CONTINUE;     return true;
            case 0xFE: return false;  /* Active Sensing - drop */
            case 0xFF: out->type = MIDI_EVENT_SYSTEM_RESET; return true;
            default:   return false;
        }
    }

    /* SysEx in progress? */
    if (p->sysex_active) {
        if (byte == 0xF7) {
            sysex_append(p, byte);
            finalize_sysex(p, out);
            return true;
        }
        if (byte & 0x80) {
            /* Premature termination by another status byte: emit what
             * we have, then start a fresh message with this status. */
            finalize_sysex(p, out);
            p->last_status = byte;
            p->buffer[0] = byte;
            p->buffer_pos = 1;
            p->expected_bytes = expected_data_bytes(byte);
            return true;
        }
        sysex_append(p, byte);
        return false;
    }

    /* SysEx start. */
    if (byte == 0xF0) {
        if (sysex_begin(p)) sysex_append(p, 0xF0);
        p->last_status = 0;
        return false;
    }

    /* Non-real-time status byte. */
    if (byte & 0x80) {
        p->last_status = byte;
        p->buffer[0] = byte;
        p->buffer_pos = 1;
        p->expected_bytes = expected_data_bytes(byte);
        return false;
    }

    /* Data byte. */
    if (p->last_status == 0) return false;

    if (p->buffer_pos >= 3) {
        p->buffer_pos = 1;  /* defensive: keep status, drop excess */
    }
    p->buffer[p->buffer_pos++] = byte;

    if (p->buffer_pos - 1 < p->expected_bytes) {
        return false;  /* need more */
    }

    uint8_t status = p->buffer[0];
    uint8_t d1 = p->buffer_pos > 1 ? p->buffer[1] : 0;
    uint8_t d2 = p->buffer_pos > 2 ? p->buffer[2] : 0;

    out->source = p->source;
    out->channel = status & 0x0F;
    out->data1 = d1;
    out->data2 = d2;
    out->value = 0;

    /* Allow running status: keep status, reset to first data byte slot. */
    p->buffer_pos = 1;

    switch (status & 0xF0) {
        case 0x90:
            out->type = (d2 > 0) ? MIDI_EVENT_NOTE_ON : MIDI_EVENT_NOTE_OFF;
            return true;
        case 0x80:
            out->type = MIDI_EVENT_NOTE_OFF;
            return true;
        case 0xB0:
            out->type = MIDI_EVENT_CONTROL_CHANGE;
            return true;
        case 0xC0:
            out->type = MIDI_EVENT_PROGRAM_CHANGE;
            return true;
        case 0xE0:
            out->type = MIDI_EVENT_PITCH_BEND;
            out->value = ((d2 << 7) | d1) - 8192;
            return true;
        case 0xA0:
            out->type = MIDI_EVENT_POLY_AFTERTOUCH;
            return true;
        case 0xD0:
            out->type = MIDI_EVENT_CHANNEL_PRESSURE;
            return true;
        default:
            return false;
    }
}

bool midi_parser_feed_usb(midi_parser_t *p, uint8_t cin,
                          uint8_t b1, uint8_t b2, uint8_t b3,
                          uint64_t now_us, midi_event_t *out)
{
    if (!p || !out) return false;

    out->source = p->source;
    out->channel = b1 & 0x0F;
    out->data1 = b2;
    out->data2 = b3;
    out->value = 0;
    out->sysex_data = NULL;
    out->sysex_len = 0;

    switch (cin) {
        case 0x04:  /* SysEx start / continue (3 bytes) */
            if (b1 == 0xF0) sysex_begin(p);
            if (p->sysex_active) {
                sysex_append(p, b1);
                sysex_append(p, b2);
                sysex_append(p, b3);
            }
            return false;

        case 0x05:  /* SysEx end w/ 1 byte (or System Common 1-byte) */
            if (p->sysex_active) {
                sysex_append(p, b1);
                finalize_sysex(p, out);
                return true;
            }
            return false;

        case 0x06:  /* SysEx end w/ 2 bytes */
            if (!p->sysex_active) sysex_begin(p);
            sysex_append(p, b1);
            sysex_append(p, b2);
            finalize_sysex(p, out);
            return true;

        case 0x07:  /* SysEx end w/ 3 bytes */
            if (!p->sysex_active) sysex_begin(p);
            sysex_append(p, b1);
            sysex_append(p, b2);
            sysex_append(p, b3);
            finalize_sysex(p, out);
            return true;

        case 0x09:  /* Note On */
            out->type = (b3 > 0) ? MIDI_EVENT_NOTE_ON : MIDI_EVENT_NOTE_OFF;
            return true;
        case 0x08:  /* Note Off */
            out->type = MIDI_EVENT_NOTE_OFF;
            return true;
        case 0x0B:  /* Control Change */
            out->type = MIDI_EVENT_CONTROL_CHANGE;
            return true;
        case 0x0C:  /* Program Change */
            out->type = MIDI_EVENT_PROGRAM_CHANGE;
            return true;
        case 0x0E:  /* Pitch Bend */
            out->type = MIDI_EVENT_PITCH_BEND;
            out->value = ((b3 << 7) | b2) - 8192;
            return true;
        case 0x0A:  /* Poly Aftertouch */
            out->type = MIDI_EVENT_POLY_AFTERTOUCH;
            return true;
        case 0x0D:  /* Channel Pressure */
            out->type = MIDI_EVENT_CHANNEL_PRESSURE;
            return true;

        case 0x0F:  /* Single-byte Real-time / System */
            switch (b1) {
                case 0xF8:
                    track_external_clock(p, now_us);
                    return false;
                case 0xFA: out->type = MIDI_EVENT_START;        return true;
                case 0xFC: out->type = MIDI_EVENT_STOP;         return true;
                case 0xFB: out->type = MIDI_EVENT_CONTINUE;     return true;
                case 0xFE: return false;  /* Active Sensing - drop */
                case 0xFF: out->type = MIDI_EVENT_SYSTEM_RESET; return true;
            }
            break;
    }

    out->type = MIDI_EVENT_NONE;
    return false;
}
