/*
 * PicoRuby MIDI - Transport interface
 *
 * Abstract MIDI byte-stream transport. Each concrete transport gem
 * (USB-MIDI host, USB-MIDI device, UART/serial MIDI, BLE-MIDI, ...)
 * implements this op-table and presents itself as a midi_transport_t
 * that the protocol layer can use uniformly.
 *
 * The interface is OS-free: implementations may sit on top of FreeRTOS
 * queues, ESP-IDF drivers, TinyUSB, or pure ring buffers.
 */

#ifndef MIDI_TRANSPORT_DEFINED_H_
#define MIDI_TRANSPORT_DEFINED_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Stable transport identifiers. The protocol layer uses these to route
 * by category without string-matching Ruby class names (replacing the
 * old _get_transport_mask logic). */
#define MIDI_TRANSPORT_ID_NONE      0
#define MIDI_TRANSPORT_ID_USB       1   /* USB-MIDI Host or Device */
#define MIDI_TRANSPORT_ID_SERIAL    2   /* UART / DIN MIDI */
#define MIDI_TRANSPORT_ID_BLE       3   /* BLE-MIDI */

typedef struct midi_transport_ops {
    /* Send one 4-byte USB-MIDI packet.
     * Returns 0 on success, negative on error. Required.
     */
    int  (*send_packet)(void *ctx, uint8_t cable, uint8_t cin,
                        uint8_t b1, uint8_t b2, uint8_t b3);

    /* Read raw bytes/packets from the transport's RX buffer into `buf`.
     * Returns the number of bytes read, 0 if none available, or
     * negative on error. NULL if the transport is send-only.
     */
    int  (*read_bytes)(void *ctx, uint8_t *buf, size_t maxlen);

    /* Bytes currently available to read. NULL if send-only. */
    int  (*bytes_available)(void *ctx);

    /* Whether the transport is currently usable (e.g. USB enumerated,
     * UART driver installed, BLE link up). Required.
     */
    bool (*is_connected)(void *ctx);

    /* Stable category identifier; one of MIDI_TRANSPORT_ID_*. */
    uint8_t transport_id;
} midi_transport_ops_t;

typedef struct midi_transport {
    const midi_transport_ops_t *ops;
    void *ctx;
} midi_transport_t;

#ifdef __cplusplus
}
#endif

#endif /* MIDI_TRANSPORT_DEFINED_H_ */
