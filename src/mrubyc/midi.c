/*
 * PicoRuby MIDI - mrubyc bindings for Clock timer
 */

#include <mrubyc.h>
#include <stdlib.h>

#include "../../include/midi.h"

/*
 * MIDI::Clock._init_timer
 */
static void
c_midi_clock_init_timer(mrbc_vm *vm, mrbc_value v[], int argc)
{
    int ret = MIDI_Clock_init();
    SET_INT_RETURN(ret);
}

/*
 * MIDI::Clock._start_timer
 */
static void
c_midi_clock_start_timer(mrbc_vm *vm, mrbc_value v[], int argc)
{
    MIDI_Clock_start();
    SET_NIL_RETURN();
}

/*
 * MIDI::Clock._stop_timer
 */
static void
c_midi_clock_stop_timer(mrbc_vm *vm, mrbc_value v[], int argc)
{
    MIDI_Clock_stop();
    SET_NIL_RETURN();
}

/*
 * MIDI::Clock._update_timer_period
 */
static void
c_midi_clock_update_period(mrbc_vm *vm, mrbc_value v[], int argc)
{
    mrbc_value self = v[0];
    mrbc_value bpm_val = mrbc_instance_getiv(&self, mrbc_str_to_symid("bpm"));

    if (bpm_val.tt == MRBC_TT_FLOAT) {
        MIDI_Clock_set_bpm((float)bpm_val.d);
    } else if (bpm_val.tt == MRBC_TT_INTEGER) {
        MIDI_Clock_set_bpm((float)bpm_val.i);
    }

    SET_NIL_RETURN();
}

/*
 * MIDI::Clock._timer_running?
 */
static void
c_midi_clock_timer_running(mrbc_vm *vm, mrbc_value v[], int argc)
{
    SET_BOOL_RETURN(MIDI_Clock_is_running());
}

/*
 * MIDI::Input._start_task
 */
static void
c_midi_input_start_task(mrbc_vm *vm, mrbc_value v[], int argc)
{
    int ret = MIDI_Input_start();
    SET_INT_RETURN(ret);
}

/*
 * MIDI::Input._stop_task
 */
static void
c_midi_input_stop_task(mrbc_vm *vm, mrbc_value v[], int argc)
{
    MIDI_Input_stop();
    SET_NIL_RETURN();
}

/*
 * MIDI::Input._task_running?
 */
static void
c_midi_input_task_running(mrbc_vm *vm, mrbc_value v[], int argc)
{
    SET_BOOL_RETURN(MIDI_Input_is_running());
}

/*
 * MIDI::Input._events_available
 */
static void
c_midi_input_events_available(mrbc_vm *vm, mrbc_value v[], int argc)
{
    SET_INT_RETURN(MIDI_Input_events_available());
}

/*
 * MIDI::Input._events_available_usb
 */
static void
c_midi_input_events_available_usb(mrbc_vm *vm, mrbc_value v[], int argc)
{
    SET_INT_RETURN(MIDI_Input_events_available_usb());
}

/*
 * MIDI::Input._events_available_sam
 */
static void
c_midi_input_events_available_sam(mrbc_vm *vm, mrbc_value v[], int argc)
{
    SET_INT_RETURN(MIDI_Input_events_available_sam());
}

/*
 * MIDI::Input._external_bpm
 * Returns: Float BPM calculated from external MIDI clock (USB-MIDI), or 0.0 if not available
 */
static void
c_midi_input_external_bpm(mrbc_vm *vm, mrbc_value v[], int argc)
{
    float bpm = MIDI_Input_get_external_bpm();
    SET_FLOAT_RETURN(bpm);
}

/*
 * MIDI::Input._external_bpm_usb
 * Returns: Float BPM calculated from USB-MIDI clock, or 0.0 if not available
 */
static void
c_midi_input_external_bpm_usb(mrbc_vm *vm, mrbc_value v[], int argc)
{
    float bpm = MIDI_Input_get_external_bpm_usb();
    SET_FLOAT_RETURN(bpm);
}

/*
 * MIDI::Input._external_bpm_sam
 * Returns: Float BPM calculated from SAM2695 (MIDI-DIN) clock, or 0.0 if not available
 */
