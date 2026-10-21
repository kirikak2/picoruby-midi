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
# Sources are the transports the input task reads -- the USB-MIDI host, the
# UART (SAM2695 / UART_MIDI) and the USB-MIDI device (what the PC sends) --
# plus transports that declare themselves a
# source and forward their own output in C (e.g. picoruby-ui's UI::Keys, an
# on-screen keyboard). Any registered transport can be a destination.
# A MIDI::Input on the same source keeps working alongside: it receives the
# same messages, routed or not.
#
# MIDI.route returns a MIDI::Route, which can take a Ruby filter
# (MIDI.filter, midi_filter.rb):
#
#   MIDI.route(kb, syn).filter(types: [:note_on, :note_off]) { |m| m.note += 12; m }
module MIDI
  # Transports the input task reads from (USB host 0x01, UART 0x02, USB
  # device 0x04). A route may also start from any transport in
  # MIDI._route_sources.
  #
  # The USB device is read by the input task only while a route starts from
  # it; it then takes every packet the PC sends, so do not also poll
  # USB_MIDI_DEVICE#read_available meanwhile.
  ROUTE_SOURCES = 0x07

  # channel: value meaning "every channel, plus clock / start / stop / SysEx"
  ROUTE_ALL_CHANNELS = -1

  class << self
    # Forward messages from one transport to another.
    #
    # @param from [Object] source transport, or a MIDI::Device on it
    # @param to [Object] destination transport, or a MIDI::Device on it
    # @param channel [Integer, nil] 0..15 to pass only that channel's voice
    #   messages; nil (default) passes every channel and system messages
    # @return [MIDI::Route, false] false if the route table is full
    def route(from, to, channel: nil)
      src = _check_source(from, "MIDI.route")
      dst = route_bit(to)
      raise ArgumentError, "MIDI.route: destination is not a registered transport" if dst == 0
      raise ArgumentError, "MIDI.route: source and destination are the same" if src == dst
      if !channel.nil? && (channel < 0 || channel > 15)
        raise ArgumentError, "MIDI.route: channel must be 0..15"
      end

      return false if _route_add(src, dst, channel.nil? ? ROUTE_ALL_CHANNELS : channel) != 0
      _start_source(from, src)
      Route.new(from, to, channel)
    end

    # Stop forwarding from `from` to `to`, or everything from `from` when
    # `to` is omitted. Their filters go too; MIDI.on listeners stay.
    # @return [Integer] routes removed
    def unroute(from, to = nil)
      unfilter(from, to)
      dst = to.nil? ? 0xFF : route_bit(to)
      _route_remove(route_bit(from), dst)
    end

    # Remove every route, filter and listener.
    def unroute_all
      _route_clear
      $__midi_filters__ = {}
    end

    # The transport-registry bit of a transport or of a MIDI::Device's
    # transport (0 if it has none). An Integer is taken as the bit itself.
    def route_bit(obj)
      return obj if obj.is_a?(Integer)
      transport = obj.respond_to?(:transport) ? obj.transport : obj
      transport.respond_to?(:transport_id) ? transport.transport_id : 0
    end

    # Internal: the bit of `from`, raising unless a route may start there.
    def _check_source(from, who)
      src = route_bit(from)
      if src == 0 || (src & _route_sources) == 0
        raise ArgumentError, "#{who}: source must be the USB-MIDI host / device, a UART transport, " \
                             "or a transport that forwards its own output (e.g. UI::Keys)"
      end
      src
    end

    # Internal: make sure something reads `from` now that a route or filter
    # starts there.
    def _start_source(from, src)
      # The other sources push their packets themselves: no input task
      return if (src & ROUTE_SOURCES) == 0

      # A UART transport reads only once its own input is started.
      transport = from.respond_to?(:transport) ? from.transport : from
      if transport.respond_to?(:start_input) &&
         !(transport.respond_to?(:input_running?) && transport.input_running?)
        transport.start_input
      end
      # Fails (-1) while the USB host has no device; the USB host driver
      # starts the task once one is plugged in.
      _route_start
    end
  end

  # A route as MIDI.route returned it.
  class Route
    attr_reader :from, :to, :channel

    def initialize(from, to, channel)
      @from = from
      @to = to
      @channel = channel
    end

    # MIDI.filter on this route. Returns the block (for MIDI.unfilter).
    def filter(types: nil, channel: nil, overflow: :bypass, &block)
      MIDI.filter(@from, @to, types: types, channel: channel, overflow: overflow, &block)
    end

    # Remove this route's filters (the route stays).
    def unfilter(handler = nil)
      MIDI.unfilter(@from, @to, handler)
    end

    # Remove the route (and its filters).
    def unroute
      MIDI.unroute(@from, @to)
    end
  end
end
