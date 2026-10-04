# MIDI routing (MIDI Thru)
#
# Forward what arrives on one transport straight to others. The forwarding
# happens in C, in the input task, as each message is read -- no Ruby in the
# path -- so a keyboard on the USB host port can play a synth with no more
# delay than the input task's poll (one RTOS tick, 10 ms at 100 Hz),
# whatever the script is doing.
#
#   MIDI.route(MIDIDevices.usb_midi_host, MIDIDevices.amy)              # everything
#   MIDI.route(MIDIDevices.usb_midi_host, MIDIDevices.sam2695, channel: 9)
#   MIDI.unroute(MIDIDevices.usb_midi_host, MIDIDevices.amy)
#   MIDI.unroute_all
#
# Sources are the transports the input task reads: the USB-MIDI host and the
# UART (SAM2695 / UART_MIDI). Any registered transport can be a destination.
# A MIDI::Input on the same source keeps working alongside: it receives the
# same messages, routed or not.
module MIDI
  # Transports MIDI.route can read from (USB host 0x01, UART 0x02)
  ROUTE_SOURCES = 0x03

  # channel: value meaning "every channel, plus clock / start / stop / SysEx"
  ROUTE_ALL_CHANNELS = -1

  class << self
    # Forward messages from one transport to another.
    #
    # @param from [Object] source transport, or a MIDI::Device on it
    # @param to [Object] destination transport, or a MIDI::Device on it
    # @param channel [Integer, nil] 0..15 to pass only that channel's voice
    #   messages; nil (default) passes every channel and system messages
    # @return [Boolean] false if the route table is full
    def route(from, to, channel: nil)
      src = route_bit(from)
      dst = route_bit(to)
      if src == 0 || (src & ROUTE_SOURCES) == 0
        raise ArgumentError, "MIDI.route: source must be the USB-MIDI host or a UART transport"
      end
      raise ArgumentError, "MIDI.route: destination is not a registered transport" if dst == 0
      raise ArgumentError, "MIDI.route: source and destination are the same" if src == dst
      if !channel.nil? && (channel < 0 || channel > 15)
        raise ArgumentError, "MIDI.route: channel must be 0..15"
      end

      return false if _route_add(src, dst, channel.nil? ? ROUTE_ALL_CHANNELS : channel) != 0

      # A UART transport reads only once its own input is started.
      transport = from.respond_to?(:transport) ? from.transport : from
      if transport.respond_to?(:start_input) &&
         !(transport.respond_to?(:input_running?) && transport.input_running?)
        transport.start_input
      end
      # Fails (-1) while the USB host has no device; the USB host driver
      # starts the task once one is plugged in.
      _route_start
      true
    end

    # Stop forwarding from `from` to `to`, or everything from `from` when
    # `to` is omitted.
    # @return [Integer] routes removed
    def unroute(from, to = nil)
      dst = to.nil? ? 0xFF : route_bit(to)
      _route_remove(route_bit(from), dst)
    end

    # Remove every route.
    def unroute_all
      _route_clear
    end

    # The transport-registry bit of a transport or of a MIDI::Device's
    # transport (0 if it has none).
    def route_bit(obj)
      transport = obj.respond_to?(:transport) ? obj.transport : obj
      transport.respond_to?(:transport_id) ? transport.transport_id : 0
    end
  end
end