static void
c_midi_input_external_bpm_sam(mrbc_vm *vm, mrbc_value v[], int argc)
{
    float bpm = MIDI_Input_get_external_bpm_sam();
    SET_FLOAT_RETURN(bpm);
}

/*
 * MIDI::Input._reset_external_clock
 * Reset external clock tracking (call on MIDI Start)
 */
static void
c_midi_input_reset_external_clock(mrbc_vm *vm, mrbc_value v[], int argc)
{
    MIDI_Input_reset_external_clock();
    SET_NIL_RETURN();
}

/*
 * MIDI::Input._reset_external_clock_usb
 * Reset USB-MIDI external clock tracking
 */
static void
c_midi_input_reset_external_clock_usb(mrbc_vm *vm, mrbc_value v[], int argc)
{
    MIDI_Input_reset_external_clock_usb();
    SET_NIL_RETURN();
}

/*
 * MIDI::Input._reset_external_clock_sam
 * Reset SAM2695 (MIDI-DIN) external clock tracking
 */
static void
c_midi_input_reset_external_clock_sam(mrbc_vm *vm, mrbc_value v[], int argc)
{
    MIDI_Input_reset_external_clock_sam();
    SET_NIL_RETURN();
}

/*
 * MIDI._trigger(transport_mask, channel, note, velocity, duration_ms)
 *
 * Trigger a note with automatic note_off after duration.
 * Sends note_on immediately and schedules note_off.
 *
 * @param transport_mask [Integer] MIDI_TRANSPORT_USB(1), MIDI_TRANSPORT_SAM2695(2), or both(3)
 * @param channel [Integer] MIDI channel (0-15)
 * @param note [Integer] Note number (0-127)
 * @param velocity [Integer] Velocity (0-127)
 * @param duration_ms [Integer] Duration in milliseconds
 * @return [Integer] 0 on success, -1 on error
 */
static void
c_midi_trigger(mrbc_vm *vm, mrbc_value v[], int argc)
{
    if (argc < 5) {
        SET_INT_RETURN(-1);
        return;
    }

    uint8_t transport_mask = (uint8_t)GET_INT_ARG(1);
    uint8_t channel = (uint8_t)GET_INT_ARG(2);
    uint8_t note = (uint8_t)GET_INT_ARG(3);
    uint8_t velocity = (uint8_t)GET_INT_ARG(4);
    uint32_t duration_ms = (uint32_t)GET_INT_ARG(5);

    int ret = MIDI_Note_trigger(transport_mask, channel, note, velocity, duration_ms);
    SET_INT_RETURN(ret);
}

/*
 * MIDI._scheduler_clear
 *
 * Clear all scheduled notes and send note_off for each.
 */
static void
c_midi_scheduler_clear(mrbc_vm *vm, mrbc_value v[], int argc)
{
    MIDI_Note_scheduler_clear();
    SET_NIL_RETURN();
}

/*
 * MIDI._send_batch(events)
 *
 * Send multiple note_on events with minimal latency.
 * Events are sent sequentially with no Ruby overhead.
 * Note: Only note_on events are processed. note_off should be sent separately.
 *
 * @param events [Array] Array of hashes with keys:
 *   - :type => :note_on (Symbol)
 *   - :transport => 1 (USB), 2 (SAM2695), or 3 (both) (Integer)
 *   - :channel => 0-15 (Integer)
 *   - :note => 0-127 (Integer)
 *   - :velocity => 0-127 (Integer)
 * @return [Integer] Number of events sent
 */
