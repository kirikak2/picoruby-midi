# Print every MIDI message arriving on a connected USB MIDI controller.

require 'midi'
require 'usb_midi_host'

usb = USB_MIDI_HOST.instance
puts "Waiting for USB MIDI device..."
sleep_ms 100 until usb.connected?

input = MIDI::Input.new(MIDI::Device.new(usb))

input.on(:note_on)        { |e| puts "NOTE ON  ch=#{e[:channel]} #{MIDI::Notes.name(e[:note])} vel=#{e[:velocity]}" }
input.on(:note_off)       { |e| puts "NOTE OFF ch=#{e[:channel]} #{MIDI::Notes.name(e[:note])}" }
input.on(:control_change) { |e| puts "CC       ch=#{e[:channel]} cc=#{e[:cc]} val=#{e[:value]}" }
input.on(:pitch_bend)     { |e| puts "BEND     ch=#{e[:channel]} val=#{e[:value]}" }
input.on(:clock)          { puts "CLK" }

input.start
loop do
  input.process
  sleep_ms 5
end
