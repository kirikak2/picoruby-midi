# MIDI Module
#
# Main entry point for MIDI functionality
# Note: midi_constants.rb, midi_device.rb, midi_input.rb, midi_clock.rb
# are automatically compiled together by the mrbgem build system.

module MIDI
  class << self
    # Get USB MIDI Host device (convenience method)
    # @return [MIDI::Device, nil] MIDI device or nil if not connected
    def usb_host_device
      require 'usb_midi_host'
      transport = USB_MIDI_HOST.instance
      return nil unless transport.connected?
      Device.new(transport)
    end

    # Get SAM2695 synthesizer device (convenience method)
    # @param tx_pin [Integer] GPIO pin for MIDI TX
    # @param rx_pin [Integer] GPIO pin for MIDI RX (-1 to disable)
    # @return [MIDI::Device, nil] MIDI device or nil if not ready
    def sam2695_device(tx_pin, rx_pin = -1)
      require 'sam2695'
      transport = SAM2695.new(tx_pin, rx_pin)
      return nil unless transport.connected?
      Device.new(transport)
    end

    # Create input handler for a device
    # @param device [MIDI::Device] MIDI device
    # @return [MIDI::Input] Input handler
    def input(device)
      Input.new(device)
    end

    # Create clock controller
    # @param device [MIDI::Device, nil] MIDI device for clock output
    # @return [MIDI::Clock] Clock controller
    def clock(device = nil)
      Clock.new(device)
    end
  end
end