static void
c_midi_send_batch(mrbc_vm *vm, mrbc_value v[], int argc)
{
    if (argc < 1 || v[1].tt != MRBC_TT_ARRAY) {
        SET_INT_RETURN(0);
        return;
    }

    mrbc_value events_array = v[1];
    int num_events = mrbc_array_size(&events_array);
    int sent_count = 0;

    for (int i = 0; i < num_events; i++) {
        mrbc_value event = mrbc_array_get(&events_array, i);

        if (event.tt != MRBC_TT_HASH) {
            continue;
        }

        /* Extract event fields from hash */
        mrbc_value type_val = mrbc_hash_get(&event, &mrbc_symbol_value(mrbc_str_to_symid("type")));
        mrbc_value transport_val = mrbc_hash_get(&event, &mrbc_symbol_value(mrbc_str_to_symid("transport")));
        mrbc_value channel_val = mrbc_hash_get(&event, &mrbc_symbol_value(mrbc_str_to_symid("channel")));
        mrbc_value note_val = mrbc_hash_get(&event, &mrbc_symbol_value(mrbc_str_to_symid("note")));
        mrbc_value velocity_val = mrbc_hash_get(&event, &mrbc_symbol_value(mrbc_str_to_symid("velocity")));
        mrbc_value duration_val = mrbc_hash_get(&event, &mrbc_symbol_value(mrbc_str_to_symid("duration_ms")));

        if (type_val.tt != MRBC_TT_SYMBOL || transport_val.tt != MRBC_TT_INTEGER ||
            channel_val.tt != MRBC_TT_INTEGER || note_val.tt != MRBC_TT_INTEGER) {
            continue;
        }

        uint8_t transport = (uint8_t)transport_val.i;
        uint8_t channel = (uint8_t)channel_val.i;
        uint8_t note = (uint8_t)note_val.i;

        /* Get type symbol string */
        const char *type_str = mrbc_symid_to_str(type_val.i);

        /* Only process note_on in batch */
        if (strcmp(type_str, "note_on") == 0) {
            uint8_t velocity = (velocity_val.tt == MRBC_TT_INTEGER) ? (uint8_t)velocity_val.i : 100;
            uint32_t duration_ms = (duration_val.tt == MRBC_TT_INTEGER) ? (uint32_t)duration_val.i : 1000;

            /* Log the event */
            // console_printf("[BATCH] ch=%d note=%d vel=%d dur=%ums\n",
            //               channel, note, velocity, duration_ms);

            /* Send note_on with automatic note_off after duration_ms */
            MIDI_Note_trigger(transport, channel, note, velocity, duration_ms);
            sent_count++;
        }
    }

    SET_INT_RETURN(sent_count);
}

/*
 * Helper function to convert midi_event_t to mruby hash
 */
