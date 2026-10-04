/*
 * PicoRuby MIDI - Routing table (OS-free)
 *
 * See include/midi_route.h.
 */

#include <stddef.h>
#include "../include/midi_route.h"
#include "../include/midi_transport.h"

typedef struct {
    volatile uint8_t src;    /* single transport bit; 0 = slot unused */
    volatile uint8_t dst;    /* transport mask */
    volatile int8_t channel; /* 0..15, or MIDI_ROUTE_ALL_CHANNELS */
} midi_route_t;

static midi_route_t g_routes[MIDI_ROUTE_MAX];

static bool single_bit(uint8_t b)
{
    return b != 0 && (b & (b - 1)) == 0;
}

int MIDI_route_add(uint8_t src_bit, uint8_t dst_mask, int channel)
{
    if (!single_bit(src_bit)) return -1;
    dst_mask &= (uint8_t)~src_bit;            /* no echo back to the source */
    if (dst_mask == 0) return -1;
    if (channel < MIDI_ROUTE_ALL_CHANNELS || channel > 15) return -1;

    for (int i = 0; i < MIDI_ROUTE_MAX; i++) {
        if (g_routes[i].src == src_bit && g_routes[i].channel == channel) {
            g_routes[i].dst |= dst_mask;
            return 0;
        }
    }
    for (int i = 0; i < MIDI_ROUTE_MAX; i++) {
        if (g_routes[i].src == 0) {
            g_routes[i].dst = dst_mask;
            g_routes[i].channel = (int8_t)channel;
            g_routes[i].src = src_bit;        /* last: publishes the slot */
            return 0;
        }
    }
    return -1;
}

int MIDI_route_remove(uint8_t src_bit, uint8_t dst_mask)
{
    int dropped = 0;
    for (int i = 0; i < MIDI_ROUTE_MAX; i++) {
        if (g_routes[i].src != src_bit) continue;
        uint8_t left = g_routes[i].dst & (uint8_t)~dst_mask;
        if (left == 0) {
            g_routes[i].src = 0;              /* first: unpublishes the slot */
            g_routes[i].dst = 0;
            dropped++;
        } else {
            g_routes[i].dst = left;
        }
    }
    return dropped;
}

void MIDI_route_clear(void)
{
    for (int i = 0; i < MIDI_ROUTE_MAX; i++) {
        g_routes[i].src = 0;
        g_routes[i].dst = 0;
    }
}

bool MIDI_route_active(void)
{
    for (int i = 0; i < MIDI_ROUTE_MAX; i++) {
        if (g_routes[i].src != 0) return true;
    }
    return false;
}

/* The channel a packet belongs to, or -1 for system messages (and SysEx,
 * whose continuation packets carry no status byte). */
static int packet_channel(uint8_t cin, uint8_t b1)
{
    switch (cin & 0x0F) {
    case 0x8: case 0x9: case 0xA: case 0xB:
    case 0xC: case 0xD: case 0xE:
        return b1 & 0x0F;
    default:
        return -1;
    }
}

void MIDI_route_packet(uint8_t src_bit, uint8_t cin,
                       uint8_t b1, uint8_t b2, uint8_t b3)
{
    int ch = packet_channel(cin, b1);
    uint8_t dst = 0;
    for (int i = 0; i < MIDI_ROUTE_MAX; i++) {
        if (g_routes[i].src != src_bit) continue;
        int rc = g_routes[i].channel;
        if (rc == MIDI_ROUTE_ALL_CHANNELS || rc == ch) {
            dst |= g_routes[i].dst;
        }
    }
    /* One send per destination even when several routes name it. */
    if (dst != 0) {
        MIDI_transport_send(dst, 0, cin & 0x0F, b1, b2, b3);
    }
}
