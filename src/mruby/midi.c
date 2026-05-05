/*
 * PicoRuby MIDI - mruby bindings
 *
 * Feature parity with the mrubyc binding in src/mrubyc/midi.c. Methods
 * defined here are wired up only when the gem is built against the
 * mruby VM (PICORB_VM_MRUBY); on mrubyc builds the mrubyc/ binding is
 * used instead.
 */

#include <stdlib.h>
#include <string.h>

#include <mruby.h>
#include <mruby/presym.h>
#include <mruby/class.h>
#include <mruby/variable.h>
#include <mruby/hash.h>
#include <mruby/array.h>
#include <mruby/string.h>

#include "../../include/midi.h"

/* ========================================================================
 * MIDI::Clock
 * ======================================================================== */

static mrb_value
mrb_midi_clock_init_timer(mrb_state *mrb, mrb_value self)
{
    int ret = MIDI_Clock_init();
    return mrb_fixnum_value(ret);
}

static mrb_value
mrb_midi_clock_start_timer(mrb_state *mrb, mrb_value self)
{
    MIDI_Clock_start();
    return mrb_nil_value();
}

static mrb_value
mrb_midi_clock_stop_timer(mrb_state *mrb, mrb_value self)
{
    MIDI_Clock_stop();
    return mrb_nil_value();
}

static mrb_value
mrb_midi_clock_update_period(mrb_state *mrb, mrb_value self)
{
    mrb_value bpm_val = mrb_iv_get(mrb, self, MRB_IVSYM(bpm));
    if (mrb_float_p(bpm_val)) {
        MIDI_Clock_set_bpm((float)mrb_float(bpm_val));
    } else if (mrb_integer_p(bpm_val)) {
        MIDI_Clock_set_bpm((float)mrb_integer(bpm_val));
    }
    return mrb_nil_value();
}

static mrb_value
mrb_midi_clock_timer_running(mrb_state *mrb, mrb_value self)
{
    return mrb_bool_value(MIDI_Clock_is_running());
}

/* ========================================================================
 * MIDI::Input - task control + queue accessors
 * ======================================================================== */

static mrb_value
mrb_midi_input_start_task(mrb_state *mrb, mrb_value self)
{
    int ret = MIDI_Input_start();
    return mrb_fixnum_value(ret);
}

static mrb_value
mrb_midi_input_stop_task(mrb_state *mrb, mrb_value self)
{
    MIDI_Input_stop();
    return mrb_nil_value();
}

static mrb_value
mrb_midi_input_task_running(mrb_state *mrb, mrb_value self)
{
    return mrb_bool_value(MIDI_Input_is_running());
}

static mrb_value
mrb_midi_input_events_available(mrb_state *mrb, mrb_value self)
{
    return mrb_fixnum_value(MIDI_Input_events_available());
}

static mrb_value
mrb_midi_input_events_available_usb(mrb_state *mrb, mrb_value self)
{
    return mrb_fixnum_value(MIDI_Input_events_available_usb());
}

static mrb_value
mrb_midi_input_events_available_sam(mrb_state *mrb, mrb_value self)
{
    return mrb_fixnum_value(MIDI_Input_events_available_sam());
}

/* ========================================================================
 * MIDI::Input - external clock BPM tracking
 * ======================================================================== */

static mrb_value
mrb_midi_input_external_bpm(mrb_state *mrb, mrb_value self)
{
    return mrb_float_value(mrb, (mrb_float)MIDI_Input_get_external_bpm());
}

static mrb_value
mrb_midi_input_external_bpm_usb(mrb_state *mrb, mrb_value self)
{
    return mrb_float_value(mrb, (mrb_float)MIDI_Input_get_external_bpm_usb());
}

static mrb_value
mrb_midi_input_external_bpm_sam(mrb_state *mrb, mrb_value self)
{
    return mrb_float_value(mrb, (mrb_float)MIDI_Input_get_external_bpm_sam());
}

static mrb_value
mrb_midi_input_reset_external_clock(mrb_state *mrb, mrb_value self)
{
    MIDI_Input_reset_external_clock();
    return mrb_nil_value();
}

static mrb_value
mrb_midi_input_reset_external_clock_usb(mrb_state *mrb, mrb_value self)
{
    MIDI_Input_reset_external_clock_usb();
    return mrb_nil_value();
}