static mrbc_value midi_event_to_hash(mrbc_vm *vm, midi_event_t *event)
{
    /* Convert event type to symbol */
    const char *type_str;
    switch (event->type) {
        case MIDI_EVENT_NOTE_ON:          type_str = "note_on"; break;
        case MIDI_EVENT_NOTE_OFF:         type_str = "note_off"; break;
        case MIDI_EVENT_CONTROL_CHANGE:   type_str = "control_change"; break;
        case MIDI_EVENT_PROGRAM_CHANGE:   type_str = "program_change"; break;
        case MIDI_EVENT_PITCH_BEND:       type_str = "pitch_bend"; break;
        case MIDI_EVENT_POLY_AFTERTOUCH:  type_str = "poly_aftertouch"; break;
        case MIDI_EVENT_CHANNEL_PRESSURE: type_str = "channel_pressure"; break;
        case MIDI_EVENT_CLOCK:            type_str = "clock"; break;
        case MIDI_EVENT_START:            type_str = "start"; break;
        case MIDI_EVENT_STOP:             type_str = "stop"; break;
        case MIDI_EVENT_CONTINUE:         type_str = "continue"; break;
        case MIDI_EVENT_ACTIVE_SENSING:   type_str = "active_sensing"; break;
        case MIDI_EVENT_SYSTEM_RESET:     type_str = "system_reset"; break;
        case MIDI_EVENT_SYSEX:            type_str = "sysex"; break;
        default:                          type_str = "unknown"; break;
    }

    mrbc_value hash = mrbc_hash_new(vm, 6);

    /* :type */
    mrbc_value key_type = mrbc_symbol_value(mrbc_str_to_symid("type"));
    mrbc_value val_type = mrbc_symbol_value(mrbc_str_to_symid(type_str));
    mrbc_hash_set(&hash, &key_type, &val_type);

    /* :source */
    const char *source_str;
    switch (event->source) {
        case MIDI_SOURCE_USB:     source_str = "usb"; break;
        case MIDI_SOURCE_SAM2695: source_str = "sam2695"; break;
        default:                  source_str = "unknown"; break;
    }
    mrbc_value key_source = mrbc_symbol_value(mrbc_str_to_symid("source"));
    mrbc_value val_source = mrbc_symbol_value(mrbc_str_to_symid(source_str));
    mrbc_hash_set(&hash, &key_source, &val_source);

    /* :channel */
    mrbc_value key_channel = mrbc_symbol_value(mrbc_str_to_symid("channel"));
    mrbc_value val_channel = mrbc_integer_value(event->channel);
    mrbc_hash_set(&hash, &key_channel, &val_channel);

    /* Event-specific data */
    switch (event->type) {
        case MIDI_EVENT_NOTE_ON:
        case MIDI_EVENT_NOTE_OFF:
        {
            mrbc_value key_note = mrbc_symbol_value(mrbc_str_to_symid("note"));
            mrbc_value val_note = mrbc_integer_value(event->data1);
            mrbc_hash_set(&hash, &key_note, &val_note);

            mrbc_value key_vel = mrbc_symbol_value(mrbc_str_to_symid("velocity"));
            mrbc_value val_vel = mrbc_integer_value(event->data2);
            mrbc_hash_set(&hash, &key_vel, &val_vel);
            break;
        }
        case MIDI_EVENT_CONTROL_CHANGE:
        {
            mrbc_value key_cc = mrbc_symbol_value(mrbc_str_to_symid("cc"));
            mrbc_value val_cc = mrbc_integer_value(event->data1);
            mrbc_hash_set(&hash, &key_cc, &val_cc);

            mrbc_value key_val = mrbc_symbol_value(mrbc_str_to_symid("value"));
            mrbc_value val_val = mrbc_integer_value(event->data2);
            mrbc_hash_set(&hash, &key_val, &val_val);
            break;
        }
        case MIDI_EVENT_PROGRAM_CHANGE:
        {
            mrbc_value key_prog = mrbc_symbol_value(mrbc_str_to_symid("program"));
            mrbc_value val_prog = mrbc_integer_value(event->data1);
            mrbc_hash_set(&hash, &key_prog, &val_prog);
            break;
        }
        case MIDI_EVENT_PITCH_BEND:
        {
            mrbc_value key_val = mrbc_symbol_value(mrbc_str_to_symid("value"));
            mrbc_value val_val = mrbc_integer_value(event->value);
            mrbc_hash_set(&hash, &key_val, &val_val);
            break;
        }
        case MIDI_EVENT_POLY_AFTERTOUCH:
        {
            mrbc_value key_note = mrbc_symbol_value(mrbc_str_to_symid("note"));
            mrbc_value val_note = mrbc_integer_value(event->data1);
            mrbc_hash_set(&hash, &key_note, &val_note);

            mrbc_value key_pressure = mrbc_symbol_value(mrbc_str_to_symid("pressure"));
            mrbc_value val_pressure = mrbc_integer_value(event->data2);
            mrbc_hash_set(&hash, &key_pressure, &val_pressure);
            break;
        }
        case MIDI_EVENT_CHANNEL_PRESSURE:
        {
            mrbc_value key_pressure = mrbc_symbol_value(mrbc_str_to_symid("pressure"));
            mrbc_value val_pressure = mrbc_integer_value(event->data1);
            mrbc_hash_set(&hash, &key_pressure, &val_pressure);
            break;
        }
        case MIDI_EVENT_SYSEX:
        {
            /* Build :data as Array<Integer> from malloc'd buffer, then free it */
            uint16_t len = event->sysex_len;
            mrbc_value arr = mrbc_array_new(vm, len);
            if (event->sysex_data) {
                for (uint16_t i = 0; i < len; i++) {
                    mrbc_value byte_val = mrbc_integer_value(event->sysex_data[i]);
                    mrbc_array_set(&arr, i, &byte_val);
                }
                free(event->sysex_data);
                event->sysex_data = NULL;
            }
            mrbc_value key_data = mrbc_symbol_value(mrbc_str_to_symid("data"));
            mrbc_hash_set(&hash, &key_data, &arr);

            /* :truncated flag (true if message exceeded MIDI_SYSEX_MAX_LEN) */
            if (event->value) {
                mrbc_value key_trunc = mrbc_symbol_value(mrbc_str_to_symid("truncated"));
                mrbc_value val_trunc = mrbc_true_value();
                mrbc_hash_set(&hash, &key_trunc, &val_trunc);
            }
            break;
        }
        default:
            break;
    }

    return hash;
}

/*
 * MIDI::Input._pop_event
 * Returns: Hash with event data or nil
 */
