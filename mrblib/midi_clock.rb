# MIDI Clock
#
# Master clock generation and external clock sync
# Uses high-precision hardware timer for accurate timing

module MIDI
  class Clock
    # Pulses Per Quarter Note (MIDI standard)
    PPQ = 24

    # @param device [MIDI::Device, nil] MIDI device for clock output
    def initialize(device = nil)
      @device = device
      @bpm = 120.0
      @running = false

      # External clock tracking
      @external_timestamps = []
      @external_bpm = nil
      @last_external_time = 0

      # Initialize hardware timer if available
      _init_timer if respond_to?(:_init_timer)
    end

    attr_reader :running

    # Get current BPM
    def bpm
      @bpm
    end

    # Set BPM (clamps to valid range)
    # @param new_bpm [Float] BPM value (20.0 - 300.0)
    def bpm=(new_bpm)
      @bpm = new_bpm.to_f.clamp(20.0, 300.0)
      _update_timer_period if respond_to?(:_update_timer_period)
    end

    # Start master clock
    # Sends MIDI Start message and begins clock generation
    def start
      return if @running

      @running = true
      @device&.send_start

      if respond_to?(:_start_timer)
        _start_timer
      end
    end

    # Stop master clock
    # Sends MIDI Stop message and stops clock generation
    def stop
      return unless @running

      @running = false
      @device&.send_stop

      if respond_to?(:_stop_timer)
        _stop_timer
      end
    end

    # Continue master clock
    # Sends MIDI Continue message and resumes clock generation
    def continue
      return if @running

      @running = true
      @device&.send_continue

      if respond_to?(:_start_timer)
        _start_timer
      end
    end

    # Check if clock is running
    # Uses hardware timer state for accurate status (handles stop_requested)
    def running?
      if respond_to?(:_timer_running?)
        # Get actual timer state from C (may be stopped by stop_requested)
        timer_running = _timer_running?
        # Sync Ruby state if C stopped the timer
        if @running && !timer_running
          @running = false
        end
        timer_running
      else
        @running
      end
    end

    # --- External Clock Sync ---

    # Call when MIDI Clock is received from external source
    # This calculates the external BPM
    def receive_clock
      now = _get_time_us

      @external_timestamps << now

      # Keep last PPQ timestamps (1 beat)
      while @external_timestamps.length > PPQ
        @external_timestamps.shift
      end

      # Calculate BPM from timestamps
      if @external_timestamps.length >= 2
        first_ts = @external_timestamps.first
        last_ts = @external_timestamps.last
        count = @external_timestamps.length - 1

        if count > 0
          avg_interval = (last_ts - first_ts).to_f / count
          if avg_interval > 0
            # us per clock -> BPM
            # 60,000,000 us/min / (interval_us * PPQ)
            @external_bpm = (60_000_000.0 / (avg_interval * PPQ)).round(2)
          end
        end
      end
    end

    # Call when MIDI Start is received
    def receive_start
      @external_timestamps.clear
      @external_bpm = nil
    end

    # Call when MIDI Stop is received
    def receive_stop
      # Keep timestamps for BPM reference
    end

    # Get detected external BPM
    # @return [Float, nil] Detected BPM or nil if not enough data
    def external_bpm
      @external_bpm
    end

    # Sync internal BPM to external clock
    # @return [Boolean] true if synced, false if no external BPM available
    def sync_to_external
      if @external_bpm && @external_bpm > 0
        self.bpm = @external_bpm
        true
      else
        false
      end
    end

    # Calculate interval in microseconds for given BPM
    # @param bpm [Float] BPM value
    # @return [Integer] Microseconds per clock tick
    def self.bpm_to_interval_us(bpm)
      (60_000_000.0 / bpm / PPQ).to_i
    end

    private

    # Get current time in microseconds
    # Note: mrubyc doesn't have 'defined?' keyword
    def _get_time_us
      Machine.uptime_us
    rescue
      0
    end
  end
end