static mrb_value
mrb_midi_input_reset_external_clock_sam(mrb_state *mrb, mrb_value self)
{
    MIDI_Input_reset_external_clock_sam();
    return mrb_nil_value();
}

/* ========================================================================
 * MIDI module-level: note scheduler / batch trigger
 * ======================================================================== */

/*
 * MIDI._trigger(transport_mask, channel, note, velocity, duration_ms)
 * Sends note_on immediately and schedules note_off after duration_ms.
 * Returns 0 on success, -1 on error.
 */
static mrb_value
mrb_midi_trigger(mrb_state *mrb, mrb_value self)
{
    mrb_int transport_mask, channel, note, velocity, duration_ms;
    mrb_get_args(mrb, "iiiii",
                 &transport_mask, &channel, &note, &velocity, &duration_ms);

    int ret = MIDI_Note_trigger((uint8_t)transport_mask,
                                (uint8_t)channel,
                                (uint8_t)note,
                                (uint8_t)velocity,
                                (uint32_t)duration_ms);
    return mrb_fixnum_value(ret);
}

static mrb_value
mrb_midi_scheduler_clear(mrb_state *mrb, mrb_value self)
{
    MIDI_Note_scheduler_clear();
    return mrb_nil_value();
}

/*
 * MIDI._send_batch(events)
 * Trigger many note_ons in a tight C loop. Each entry must be a Hash with
 * at least :type => :note_on, :transport, :channel, :note. :velocity and
 * :duration_ms are optional (defaults 100 / 1000).
 */
static mrb_value
mrb_midi_send_batch(mrb_state *mrb, mrb_value self)
{
    mrb_value events;
    mrb_get_args(mrb, "A", &events);

    if (!mrb_array_p(events)) {
        return mrb_fixnum_value(0);
    }

    mrb_int num_events = RARRAY_LEN(events);
    mrb_int sent = 0;

    mrb_sym sym_type        = mrb_intern_lit(mrb, "type");
    mrb_sym sym_transport   = mrb_intern_lit(mrb, "transport");
    mrb_sym sym_channel     = mrb_intern_lit(mrb, "channel");
    mrb_sym sym_note        = mrb_intern_lit(mrb, "note");
    mrb_sym sym_velocity    = mrb_intern_lit(mrb, "velocity");
    mrb_sym sym_duration_ms = mrb_intern_lit(mrb, "duration_ms");
    mrb_sym sym_note_on     = mrb_intern_lit(mrb, "note_on");

    for (mrb_int i = 0; i < num_events; i++) {
        mrb_value entry = mrb_ary_ref(mrb, events, i);
        if (!mrb_hash_p(entry)) continue;

        mrb_value type     = mrb_hash_get(mrb, entry, mrb_symbol_value(sym_type));
        mrb_value tx       = mrb_hash_get(mrb, entry, mrb_symbol_value(sym_transport));
        mrb_value channel  = mrb_hash_get(mrb, entry, mrb_symbol_value(sym_channel));
        mrb_value note     = mrb_hash_get(mrb, entry, mrb_symbol_value(sym_note));
        mrb_value velocity = mrb_hash_get(mrb, entry, mrb_symbol_value(sym_velocity));
        mrb_value duration = mrb_hash_get(mrb, entry, mrb_symbol_value(sym_duration_ms));

        if (!mrb_symbol_p(type) || mrb_symbol(type) != sym_note_on) continue;
        if (!mrb_integer_p(tx) || !mrb_integer_p(channel) || !mrb_integer_p(note)) continue;

        uint8_t  vel_u  = mrb_integer_p(velocity) ? (uint8_t)mrb_integer(velocity) : 100;
        uint32_t dur_u  = mrb_integer_p(duration) ? (uint32_t)mrb_integer(duration) : 1000;

        MIDI_Note_trigger((uint8_t)mrb_integer(tx),
                          (uint8_t)mrb_integer(channel),
                          (uint8_t)mrb_integer(note),
                          vel_u, dur_u);
        sent++;
    }

    return mrb_fixnum_value(sent);
}

/* ========================================================================
 * MIDI::Input - event pop helpers
 * ======================================================================== */

/* Convert a midi_event_t into the Hash shape Ruby code expects. Mirrors
 * mrubyc/midi.c's midi_event_to_hash; consumer of event->sysex_data
 * frees it. */
