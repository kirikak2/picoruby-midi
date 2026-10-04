/*
 * Host-build unit tests for the transport registry and the route table
 * (src/midi_transport_registry.c, src/midi_route.c). No FreeRTOS needed:
 *
 *     make -C host_test
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/midi_transport.h"
#include "../include/midi_route.h"

#define EXPECT_EQ(a, b) do { \
    long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { \
        fprintf(stderr, "FAIL %s:%d  %s=%lld  %s=%lld\n", \
                __FILE__, __LINE__, #a, _a, #b, _b); \
        exit(1); \
    } \
} while (0)

/* Fake transports: count packets and remember the last one. */
static int g_hits[8];
static uint8_t g_last[8][4];

static int fake_send(void *ctx, uint8_t cable, uint8_t cin,
                     uint8_t b1, uint8_t b2, uint8_t b3)
{
    int i = (int)(intptr_t)ctx;
    (void)cable;
    g_hits[i]++;
    g_last[i][0] = cin; g_last[i][1] = b1; g_last[i][2] = b2; g_last[i][3] = b3;
    return 0;
}

static const midi_transport_ops_t fake_ops = { .send_packet = fake_send };
static midi_transport_t g_t[8];

static void reset_hits(void) { memset(g_hits, 0, sizeof(g_hits)); }

int main(void)
{
    for (int i = 0; i < 8; i++) {
        g_t[i].ops = &fake_ops;
        g_t[i].ctx = (void *)(intptr_t)i;
    }

    printf("registry\n");
    EXPECT_EQ(MIDI_transport_register_at(0x01, &g_t[0]), 0);   /* USB host */
    EXPECT_EQ(MIDI_transport_register_at(0x02, &g_t[1]), 0);   /* UART */
    EXPECT_EQ(MIDI_transport_register_at(0x04, &g_t[2]), 0);   /* USB device */
    EXPECT_EQ(MIDI_transport_register_at(0x01, &g_t[3]), -1);  /* taken */
    EXPECT_EQ(MIDI_transport_register_at(0x03, &g_t[3]), -1);  /* not one bit */
    EXPECT_EQ(MIDI_transport_register(&g_t[3]), 0x08);         /* e.g. AMY */
    EXPECT_EQ(MIDI_transport_register(&g_t[3]), 0x08);         /* idempotent */
    EXPECT_EQ(MIDI_transport_register(&g_t[4]), 0x10);
    MIDI_transport_send(0x03, 0, 0x9, 0x90, 60, 100);
    EXPECT_EQ(g_hits[0], 1); EXPECT_EQ(g_hits[1], 1); EXPECT_EQ(g_hits[2], 0);
    MIDI_transport_unregister(0x10);
    EXPECT_EQ((intptr_t)MIDI_transport_get(0x10), 0);
    reset_hits();

    printf("route: arguments\n");
    EXPECT_EQ(MIDI_route_active(), 0);
    EXPECT_EQ(MIDI_route_add(0x03, 0x08, -1), -1);   /* source not one bit */
    EXPECT_EQ(MIDI_route_add(0x01, 0x01, -1), -1);   /* only itself */
    EXPECT_EQ(MIDI_route_add(0x01, 0x08, 16), -1);   /* bad channel */

    printf("route: all channels\n");
    EXPECT_EQ(MIDI_route_add(0x01, 0x08, MIDI_ROUTE_ALL_CHANNELS), 0);
    EXPECT_EQ(MIDI_route_active(), 1);
    MIDI_route_packet(0x01, 0x9, 0x93, 60, 100);      /* ch 3 note on */
    EXPECT_EQ(g_hits[3], 1);
    EXPECT_EQ(g_last[3][0], 0x9); EXPECT_EQ(g_last[3][1], 0x93);
    EXPECT_EQ(g_last[3][2], 60);  EXPECT_EQ(g_last[3][3], 100);
    MIDI_route_packet(0x01, 0xF, 0xF8, 0, 0);         /* clock passes */
    EXPECT_EQ(g_hits[3], 2);
    MIDI_route_packet(0x02, 0x9, 0x90, 60, 100);      /* other source */
    EXPECT_EQ(g_hits[3], 2);
    EXPECT_EQ(g_hits[0], 0);                          /* never back to source */
    reset_hits();

    printf("route: one channel\n");
    EXPECT_EQ(MIDI_route_add(0x02, 0x02 | 0x04, 9), 0);  /* self bit dropped */
    MIDI_route_packet(0x02, 0x9, 0x99, 36, 127);      /* ch 9 */
    MIDI_route_packet(0x02, 0x9, 0x90, 36, 127);      /* ch 0: filtered */
    MIDI_route_packet(0x02, 0xF, 0xF8, 0, 0);         /* clock: filtered */
    EXPECT_EQ(g_hits[2], 1); EXPECT_EQ(g_hits[1], 0);
    reset_hits();

    printf("route: merge and dedupe\n");
    EXPECT_EQ(MIDI_route_add(0x01, 0x04, MIDI_ROUTE_ALL_CHANNELS), 0); /* merged */
    EXPECT_EQ(MIDI_route_add(0x01, 0x08, 0), 0);      /* overlaps the all-channel route */
    MIDI_route_packet(0x01, 0xB, 0xB0, 74, 64);
    EXPECT_EQ(g_hits[3], 1);                          /* once, not twice */
    EXPECT_EQ(g_hits[2], 1);
    reset_hits();

    printf("route: remove\n");
    EXPECT_EQ(MIDI_route_remove(0x01, 0x08), 1);      /* ch0 route had only 0x08 */
    MIDI_route_packet(0x01, 0xB, 0xB0, 74, 64);
    EXPECT_EQ(g_hits[3], 0); EXPECT_EQ(g_hits[2], 1);
    EXPECT_EQ(MIDI_route_remove(0x01, 0xFF), 1);
    EXPECT_EQ(MIDI_route_active(), 1);                /* the UART route is left */
    MIDI_route_clear();
    EXPECT_EQ(MIDI_route_active(), 0);

    printf("route: table full\n");
    for (int ch = 0; ch < MIDI_ROUTE_MAX; ch++) {
        EXPECT_EQ(MIDI_route_add(0x01, 0x08, ch), 0);
    }
    EXPECT_EQ(MIDI_route_add(0x01, 0x08, MIDI_ROUTE_ALL_CHANNELS), -1);
    EXPECT_EQ(MIDI_route_add(0x01, 0x04, 0), 0);      /* merging still works */
    MIDI_route_clear();

    printf("all route tests passed\n");
    return 0;
}
