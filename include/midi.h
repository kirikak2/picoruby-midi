/*
 * PicoRuby MIDI - Constants and Clock Timer API
 */

#ifndef MIDI_DEFINED_H_
#define MIDI_DEFINED_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * MIDI Clock Timer API (platform-specific)
 */

/* Initialize clock timer */
int MIDI_Clock_init(void);

/* Deinitialize clock timer */
void MIDI_Clock_deinit(void);

/* Set BPM and update timer period */
void MIDI_Clock_set_bpm(float bpm);

/* Start timer */
void MIDI_Clock_start(void);

/* Stop timer */
void MIDI_Clock_stop(void);

/* Check if timer is running */
bool MIDI_Clock_is_running(void);

/* Set callback for clock tick (called from ISR context) */
typedef void (*midi_clock_callback_t)(void);
void MIDI_Clock_set_callback(midi_clock_callback_t callback);

/*
 * Cooperative-stop hooks
 *
 * The clock timer and input task periodically consult the registered
 * stop-check function (if any). When it returns true, the cleanup hook
 * (if any) is invoked, then the clock and input task halt themselves.
 * Both hooks are application-supplied so picoruby-midi stays free of
 * application-layer references (e.g. picoruby-esp32's script-stop flag
 * or its all-notes-off cleanup routine).
 */
typedef bool (*midi_stop_check_fn)(void);
typedef void (*midi_cleanup_fn)(void);

/* Register the stop-requested predicate. NULL disables. */
void MIDI_set_stop_check(midi_stop_check_fn fn);

/* Register the cleanup hook fired right after stop_check returns true. */
void MIDI_set_cleanup_hook(midi_cleanup_fn fn);

/*
 * MIDI Input Background Task API
 */

/* MIDI event types */
typedef enum {
    MIDI_EVENT_NONE = 0,
    MIDI_EVENT_NOTE_ON,
    MIDI_EVENT_NOTE_OFF,
    MIDI_EVENT_CONTROL_CHANGE,
    MIDI_EVENT_PROGRAM_CHANGE,
    MIDI_EVENT_PITCH_BEND,
    MIDI_EVENT_POLY_AFTERTOUCH,
    MIDI_EVENT_CHANNEL_PRESSURE,
    MIDI_EVENT_CLOCK,
    MIDI_EVENT_START,
    MIDI_EVENT_STOP,
    MIDI_EVENT_CONTINUE,
    MIDI_EVENT_ACTIVE_SENSING,
    MIDI_EVENT_SYSTEM_RESET,
    MIDI_EVENT_SYSEX
} midi_event_type_t;

/* MIDI event source */
typedef enum {
    MIDI_SOURCE_NONE = 0,
    MIDI_SOURCE_USB = 1,
    MIDI_SOURCE_SAM2695 = 2
} midi_event_source_t;

/* Maximum SysEx message length (including F0/F7). Longer messages are truncated. */
#define MIDI_SYSEX_MAX_LEN 512

/* MIDI event structure */
typedef struct {
    midi_event_type_t type;
    midi_event_source_t source;  /* Source of the event (USB or SAM2695) */
    uint8_t channel;
    uint8_t data1;   /* note, cc number, program, pressure */
    uint8_t data2;   /* velocity, cc value */
    int16_t value;   /* pitch bend value (-8192 to 8191) */
    uint16_t sysex_len;      /* SysEx payload length (0 if not SysEx) */
    uint8_t *sysex_data;     /* malloc'd SysEx payload incl. F0/F7, NULL if not SysEx.
                                Consumer owns the buffer and must free() after use. */
} midi_event_t;

/* Initialize input task */
int MIDI_Input_init(void);

/* Deinitialize input task */
void MIDI_Input_deinit(void);

/* Start background processing task */
int MIDI_Input_start(void);

/* Stop background processing task */
void MIDI_Input_stop(void);

/* Check if input task is running */
bool MIDI_Input_is_running(void);

/* Check if input task was ever started (for auto-restart on reconnect) */
bool MIDI_Input_was_started(void);

/* Get number of events available (from USB queue) */
int MIDI_Input_events_available(void);

/* Get number of events available from specific source */
int MIDI_Input_events_available_usb(void);
int MIDI_Input_events_available_sam(void);

/* Pop event from queue (returns false if empty) - legacy, uses USB queue */
bool MIDI_Input_pop_event(midi_event_t *event);

/* Pop event from specific source queue */
bool MIDI_Input_pop_event_usb(midi_event_t *event);
bool MIDI_Input_pop_event_sam(midi_event_t *event);

/* Get external BPM calculated from incoming MIDI clock (USB-MIDI) */
float MIDI_Input_get_external_bpm_usb(void);

/* Get external BPM calculated from incoming MIDI clock (SAM2695/MIDI-DIN) */
float MIDI_Input_get_external_bpm_sam(void);

/* Get external BPM calculated from incoming MIDI clock (legacy - returns USB BPM) */
float MIDI_Input_get_external_bpm(void);

/* Reset external clock tracking for USB-MIDI */
void MIDI_Input_reset_external_clock_usb(void);

/* Reset external clock tracking for SAM2695 */
void MIDI_Input_reset_external_clock_sam(void);

/* Reset external clock tracking (legacy - resets both) */
void MIDI_Input_reset_external_clock(void);

/*
 * Note Scheduler API
 *
 * Allows triggering notes with automatic note-off after specified duration.
 * Designed for simultaneous multi-note triggering (e.g., multi-touch pads).
 */

/* Transport type bitmask */
#define MIDI_TRANSPORT_USB      0x01
#define MIDI_TRANSPORT_SAM2695  0x02
#define MIDI_TRANSPORT_ALL      0x03

/* Maximum number of scheduled notes */
#define MIDI_MAX_SCHEDULED_NOTES 32

/* Initialize note scheduler */
int MIDI_Note_scheduler_init(void);

/* Deinitialize note scheduler */
void MIDI_Note_scheduler_deinit(void);

/* Trigger a note (sends note_on immediately, schedules note_off after duration)
 *
 * @param transport_mask  Which transports to send to (MIDI_TRANSPORT_*)
 * @param channel         MIDI channel (0-15)
 * @param note            Note number (0-127)
 * @param velocity        Velocity (0-127)
 * @param duration_ms     Duration in milliseconds before note_off
 * @return 0 on success, -1 on error (scheduler full)
 */
int MIDI_Note_trigger(uint8_t transport_mask, uint8_t channel, uint8_t note,
                      uint8_t velocity, uint32_t duration_ms);

/* Cancel all scheduled notes and send all-notes-off */
void MIDI_Note_scheduler_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* MIDI_DEFINED_H_ */