static mrb_value
midi_event_to_hash(mrb_state *mrb, midi_event_t *event)
{
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

    const char *source_str;
    switch (event->source) {
        case MIDI_SOURCE_USB:     source_str = "usb"; break;
        case MIDI_SOURCE_SAM2695: source_str = "sam2695"; break;
        default:                  source_str = "unknown"; break;
    }

    mrb_value hash = mrb_hash_new_capa(mrb, 6);
    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_lit(mrb, "type")),
                 mrb_symbol_value(mrb_intern_cstr(mrb, type_str)));
    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_lit(mrb, "source")),
                 mrb_symbol_value(mrb_intern_cstr(mrb, source_str)));
    mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_lit(mrb, "channel")),
                 mrb_fixnum_value(event->channel));

    switch (event->type) {
        case MIDI_EVENT_NOTE_ON:
        case MIDI_EVENT_NOTE_OFF:
            mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_lit(mrb, "note")),
                         mrb_fixnum_value(event->data1));
            mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_lit(mrb, "velocity")),
                         mrb_fixnum_value(event->data2));
            break;
        case MIDI_EVENT_CONTROL_CHANGE:
            mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_lit(mrb, "cc")),
                         mrb_fixnum_value(event->data1));
            mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_lit(mrb, "value")),
                         mrb_fixnum_value(event->data2));
            break;
        case MIDI_EVENT_PROGRAM_CHANGE:
            mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_lit(mrb, "program")),
                         mrb_fixnum_value(event->data1));
            break;
        case MIDI_EVENT_PITCH_BEND:
            mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_lit(mrb, "value")),
                         mrb_fixnum_value(event->value));
            break;
        case MIDI_EVENT_POLY_AFTERTOUCH:
            mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_lit(mrb, "note")),
                         mrb_fixnum_value(event->data1));
            mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_lit(mrb, "pressure")),
                         mrb_fixnum_value(event->data2));
            break;
        case MIDI_EVENT_CHANNEL_PRESSURE:
            mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_lit(mrb, "pressure")),
                         mrb_fixnum_value(event->data1));
            break;
        case MIDI_EVENT_SYSEX: {
            mrb_int len = (mrb_int)event->sysex_len;
            mrb_value arr = mrb_ary_new_capa(mrb, len);
            if (event->sysex_data) {
                for (mrb_int i = 0; i < len; i++) {
                    mrb_ary_set(mrb, arr, i, mrb_fixnum_value(event->sysex_data[i]));
                }
                free(event->sysex_data);
                event->sysex_data = NULL;
            }
            mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_lit(mrb, "data")), arr);
            if (event->value) {
                mrb_hash_set(mrb, hash, mrb_symbol_value(mrb_intern_lit(mrb, "truncated")),
                             mrb_true_value());
            }
            break;
        }
        default:
            break;
    }

    return hash;
}

static mrb_value
mrb_midi_input_pop_event(mrb_state *mrb, mrb_value self)
{
    midi_event_t event;
    if (!MIDI_Input_pop_event(&event)) {
        return mrb_nil_value();
    }
    return midi_event_to_hash(mrb, &event);
}

static mrb_value
mrb_midi_input_pop_event_usb(mrb_state *mrb, mrb_value self)
{
    midi_event_t event;
    if (!MIDI_Input_pop_event_usb(&event)) {
        return mrb_nil_value();
    }
    return midi_event_to_hash(mrb, &event);
}

static mrb_value
mrb_midi_input_pop_event_sam(mrb_state *mrb, mrb_value self)
{
    midi_event_t event;
    if (!MIDI_Input_pop_event_sam(&event)) {
        return mrb_nil_value();
    }
    return midi_event_to_hash(mrb, &event);
}

/* ========================================================================
 * Gem init / final
 * ======================================================================== */

