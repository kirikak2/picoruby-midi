/*
 * PicoRuby MIDI - ESP32 High-Precision Clock Timer
 *
 * Uses esp_timer for accurate MIDI clock generation
 */

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "../../include/midi.h"
#include "../../include/midi_parser.h"
#include "../../include/midi_scheduler.h"
#include "../../include/midi_clock_gen.h"
#include "../../include/midi_transport.h"
#include "../../../picoruby-usb_midi_host/include/usb_midi_host.h"
#include "../../../picoruby-uart_midi/include/uart_midi.h"

/* Application-supplied cooperative-stop hooks. Both default to NULL,
 * meaning "never stop on app request, no cleanup to run". The host
 * application registers these via MIDI_set_stop_check() /
 * MIDI_set_cleanup_hook() at init time. */
static midi_stop_check_fn g_stop_check_fn = NULL;
static midi_cleanup_fn    g_cleanup_fn    = NULL;

void MIDI_set_stop_check(midi_stop_check_fn fn)
{
    g_stop_check_fn = fn;
}

void MIDI_set_cleanup_hook(midi_cleanup_fn fn)
{
    g_cleanup_fn = fn;
}

static inline bool midi_stop_requested(void)
{
    return g_stop_check_fn ? g_stop_check_fn() : false;
}

static inline void midi_run_cleanup(void)
{
    if (g_cleanup_fn) g_cleanup_fn();
}

static const char *TAG = "MIDI_CLOCK";

/* ========================================
 * Transport instances - wrap the existing per-gem APIs in the
 * picoruby-midi Transport interface. Static instances live here so the
 * port can route by transport_mask without picoruby-midi reaching back
 * into transport-specific symbols. Phase 5 of the standardization plan
 * will move ownership of these into each transport gem.
 * ======================================== */

static int usb_tx_send_packet(void *ctx, uint8_t cable, uint8_t cin,
                              uint8_t b1, uint8_t b2, uint8_t b3)
{
    (void)ctx;
    return USB_MIDI_HOST_send_packet(cable, cin, b1, b2, b3);
}

static int usb_tx_read_bytes(void *ctx, uint8_t *buf, size_t maxlen)
{
    (void)ctx;
    return USB_MIDI_HOST_read_packet(buf, maxlen);
}

static int usb_tx_bytes_available(void *ctx)
{
    (void)ctx;
    return USB_MIDI_HOST_bytes_available();
}

static bool usb_tx_is_connected(void *ctx)
{
    (void)ctx;
    return USB_MIDI_HOST_get_status() == USB_MIDI_HOST_CONNECTED;
}

static const midi_transport_ops_t g_usb_tx_ops = {
    .send_packet     = usb_tx_send_packet,
    .read_bytes      = usb_tx_read_bytes,
    .bytes_available = usb_tx_bytes_available,
    .is_connected    = usb_tx_is_connected,
    .transport_id    = MIDI_TRANSPORT_ID_USB,
};

/* Const so the transport struct lands in .rodata rather than .data and
 * does not perturb the internal-DRAM .bss layout (see CLAUDE.md). */
static const midi_transport_t g_usb_transport = {
    .ops = &g_usb_tx_ops,
    .ctx = NULL,
};

static int sam_tx_send_packet(void *ctx, uint8_t cable, uint8_t cin,
                              uint8_t b1, uint8_t b2, uint8_t b3)
{
    (void)ctx;
    return UART_MIDI_send_packet(cable, cin, b1, b2, b3);
}

static int sam_tx_read_bytes(void *ctx, uint8_t *buf, size_t maxlen)
{
    (void)ctx;
    return UART_MIDI_read_bytes(buf, maxlen);
}

static int sam_tx_bytes_available(void *ctx)
{
    (void)ctx;
    return UART_MIDI_bytes_available();
}

static bool sam_tx_is_connected(void *ctx)
{
    (void)ctx;
    return UART_MIDI_Input_is_running();
}

static const midi_transport_ops_t g_sam_tx_ops = {
    .send_packet     = sam_tx_send_packet,
    .read_bytes      = sam_tx_read_bytes,
    .bytes_available = sam_tx_bytes_available,
    .is_connected    = sam_tx_is_connected,
    .transport_id    = MIDI_TRANSPORT_ID_SERIAL,
};

/* Const for the same reason as g_usb_transport. */
static const midi_transport_t g_sam_transport = {
    .ops = &g_sam_tx_ops,
    .ctx = NULL,
};