static void
c_midi_input_pop_event(mrbc_vm *vm, mrbc_value v[], int argc)
{
    midi_event_t event;
    if (!MIDI_Input_pop_event(&event)) {
        SET_NIL_RETURN();
        return;
    }

    mrbc_value hash = midi_event_to_hash(vm, &event);
    SET_RETURN(hash);
}

/*
 * MIDI::Input._pop_event_usb
 * Returns: Hash with event data or nil (from USB queue)
 */
static void
c_midi_input_pop_event_usb(mrbc_vm *vm, mrbc_value v[], int argc)
{
    midi_event_t event;
    if (!MIDI_Input_pop_event_usb(&event)) {
        SET_NIL_RETURN();
        return;
    }

    mrbc_value hash = midi_event_to_hash(vm, &event);
    SET_RETURN(hash);
}

/*
 * MIDI::Input._pop_event_sam
 * Returns: Hash with event data or nil (from SAM2695 queue)
 */
static void
c_midi_input_pop_event_sam(mrbc_vm *vm, mrbc_value v[], int argc)
{
    midi_event_t event;
    if (!MIDI_Input_pop_event_sam(&event)) {
        SET_NIL_RETURN();
        return;
    }

    mrbc_value hash = midi_event_to_hash(vm, &event);
    SET_RETURN(hash);
}

/*
 * Gem initialization
 */
void
mrbc_midi_init(mrbc_vm *vm)
{
    /* Define MIDI module */
    mrbc_class *module_MIDI = mrbc_define_module(vm, "MIDI");

    /* Module-level methods for note scheduler */
    mrbc_define_method(vm, module_MIDI, "_trigger", c_midi_trigger);
    mrbc_define_method(vm, module_MIDI, "_scheduler_clear", c_midi_scheduler_clear);
    mrbc_define_method(vm, module_MIDI, "_send_batch", c_midi_send_batch);

    /* Define Clock class under MIDI */
    mrbc_class *class_Clock = mrbc_define_class_under(vm, module_MIDI, "Clock", mrbc_class_object);

    /* Clock timer methods */
    mrbc_define_method(vm, class_Clock, "_init_timer", c_midi_clock_init_timer);
    mrbc_define_method(vm, class_Clock, "_start_timer", c_midi_clock_start_timer);
    mrbc_define_method(vm, class_Clock, "_stop_timer", c_midi_clock_stop_timer);
    mrbc_define_method(vm, class_Clock, "_update_timer_period", c_midi_clock_update_period);
    mrbc_define_method(vm, class_Clock, "_timer_running?", c_midi_clock_timer_running);

    /* Define Input class under MIDI */
    mrbc_class *class_Input = mrbc_define_class_under(vm, module_MIDI, "Input", mrbc_class_object);

    /* Input task methods */
    mrbc_define_method(vm, class_Input, "_start_task", c_midi_input_start_task);
    mrbc_define_method(vm, class_Input, "_stop_task", c_midi_input_stop_task);
    mrbc_define_method(vm, class_Input, "_task_running?", c_midi_input_task_running);
    mrbc_define_method(vm, class_Input, "_events_available", c_midi_input_events_available);
    mrbc_define_method(vm, class_Input, "_events_available_usb", c_midi_input_events_available_usb);
    mrbc_define_method(vm, class_Input, "_events_available_sam", c_midi_input_events_available_sam);
    mrbc_define_method(vm, class_Input, "_pop_event", c_midi_input_pop_event);
    mrbc_define_method(vm, class_Input, "_pop_event_usb", c_midi_input_pop_event_usb);
    mrbc_define_method(vm, class_Input, "_pop_event_sam", c_midi_input_pop_event_sam);
    mrbc_define_method(vm, class_Input, "_external_bpm", c_midi_input_external_bpm);
    mrbc_define_method(vm, class_Input, "_external_bpm_usb", c_midi_input_external_bpm_usb);
    mrbc_define_method(vm, class_Input, "_external_bpm_sam", c_midi_input_external_bpm_sam);
    mrbc_define_method(vm, class_Input, "_reset_external_clock", c_midi_input_reset_external_clock);
    mrbc_define_method(vm, class_Input, "_reset_external_clock_usb", c_midi_input_reset_external_clock_usb);
    mrbc_define_method(vm, class_Input, "_reset_external_clock_sam", c_midi_input_reset_external_clock_sam);
}
