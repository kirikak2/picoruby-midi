/*
 * PicoRuby MIDI - Routing (MIDI Thru between transports)
 *
 * A route forwards what arrives on one transport straight to others, in C,
 * as the input task reads it -- no Ruby in the path, so a keyboard on the
 * USB host port can play a synth with the latency of the input task rather
 * than that of a script's polling loop.
 *
 * Transports are named by their bit in the transport registry (see
 * midi_transport.h). Packets are USB-MIDI packets (CIN + 3 bytes), and
 * forwarding goes through MIDI_transport_send().
 *
 * Sources are the transports the input task reads (USB host, UART, USB
 * device -- the last only while a route starts from it) plus
 * any transport that declares itself one with MIDI_route_set_source() and
 * pushes its own packets through MIDI_route_targets() -- e.g. an on-screen
 * keyboard, which produces MIDI on the UI task rather than receiving it.
 *
 * OS-free. The table is small and fixed; writers (Ruby, via the bindings)
 * and the reader (the input task) are not locked against each other, so a
 * packet that races a change may go by the old or the new table.
 */

#ifndef MIDI_ROUTE_DEFINED_H_
#define MIDI_ROUTE_DEFINED_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MIDI_ROUTE_MAX          8
#define MIDI_ROUTE_ALL_CHANNELS (-1)

/* Forward everything from transport `src_bit` to the transports in
 * `dst_mask`. channel is 0..15 to pass only that channel's voice messages,
 * or MIDI_ROUTE_ALL_CHANNELS to pass every channel plus system messages
 * (clock, start / stop, SysEx). A route to the same source and channel as
 * an existing one adds its destinations to it.
 * Returns 0, or -1 when the arguments are invalid (src_bit not a single
 * bit, an empty destination, a destination equal to the source) or the
 * table is full. */
int MIDI_route_add(uint8_t src_bit, uint8_t dst_mask, int channel);

/* Remove the destinations in `dst_mask` from every route out of `src_bit`
 * (all of them for dst_mask 0xFF); routes left without a destination are
 * dropped. Returns how many routes were dropped. */
int MIDI_route_remove(uint8_t src_bit, uint8_t dst_mask);

/* Drop every route and every filter. */
void MIDI_route_clear(void);

/* Whether any route or filter is set (the input task keeps running while
 * one is). */
bool MIDI_route_active(void);

/* The transports a packet from `src_bit` would be forwarded to (0 for
 * none). For a source that sends on its own and wants to fold the routes
 * into one MIDI_transport_send() with destinations of its own. */
uint8_t MIDI_route_targets(uint8_t src_bit, uint8_t cin, uint8_t b1);

/* Whether any route or filter starts from `src_bit`. */
bool MIDI_route_from(uint8_t src_bit);

/* Built-in route sources: the transports the input task reads (USB host
 * 0x01, UART 0x02, USB device 0x04). */
#define MIDI_ROUTE_INPUT_SOURCES 0x07

/* Declare (on) or retract (off) `bit` as a route source besides the
 * built-in ones. Such a source forwards its own packets (see
 * MIDI_route_targets / MIDI_route_packet); the input task never reads it. */
void MIDI_route_set_source(uint8_t bit, bool on);

/* Every transport a route may start from: MIDI_ROUTE_INPUT_SOURCES plus the
 * declared ones. */
uint8_t MIDI_route_sources(void);

/* Forward one packet received on `src_bit` along the matching routes.
 * cin is the USB-MIDI Code Index Number (low nibble used), b1..b3 the
 * message bytes. Filters apply (see below). */
void MIDI_route_packet(uint8_t src_bit, uint8_t cin,
                       uint8_t b1, uint8_t b2, uint8_t b3);

/* Read route `i` (0..MIDI_ROUTE_MAX-1). Returns false for an unused slot. */
bool MIDI_route_get(int i, uint8_t *src_bit, uint8_t *dst_mask, int *channel);

/*
 * Filters (MIDI.filter / MIDI.on)
 *
 * A filter diverts what a route would forward from `src_bit` to some of its
 * destinations into a queue that Ruby drains (MIDI._run_filters), so a block
 * can rewrite, multiply or drop each message before Ruby sends it on. Only
 * the packets that match the filter's types and channel are diverted; the
 * rest -- and every destination without a filter -- still go straight out
 * in C.
 *
 * MIDI_ROUTE_DST_RUBY is a destination of its own: a filter to it receives
 * packets from `src_bit` with no route needed (what MIDI.on listens with).
 * It never names a transport, so it costs no registry bit.
 *
 * The queue itself is the port's (a FreeRTOS queue on ESP32): it hands a
 * sink to MIDI_route_set_filter_sink(). Without a sink nothing is diverted.
 */
#define MIDI_ROUTE_DST_RUBY       0x100
#define MIDI_FILTER_MAX           16

/* What happens to a packet the queue has no room for */
#define MIDI_FILTER_OVERFLOW_BYPASS 0   /* sent on unfiltered */
#define MIDI_FILTER_OVERFLOW_DROP   1   /* dropped */

/* One diverted packet: from `src`, for the destinations in `dst` (transport
 * bits and/or MIDI_ROUTE_DST_RUBY). */
typedef struct {
    uint16_t dst;
    uint8_t src;
    uint8_t cin;
    uint8_t b1, b2, b3;
} midi_filter_event_t;

/* Queues one event; false when the queue is full. Called from whatever task
 * forwards the packet (the input task, the UI task for UI::Keys). */
typedef bool (*midi_filter_sink_t)(const midi_filter_event_t *ev);

void MIDI_route_set_filter_sink(midi_filter_sink_t sink);

/* Divert packets from `src_bit` to each destination in `dst_mask` whose CIN
 * bit is set in `types` (bit n = CIN n) and, for channel messages, whose
 * channel is `channel` (or any, for MIDI_ROUTE_ALL_CHANNELS). Setting it
 * again for the same source and destination replaces it.
 * Returns 0, or -1 for invalid arguments or a full table. */
int MIDI_route_set_filter(uint8_t src_bit, uint16_t dst_mask, uint16_t types,
                          int channel, uint8_t overflow);

/* Remove the filters from `src_bit` to the destinations in `dst_mask`
 * (0xFFFF: all of them). Returns how many were removed. */
int MIDI_route_clear_filter(uint8_t src_bit, uint16_t dst_mask);

/* Destinations that have a filter from `src_bit` (for the UI). */
uint16_t MIDI_route_filtered(uint8_t src_bit);

/* Packets the queue had no room for, since start-up. */
uint32_t MIDI_route_filter_overflows(void);

/* For a source that sends on its own (e.g. UI::Keys): forward one packet to
 * `dst_mask` (normally MIDI_route_targets() of it) with the filters from
 * `src_bit` applied -- diverted destinations and Ruby listeners get it
 * through the queue, the rest directly. */
void MIDI_route_dispatch(uint8_t src_bit, uint8_t dst_mask, uint8_t cin,
                         uint8_t b1, uint8_t b2, uint8_t b3);

#ifdef __cplusplus
}
#endif

#endif /* MIDI_ROUTE_DEFINED_H_ */