/* Timer handle for the periodic 24 PPQ tick */
static esp_timer_handle_t g_clock_timer = NULL;

/* Send callback registered with the clock_gen core: emits a single
 * Timing Clock byte (0xF8) over the USB-MIDI transport. */
static void clock_send_byte(void)
{
    g_usb_transport.ops->send_packet(g_usb_transport.ctx, 0,
                                     USB_MIDI_HOST_CIN_SINGLE_BYTE,
                                     MIDI_STATUS_TIMING_CLOCK, 0, 0);
}

/* Timer callback (called from esp_timer task context) */
static void clock_timer_callback(void *arg)
{
    if (!midi_clock_gen_is_running()) {
        return;
    }

    /* Check if the application has requested a stop - run cleanup and halt */
    if (midi_stop_requested()) {
        ESP_LOGI(TAG, "Stop requested, performing cleanup...");
        midi_run_cleanup();
        midi_clock_gen_stop();
        esp_timer_stop(g_clock_timer);
        return;
    }

    midi_clock_gen_tick((uint64_t)esp_timer_get_time());
}

int MIDI_Clock_init(void)
{
    if (g_clock_timer != NULL) {
        return 0;  /* Already initialized */
    }

    midi_clock_gen_init();
    midi_clock_gen_set_send_callback(clock_send_byte);

    esp_timer_create_args_t timer_args = {
        .callback = clock_timer_callback,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "midi_clock",
        .skip_unhandled_events = true
    };

    esp_err_t ret = esp_timer_create(&timer_args, &g_clock_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create timer: %d", ret);
        return -1;
    }

    ESP_LOGI(TAG, "Clock timer initialized, BPM=%.1f, period=%llu us",
             midi_clock_gen_get_bpm(), midi_clock_gen_get_period_us());

    return 0;
}

void MIDI_Clock_deinit(void)
{
    if (g_clock_timer != NULL) {
        if (midi_clock_gen_is_running()) {
            MIDI_Clock_stop();
        }
        esp_timer_delete(g_clock_timer);
        g_clock_timer = NULL;
    }
    midi_clock_gen_deinit();
}

void MIDI_Clock_set_bpm(float bpm)
{
    midi_clock_gen_set_bpm(bpm);
    uint64_t period = midi_clock_gen_get_period_us();

    if (midi_clock_gen_is_running() && g_clock_timer != NULL) {
        /* Restart hardware timer with the new period */
        esp_timer_stop(g_clock_timer);
        esp_timer_start_periodic(g_clock_timer, period);
    }

    ESP_LOGD(TAG, "BPM set to %.1f, period=%llu us",
             midi_clock_gen_get_bpm(), period);
}

void MIDI_Clock_start(void)
{
    if (g_clock_timer == NULL) {
        if (MIDI_Clock_init() != 0) {
            return;
        }
    }

    if (midi_clock_gen_is_running()) {
        return;
    }

    midi_clock_gen_start((uint64_t)esp_timer_get_time());
    esp_timer_start_periodic(g_clock_timer, midi_clock_gen_get_period_us());
    ESP_LOGI(TAG, "Clock started at %.1f BPM", midi_clock_gen_get_bpm());
}

void MIDI_Clock_stop(void)
{
    if (!midi_clock_gen_is_running() || g_clock_timer == NULL) {
        return;
    }

    esp_timer_stop(g_clock_timer);
    midi_clock_gen_stop();
    ESP_LOGI(TAG, "Clock stopped");
}

bool MIDI_Clock_is_running(void)
{
    return midi_clock_gen_is_running();
}

void MIDI_Clock_set_callback(midi_clock_callback_t callback)
{
    midi_clock_gen_set_user_callback(callback);
}

/* ========================================
 * MIDI Input Background Task Implementation
 * ======================================== */

static const char *INPUT_TAG = "MIDI_INPUT";

/* Event queues - separate queues for USB and SAM2695 */
#define MIDI_EVENT_QUEUE_SIZE 256
static QueueHandle_t g_usb_event_queue = NULL;
static QueueHandle_t g_sam_event_queue = NULL;

/* Task handle */
static TaskHandle_t g_input_task = NULL;
static volatile bool g_input_running = false;
static volatile bool g_input_was_started = false;  /* Track if input was ever started (for auto-restart) */

