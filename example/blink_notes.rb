# Trigger a four-on-the-floor pattern at 120 BPM on a SAM2695 synth.
# Demonstrates MIDI.start! + MIDI::Device#trigger.

require 'midi'
require 'sam2695'

device = MIDI::Device.new(SAM2695.new(17, 18))
device.program_change(0)            # Acoustic Grand Piano

# subdivisions: 4 -> block fires 4 times per beat (16th notes)
MIDI.start!(bpm: 120, output: device, subdivisions: 4) do |clock|
  beat = clock.beat
  case beat % 16
  when 0  then device.trigger(36, 100, duration: 100)   # kick
  when 4  then device.trigger(38, 100, duration: 100)   # snare
  when 8  then device.trigger(36, 100, duration: 100)
  when 12 then device.trigger(38, 100, duration: 100)
  end
  device.trigger(42, 50, duration: 80)                  # hat every 16th
end
