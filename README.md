# picoruby-midi

MIDI protocol layer for PicoRuby — parser, scheduler, clock, and a
transport-agnostic device wrapper. Pair with one of the transport gems
(`picoruby-usb_midi_host`, `picoruby-uart_midi`, `picoruby-sam2695`) to
actually send and receive MIDI bytes.

## Usage

### Send notes

```ruby
require 'midi'
require 'sam2695'

device = MIDI::Device.new(SAM2695.new(17, 18))
device.note_on(60, 100)
sleep 1
device.note_off(60)
```

### Trigger with auto-off (multitouch-friendly)

```ruby
device.trigger(60, 100, duration: 200)   # note_off auto-fires after 200ms
```

### Receive events

```ruby
input = MIDI::Input.new(device)
input.on(:note_on) { |e| puts "ON #{e[:note]} vel=#{e[:velocity]}" }
input.start
loop do
  input.process
  sleep_ms 5
end
```

### BPM-synced loop

`MIDI.start!` runs the clock + dispatches MIDI Input events + invokes
the block at every subdivision tick:

```ruby
require 'midi'

MIDI.start!(bpm: 120, output: device, subdivisions: 4) do |clock|
  beat = clock.beat
  device.note_on(60 + (beat % 8), 100)
end
```

External BPM source (e.g. an incoming clock from another device) can be
fed in by hooking `MIDI.on_bpm_change`:

```ruby
MIDI.on_bpm_change { |new_bpm| puts "BPM is now #{new_bpm}" }
```

## API

### Top-level

- `MIDI.start!(bpm:, output:, sync:, subdivisions:) { |clock| ... }` -
  Canonical BPM-synced loop entry point.
- `MIDI.on_bpm_change { |new_bpm| ... }` - Register a hook called when
  the live BPM changes (manual set or external sync).
- `MIDI.input(device)` / `MIDI.clock(device)` - Convenience builders.
- `MIDI.usb_host_device` - Returns a `MIDI::Device` wrapping the USB MIDI
  Host transport, or `nil` if nothing is connected.
- `MIDI.sam2695_device(tx_pin, rx_pin = -1)` - Returns a `MIDI::Device`
  wrapping a SAM2695 synth on the given UART pins, or `nil` if not ready.
- `MIDI.sleep_ms(ms)` - Sleep that yields to MIDI input dispatch so you
  don't drop incoming events.
- `MIDI.bpm_loop(...)` - Legacy version of `start!`. Kept for now;
  prefer `start!`.

### `MIDI::Device`

Wraps any transport that implements `send_packet` / `transport_id` /
`connected?`.

- `note_on(note, velocity = 127, channel: 0)`
- `note_off(note, velocity = 0, channel: 0)`
- `trigger(note, velocity = 127, duration: 100, channel: 0)` - Sends
  `note_on` immediately and schedules `note_off` after `duration` ms
  via the gem's note scheduler.
- `trigger_batch([{note:, velocity:, duration:, channel:}, ...])`
- `control_change(cc, value, channel: 0)`
- `program_change(program, channel: 0)`
- `pitch_bend(value, channel: 0)` - 14-bit, range 0..16383, 8192 = center
- `poly_aftertouch(note, pressure, channel: 0)`
- `channel_pressure(pressure, channel: 0)`
- Convenience CC wrappers: `modulation`, `volume`, `pan`, `expression`,
  `sustain`
- `all_notes_off(channel: 0)` / `all_sound_off(channel: 0)` /
  `reset_all_controllers(channel: 0)`
- Realtime: `send_clock` / `send_start` / `send_stop` / `send_continue`
- `send_sysex(data, wrap: false)` - Send a SysEx message. `data` is an
  array of bytes; with `wrap: true` the leading `0xF0` and trailing `0xF7`
  are added automatically.

### `MIDI::Input`

- `MIDI::Input.new(device, auto_process: true)`
- `on(event_type) { |event| ... }` where `event_type` is `:note_on`,
  `:note_off`, `:control_change`, `:program_change`, `:pitch_bend`,
  `:poly_aftertouch`, `:channel_pressure`, `:clock`, `:start`, `:stop`,
  `:continue`, `:system_reset`, `:sysex`, or `:any`
- `start` / `stop` - Start/stop the background read task
- `process(max_events = 64)` - Pop and dispatch queued events
- `external_bpm` - Current external BPM (from incoming clock)

### `MIDI::Clock`

- `MIDI::Clock.new(device = nil)`
- `bpm`, `bpm=(new_bpm)`
- `start` / `stop` / `continue` / `running?`
- `receive_clock` / `receive_start` / `receive_stop` - Feed in external
  clock bytes for sync.
- `external_bpm` / `sync_to_external`

### `MIDI::Note`

- `MIDI::Note.number(MIDI::Note::C, 4)` -> 60
- `MIDI::Note.name(60)` -> `"C4"`
- Note-name constants: `MIDI::Note::C`, `Cs`/`Db`, `D`, `Ds`/`Eb`, `E`, `F`,
  `Fs`/`Gb`, `G`, `Gs`/`Ab`, `A`, `As`/`Bb`, `B` (0..11)

## Architecture

The protocol code is split into OS-free cores plus thin per-platform
ports:

- `src/midi_parser.c` - Byte/packet parser, SysEx accumulator
- `src/midi_scheduler.c` - Note off scheduler (driven by `tick(now_us)`)
- `src/midi_clock_gen.c` - Clock generator (driven by `tick(now_us)`)
- `ports/esp32/midi.c` - ESP32 port: esp_timer-driven clock, FreeRTOS
  input task

Transports plug in via the `midi_transport_t` interface
(`include/midi_transport.h`); per-transport gems implement it and
register themselves with the input task.

## Notes

- Default PPQ is 24 (MIDI standard).
- The note scheduler runs from the same 1 ms timer as the clock, so
  `trigger` is non-blocking and multitouch-safe.
- mruby and mrubyc bindings are at feature parity.