/* Per-source MIDI parsers (allocated in MIDI_Input_init).
 * The parser layer is OS-free and lives in src/midi_parser.c.
 */
static midi_parser_t *g_usb_parser = NULL;
static midi_parser_t *g_sam_parser = NULL;

/* Background task that reads from USB-MIDI and SAM2695 and queues events */
static void midi_input_task(void *arg)
{
    /* Large buffer to drain RX buffer completely each iteration */
    uint8_t buffer[256];
    midi_event_t event;
    bool usb_midi_available = false;
    bool sam2695_available = false;

    ESP_LOGI(INPUT_TAG, "Input task started");

    while (g_input_running) {
        /* Check if the application has requested a stop */
        if (midi_stop_requested()) {
            ESP_LOGI(INPUT_TAG, "Stop requested, exiting input task");
            break;
        }

        /* Check available input sources via the Transport interface */
        usb_midi_available = g_usb_transport.ops->is_connected(g_usb_transport.ctx);
        sam2695_available  = g_sam_transport.ops->is_connected(g_sam_transport.ctx);

        /* Exit if no input sources available */
        if (!usb_midi_available && !sam2695_available) {
            ESP_LOGI(INPUT_TAG, "No input sources available, exiting input task");
            break;
        }

        /* Process USB-MIDI input */
        if (usb_midi_available) {
            int bytes_avail = g_usb_transport.ops->bytes_available(g_usb_transport.ctx);

            while (bytes_avail >= 4) {
                int read_len = g_usb_transport.ops->read_bytes(g_usb_transport.ctx,
                                                               buffer, sizeof(buffer));
                ESP_LOGD(INPUT_TAG, "Read %d bytes from USB MIDI", read_len);

                if (read_len <= 0) break;

                /* Parse packets */
                for (int i = 0; i + 3 < read_len; i += 4) {
                    uint8_t cin = buffer[i] & 0x0F;
                    uint8_t midi1 = buffer[i + 1];
                    uint8_t midi2 = buffer[i + 2];
                    uint8_t midi3 = buffer[i + 3];

                    ESP_LOGD(INPUT_TAG, "USB Packet[%d]: cin=0x%02X m1=0x%02X m2=0x%02X m3=0x%02X",
                             i/4, cin, midi1, midi2, midi3);

                    if (midi_parser_feed_usb(g_usb_parser, cin, midi1, midi2, midi3,
                                             (uint64_t)esp_timer_get_time(), &event)) {
                        ESP_LOGD(INPUT_TAG, "USB event type=%d ch=%d d1=%d d2=%d",
                                 event.type, event.channel, event.data1, event.data2);
                        /* Send to USB queue */
                        if (xQueueSend(g_usb_event_queue, &event, 0) != pdTRUE) {
                            ESP_LOGW(INPUT_TAG, "USB event queue full, dropping event");
                            if (event.sysex_data) {
                                free(event.sysex_data);
                                event.sysex_data = NULL;
                            }
                        }
                    }
                }

                bytes_avail = g_usb_transport.ops->bytes_available(g_usb_transport.ctx);
            }
        }

        /* Process SAM2695 input */
        if (sam2695_available) {
            int bytes_avail = g_sam_transport.ops->bytes_available(g_sam_transport.ctx);

            if (bytes_avail > 0) {
                int read_len = g_sam_transport.ops->read_bytes(g_sam_transport.ctx,
                                                               buffer, sizeof(buffer));
                ESP_LOGD(INPUT_TAG, "Read %d bytes from SAM2695", read_len);

                if (read_len > 0) {
                    /* Parse raw MIDI bytes */
                    for (int i = 0; i < read_len; i++) {
                        ESP_LOGD(INPUT_TAG, "SAM2695 byte[%d]: 0x%02X", i, buffer[i]);

                        if (midi_parser_feed_raw(g_sam_parser, buffer[i],
                                                 (uint64_t)esp_timer_get_time(), &event)) {
                            ESP_LOGD(INPUT_TAG, "SAM2695 event type=%d ch=%d d1=%d d2=%d",
                                     event.type, event.channel, event.data1, event.data2);
                            /* Send to SAM2695 queue */
                            if (xQueueSend(g_sam_event_queue, &event, 0) != pdTRUE) {
                                ESP_LOGW(INPUT_TAG, "SAM event queue full, dropping event");
                                if (event.sysex_data) {
                                    free(event.sysex_data);
                                    event.sysex_data = NULL;
                                }
                            }
                        }
                    }
                }
            }
        }

        /* Short delay to yield CPU */
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    /* Mark as not running before cleanup */
    g_input_running = false;

    /* Reset parsers (running status, SysEx, BPM tracker) */
    midi_parser_reset(g_usb_parser);
    midi_parser_reset(g_sam_parser);

    ESP_LOGI(INPUT_TAG, "Input task stopped");
    vTaskDelete(NULL);
}

/*
 * Initialize input task
 */
int MIDI_Input_init(void)
{
    if (g_usb_event_queue != NULL && g_sam_event_queue != NULL
        && g_usb_parser != NULL && g_sam_parser != NULL) {
        return 0;  /* Already initialized */
    }

    /* Ensure DEBUG level logging is enabled for MIDI_INPUT */
    esp_log_level_set(INPUT_TAG, ESP_LOG_DEBUG);

    /* Create USB event queue */
    if (g_usb_event_queue == NULL) {
        g_usb_event_queue = xQueueCreate(MIDI_EVENT_QUEUE_SIZE, sizeof(midi_event_t));
        if (g_usb_event_queue == NULL) {
            ESP_LOGE(INPUT_TAG, "Failed to create USB event queue");
            return -1;
        }
    }

    /* Create SAM2695 event queue */
    if (g_sam_event_queue == NULL) {
        g_sam_event_queue = xQueueCreate(MIDI_EVENT_QUEUE_SIZE, sizeof(midi_event_t));
        if (g_sam_event_queue == NULL) {
            ESP_LOGE(INPUT_TAG, "Failed to create SAM event queue");
            vQueueDelete(g_usb_event_queue);
            g_usb_event_queue = NULL;
            return -1;
        }
    }

    /* Create parsers */
    if (g_usb_parser == NULL) {
        g_usb_parser = midi_parser_new(MIDI_SOURCE_USB);
        if (g_usb_parser == NULL) {
            ESP_LOGE(INPUT_TAG, "Failed to allocate USB parser");
            return -1;
        }
    }
    if (g_sam_parser == NULL) {
        g_sam_parser = midi_parser_new(MIDI_SOURCE_SAM2695);
        if (g_sam_parser == NULL) {
            ESP_LOGE(INPUT_TAG, "Failed to allocate SAM parser");
            midi_parser_free(g_usb_parser);
            g_usb_parser = NULL;
            return -1;
        }
    }

    ESP_LOGI(INPUT_TAG, "Input initialized (USB and SAM2695 queues + parsers)");
    return 0;
}

/*
 * Deinitialize input task
 */
void MIDI_Input_deinit(void)
{
    MIDI_Input_stop();

    if (g_usb_event_queue != NULL) {
        vQueueDelete(g_usb_event_queue);
        g_usb_event_queue = NULL;
    }

    if (g_sam_event_queue != NULL) {
        vQueueDelete(g_sam_event_queue);
        g_sam_event_queue = NULL;
    }

    if (g_usb_parser != NULL) {
        midi_parser_free(g_usb_parser);
        g_usb_parser = NULL;
    }
    if (g_sam_parser != NULL) {
        midi_parser_free(g_sam_parser);
        g_sam_parser = NULL;
    }
}

/*
 * Start background processing task
 */
int MIDI_Input_start(void)
{
    if (MIDI_Input_init() != 0) {
        return -1;
    }

    if (g_input_running) {
        return 0;  /* Already running */
    }

    /* Check if at least one input source is available via the Transport interface */
    bool usb_midi_available = g_usb_transport.ops->is_connected(g_usb_transport.ctx);
    bool sam2695_available  = g_sam_transport.ops->is_connected(g_sam_transport.ctx);

    ESP_LOGI(INPUT_TAG, "Starting input task - USB MIDI: %s, SAM2695: %s",
             usb_midi_available ? "available" : "not available",
             sam2695_available ? "available" : "not available");

    if (!usb_midi_available && !sam2695_available) {
        ESP_LOGW(INPUT_TAG, "Cannot start input task: no input sources available");
        return -1;
    }

    /* Reset parsers (running status, SysEx, BPM tracker) */
    midi_parser_reset(g_usb_parser);
    midi_parser_reset(g_sam_parser);

    g_input_running = true;
    BaseType_t ret = xTaskCreatePinnedToCore(
        midi_input_task,
        "midi_input",
        4096,  /* Increased stack size */
        NULL,
        1,  /* Priority - same level as app_main */
        &g_input_task,
        1   /* Core 1, same as PicoRuby (consumer of events) */
    );

    if (ret != pdTRUE) {
        ESP_LOGE(INPUT_TAG, "Failed to create input task");
        g_input_running = false;
        return -1;
    }

    g_input_was_started = true;  /* Mark that input task was started (for auto-restart on reconnect) */
    ESP_LOGI(INPUT_TAG, "Input task started successfully");
    return 0;
}

/*
 * Check if input task was ever started (for auto-restart logic)
 */
bool MIDI_Input_was_started(void)
{
    return g_input_was_started;
}

/*
 * Drain a queue, freeing any SysEx payloads to avoid leaks.
 */
static void drain_event_queue(QueueHandle_t q)
{
    if (q == NULL) return;
    midi_event_t event;
    while (xQueueReceive(q, &event, 0) == pdTRUE) {
        if (event.sysex_data) {
            free(event.sysex_data);
        }
    }
}

/*
 * Stop background processing task
 */
void MIDI_Input_stop(void)
{
    if (!g_input_running) {
        return;
    }

    g_input_running = false;

    /* Wait for task to finish */
    vTaskDelay(pdMS_TO_TICKS(50));
    g_input_task = NULL;

    /* Drain queues (frees SysEx buffers), then reset to clear any stale state */
    drain_event_queue(g_usb_event_queue);
    drain_event_queue(g_sam_event_queue);
    if (g_usb_event_queue != NULL) xQueueReset(g_usb_event_queue);
    if (g_sam_event_queue != NULL) xQueueReset(g_sam_event_queue);

    /* Reset parsers so next run starts cleanly */
    midi_parser_reset(g_usb_parser);
    midi_parser_reset(g_sam_parser);

    ESP_LOGI(INPUT_TAG, "Input task stopped");
}

/*
 * Check if input task is running
 */
bool MIDI_Input_is_running(void)
{
    return g_input_running;
}

/*
 * Get number of events available (legacy - uses USB queue)
 */
int MIDI_Input_events_available(void)
{
    return MIDI_Input_events_available_usb();
}

/*
 * Get number of events available from USB queue
 */
int MIDI_Input_events_available_usb(void)
{
    if (g_usb_event_queue == NULL) {
        return 0;
    }
    return (int)uxQueueMessagesWaiting(g_usb_event_queue);
}

/*
 * Get number of events available from SAM2695 queue
 */
int MIDI_Input_events_available_sam(void)
{
    if (g_sam_event_queue == NULL) {
        return 0;
    }
    return (int)uxQueueMessagesWaiting(g_sam_event_queue);
}

/*
 * Pop event from queue (legacy - uses USB queue)
 */
bool MIDI_Input_pop_event(midi_event_t *event)
{
    return MIDI_Input_pop_event_usb(event);
}

/*
 * Pop event from USB queue
 */
bool MIDI_Input_pop_event_usb(midi_event_t *event)
{
    if (g_usb_event_queue == NULL || event == NULL) {
        return false;
    }
    return xQueueReceive(g_usb_event_queue, event, 0) == pdTRUE;
}

/*
 * Pop event from SAM2695 queue
 */
bool MIDI_Input_pop_event_sam(midi_event_t *event)
{
    if (g_sam_event_queue == NULL || event == NULL) {
        return false;
    }
    return xQueueReceive(g_sam_event_queue, event, 0) == pdTRUE;
}

/*
 * Get external BPM calculated from incoming MIDI clock (USB-MIDI)
 * Returns 0.0 if not enough data
 */
float MIDI_Input_get_external_bpm_usb(void)
{
    return midi_parser_get_external_bpm(g_usb_parser);
}

/*
 * Get external BPM calculated from incoming MIDI clock (SAM2695/MIDI-DIN)
 * Returns 0.0 if not enough data
 */
float MIDI_Input_get_external_bpm_sam(void)
{
    return midi_parser_get_external_bpm(g_sam_parser);
}

/*
 * Get external BPM calculated from incoming MIDI clock (legacy - returns USB BPM)
 * Returns 0.0 if not enough data
 */
float MIDI_Input_get_external_bpm(void)
{
    return midi_parser_get_external_bpm(g_usb_parser);
}

/*
 * Reset external clock tracking for USB-MIDI (call on MIDI Start)
 */
void MIDI_Input_reset_external_clock_usb(void)
{
    midi_parser_reset_external_clock(g_usb_parser);
}

/*
 * Reset external clock tracking for SAM2695 (call on MIDI Start)
 */
void MIDI_Input_reset_external_clock_sam(void)
{
    midi_parser_reset_external_clock(g_sam_parser);
}

/*
 * Reset external clock tracking (legacy - resets both)
 */
void MIDI_Input_reset_external_clock(void)
{
    MIDI_Input_reset_external_clock_usb();
    MIDI_Input_reset_external_clock_sam();
}

/* ========================================
 * Note Scheduler Port - drives the OS-free scheduler core in
 * src/midi_scheduler.c via an esp_timer and routes its send callback to
 * USB_MIDI / SAM2695 transports.
 * ======================================== */

static const char *SCHED_TAG = "NOTE_SCHED";

static esp_timer_handle_t g_note_scheduler_timer = NULL;
static volatile bool g_scheduler_running = false;

/* Timer tick interval (check every 1ms for responsive note-offs) */
#define NOTE_SCHEDULER_TICK_US 1000

/* Send callback registered with the scheduler core: routes a single
 * 3-byte channel-voice message via the Transport interface to the USB
 * and/or SAM2695 transports according to transport_mask.
 */
static void scheduler_send_packet(uint8_t transport_mask, uint8_t cin,
                                  uint8_t status, uint8_t data1, uint8_t data2)
{
    if (transport_mask & MIDI_TRANSPORT_USB) {
        g_usb_transport.ops->send_packet(g_usb_transport.ctx, 0,
                                         cin, status, data1, data2);
    }
    if (transport_mask & MIDI_TRANSPORT_SAM2695) {
        g_sam_transport.ops->send_packet(g_sam_transport.ctx, 0,
                                         cin, status, data1, data2);
    }
}

static void note_scheduler_callback(void *arg)
{
    if (!g_scheduler_running) {
        return;
    }
    midi_scheduler_tick((uint64_t)esp_timer_get_time());
}

int MIDI_Note_scheduler_init(void)
{
    if (g_note_scheduler_timer != NULL) {
        return 0;  /* Already initialized */
    }

    midi_scheduler_core_init();
    midi_scheduler_set_send_callback(scheduler_send_packet);

    esp_timer_create_args_t timer_args = {
        .callback = note_scheduler_callback,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "note_scheduler",
        .skip_unhandled_events = true
    };

    esp_err_t ret = esp_timer_create(&timer_args, &g_note_scheduler_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(SCHED_TAG, "Failed to create timer: %d", ret);
        return -1;
    }

    ret = esp_timer_start_periodic(g_note_scheduler_timer, NOTE_SCHEDULER_TICK_US);
    if (ret != ESP_OK) {
        ESP_LOGE(SCHED_TAG, "Failed to start timer: %d", ret);
        esp_timer_delete(g_note_scheduler_timer);
        g_note_scheduler_timer = NULL;
        return -1;
    }

    g_scheduler_running = true;
    ESP_LOGI(SCHED_TAG, "Note scheduler initialized");
    return 0;
}

void MIDI_Note_scheduler_deinit(void)
{
    g_scheduler_running = false;

    if (g_note_scheduler_timer != NULL) {
        esp_timer_stop(g_note_scheduler_timer);
        esp_timer_delete(g_note_scheduler_timer);
        g_note_scheduler_timer = NULL;
    }

    midi_scheduler_core_deinit();
    ESP_LOGI(SCHED_TAG, "Note scheduler deinitialized");
}

int MIDI_Note_trigger(uint8_t transport_mask, uint8_t channel, uint8_t note,
                      uint8_t velocity, uint32_t duration_ms)
{
    /* Auto-initialize scheduler if needed */
    if (g_note_scheduler_timer == NULL) {
        if (MIDI_Note_scheduler_init() != 0) {
            return -1;
        }
    }

    int ret = midi_scheduler_trigger(transport_mask, channel, note, velocity,
                                     duration_ms,
                                     (uint64_t)esp_timer_get_time());
    if (ret != 0) {
        ESP_LOGW(SCHED_TAG,
                 "Scheduler full, note_off for note %d won't be automatic",
                 note);
    }
    return ret;
}

void MIDI_Note_scheduler_clear(void)
{
    midi_scheduler_clear();
    ESP_LOGI(SCHED_TAG, "Scheduler cleared");
}