void
mrb_picoruby_midi_gem_init(mrb_state *mrb)
{
    struct RClass *module_MIDI;
    struct RClass *class_Clock;
    struct RClass *class_Input;

    if (!mrb_class_defined_id(mrb, MRB_SYM(MIDI))) {
        module_MIDI = mrb_define_module_id(mrb, MRB_SYM(MIDI));
    } else {
        module_MIDI = mrb_module_get_id(mrb, MRB_SYM(MIDI));
    }

    /* Module-level methods (note scheduler / batch trigger). */
    mrb_define_module_function_id(mrb, module_MIDI, MRB_SYM(_trigger),
                                  mrb_midi_trigger, MRB_ARGS_REQ(5));
    mrb_define_module_function_id(mrb, module_MIDI, MRB_SYM(_scheduler_clear),
                                  mrb_midi_scheduler_clear, MRB_ARGS_NONE());
    mrb_define_module_function_id(mrb, module_MIDI, MRB_SYM(_send_batch),
                                  mrb_midi_send_batch, MRB_ARGS_REQ(1));

    /* MIDI::Clock */
    class_Clock = mrb_define_class_under_id(mrb, module_MIDI, MRB_SYM(Clock),
                                            mrb->object_class);
    mrb_define_method_id(mrb, class_Clock, MRB_SYM(_init_timer),
                         mrb_midi_clock_init_timer, MRB_ARGS_NONE());
    mrb_define_method_id(mrb, class_Clock, MRB_SYM(_start_timer),
                         mrb_midi_clock_start_timer, MRB_ARGS_NONE());
    mrb_define_method_id(mrb, class_Clock, MRB_SYM(_stop_timer),
                         mrb_midi_clock_stop_timer, MRB_ARGS_NONE());
    mrb_define_method_id(mrb, class_Clock, MRB_SYM(_update_timer_period),
                         mrb_midi_clock_update_period, MRB_ARGS_NONE());
    mrb_define_method_id(mrb, class_Clock, MRB_SYM(_timer_running?),
                         mrb_midi_clock_timer_running, MRB_ARGS_NONE());

    /* MIDI::Input */
    class_Input = mrb_define_class_under_id(mrb, module_MIDI, MRB_SYM(Input),
                                            mrb->object_class);
    mrb_define_method_id(mrb, class_Input, MRB_SYM(_start_task),
                         mrb_midi_input_start_task, MRB_ARGS_NONE());
    mrb_define_method_id(mrb, class_Input, MRB_SYM(_stop_task),
                         mrb_midi_input_stop_task, MRB_ARGS_NONE());
    mrb_define_method_id(mrb, class_Input, MRB_SYM(_task_running?),
                         mrb_midi_input_task_running, MRB_ARGS_NONE());
    mrb_define_method_id(mrb, class_Input, MRB_SYM(_events_available),
                         mrb_midi_input_events_available, MRB_ARGS_NONE());
    mrb_define_method_id(mrb, class_Input, MRB_SYM(_events_available_usb),
                         mrb_midi_input_events_available_usb, MRB_ARGS_NONE());
    mrb_define_method_id(mrb, class_Input, MRB_SYM(_events_available_sam),
                         mrb_midi_input_events_available_sam, MRB_ARGS_NONE());
    mrb_define_method_id(mrb, class_Input, MRB_SYM(_pop_event),
                         mrb_midi_input_pop_event, MRB_ARGS_NONE());
    mrb_define_method_id(mrb, class_Input, MRB_SYM(_pop_event_usb),
                         mrb_midi_input_pop_event_usb, MRB_ARGS_NONE());
    mrb_define_method_id(mrb, class_Input, MRB_SYM(_pop_event_sam),
                         mrb_midi_input_pop_event_sam, MRB_ARGS_NONE());
    mrb_define_method_id(mrb, class_Input, MRB_SYM(_external_bpm),
                         mrb_midi_input_external_bpm, MRB_ARGS_NONE());
    mrb_define_method_id(mrb, class_Input, MRB_SYM(_external_bpm_usb),
                         mrb_midi_input_external_bpm_usb, MRB_ARGS_NONE());
    mrb_define_method_id(mrb, class_Input, MRB_SYM(_external_bpm_sam),
                         mrb_midi_input_external_bpm_sam, MRB_ARGS_NONE());
    mrb_define_method_id(mrb, class_Input, MRB_SYM(_reset_external_clock),
                         mrb_midi_input_reset_external_clock, MRB_ARGS_NONE());
    mrb_define_method_id(mrb, class_Input, MRB_SYM(_reset_external_clock_usb),
                         mrb_midi_input_reset_external_clock_usb, MRB_ARGS_NONE());
    mrb_define_method_id(mrb, class_Input, MRB_SYM(_reset_external_clock_sam),
                         mrb_midi_input_reset_external_clock_sam, MRB_ARGS_NONE());
}

void
mrb_picoruby_midi_gem_final(mrb_state *mrb)
{
    MIDI_Input_deinit();
    MIDI_Note_scheduler_deinit();
    MIDI_Clock_deinit();
}
