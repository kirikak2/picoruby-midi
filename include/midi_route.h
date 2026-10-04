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

/* Drop every route. */
void MIDI_route_clear(void);

/* Whether any route is set (the input task keeps running while one is). */
bool MIDI_route_active(void);

/* Forward one packet received on `src_bit` along the matching routes.
 * cin is the USB-MIDI Code Index Number (low nibble used), b1..b3 the
 * message bytes. */
void MIDI_route_packet(uint8_t src_bit, uint8_t cin,
                       uint8_t b1, uint8_t b2, uint8_t b3);

#ifdef __cplusplus
}
#endif

#endif /* MIDI_ROUTE_DEFINED_H_ */
