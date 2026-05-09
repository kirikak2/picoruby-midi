# MIDI Input Handler
#
# Event-driven MIDI input processing with background task
# Events are automatically processed when using MIDI.sleep_ms

# Global list of active MIDI inputs for auto-processing
$__midi_active_inputs__ = []

# Registered handlers for MIDI._notify_bpm_change
$__midi_bpm_change_handlers__ = []

module MIDI
  class << self
    # Register a callback fired when an external source (e.g. a UI tempo
    # widget) calls MIDI._notify_bpm_change(new_bpm). The block receives
    # the new BPM as a Numeric. Returns the block so it can be passed to
    # off_bpm_change later.
    def on_bpm_change(&block)
      $__midi_bpm_change_handlers__ << block
      block
    end

    # Unregister a callback previously returned by on_bpm_change.
    # mrubyc does not implement Array#delete(value); use delete_if so the
    # ensure-paths in start!/bpm_loop don't blow up with NoMethodError and
    # mask the actual exception that triggered the unwind.
    def off_bpm_change(handler)
      $__midi_bpm_change_handlers__.delete_if { |h| h == handler }
    end

    # Notify all registered handlers of a new BPM value. Host application
    # code (UI tempo widgets, MIDI Time Code translators, ...) calls this
    # to push the current tempo into running clock loops.
    def _notify_bpm_change(new_bpm)
      $__midi_bpm_change_handlers__.each do |h|
        begin
          h.call(new_bpm)
        rescue
          # Drop the broken handler rather than aborting the notify chain.
        end
      end
    end

    # Sleep with automatic MIDI event processing
    # Use this instead of sleep_ms to auto-dispatch events
    # @param ms [Integer] Milliseconds to sleep
    def sleep_ms(ms)
      # Process events before sleeping
      process_all_inputs

      # Sleep in small chunks, processing events between
      remaining = ms
      while remaining > 0
        chunk = remaining > 50 ? 50 : remaining
        Kernel.sleep_ms(chunk)
        process_all_inputs
        remaining -= chunk
      end
    end

    # Process all active inputs
    def process_all_inputs
      $__midi_active_inputs__.each do |inp|
        inp.process
      end
    end

    # Register an input for auto-processing
    def register_input(input)
      $__midi_active_inputs__ << input unless $__midi_active_inputs__.include?(input)
    end

    # Unregister an input.
    # mrubyc lacks Array#delete(value); delete_if matches by value and
    # keeps Input#stop's cleanup path NoMethodError-free.
    def unregister_input(input)
      $__midi_active_inputs__.delete_if { |i| i == input }
    end

    # Get current time in milliseconds (internal helper)
    # Note: mrubyc doesn't have 'defined?' keyword
    def _uptime_ms
      Machine.uptime_us / 1000
    rescue
      0
    end

    # MIDI Clock constant: 24 Pulses Per Quarter Note
    PPQ = 24

    # Run the BPM-driven dispatch loop. The recommended entry point for
    # scripts that need MIDI clock output, external BPM sync, or
    # block-per-clock dispatching.
    #
    # Compared with the legacy MIDI.bpm_loop:
    #
    #   - `bpm_source:` is folded into `bpm:` (pass a Proc / callable for
    #     dynamic tempo).
    #   - `send_start:` is removed; MIDI Start is sent automatically when
    #     `output:` is non-nil, and MIDI Stop is sent on exit.
    #   - `sync:` now takes the MIDI::Input directly (or nil), replacing
    #     the old `sync: + input:` combination.
    #   - `on_loop:` / `on_error:` are removed; application-layer hooks
    #     (e.g. UI.process injection, ScriptManager stop checks) are
    #     wrapped externally — see midori's main_task_base.rb for the
    #     pattern.
    #
    # The block, when given, is yielded clock_count at every subdivision
    # boundary. With no block, the method is "send clock + poll inputs"
    # only.
    #
    # @param bpm [Numeric, #call] BPM. If callable, polled per iteration
    #   to allow dynamic tempo.
    # @param output [MIDI::Device, nil] Clock output device. nil = no
    #   clock send (input poll only).
    # @param sync [MIDI::Input, nil] External sync source. When non-nil,
    #   external_bpm is sampled periodically (~200ms) and current BPM
    #   tracks it.
    # @param subdivisions [Integer] Block call rate per beat. Default 24
    #   (yield on every 24 PPQ tick). Common alternatives: 1 (quarter),
    #   4 (sixteenth), 8 (32nd).
    # @yieldparam clock_count [Integer] Current 24 PPQ clock count.
    def start!(bpm: 120, output: nil, sync: nil, subdivisions: 24, &block)
      callable_bpm = bpm.respond_to?(:call) ? bpm : nil
      current_bpm = (callable_bpm ? callable_bpm.call : bpm).to_f
      unless current_bpm > 20.0 && current_bpm < 300.0
        current_bpm = 120.0
      end

      clock_interval_ms = 60000.0 / current_bpm / PPQ
      clocks_per_subdivision = PPQ / subdivisions

      # External BPM updates from MIDI._notify_bpm_change land here; the
      # main loop picks them up at the start of each iteration.
      pending_external_bpm = nil
      bpm_change_handler = on_bpm_change do |new_bpm|
        pending_external_bpm = new_bpm.to_f
      end

      output&.send_start

      begin
        clock_count = 0
        next_clock_time = _uptime_ms
        last_sync_time = _uptime_ms

        while true
          loop_start = _uptime_ms

          # Update BPM from callable, external notifier, or sync source
          new_bpm = nil
          if pending_external_bpm
            new_bpm = pending_external_bpm
            pending_external_bpm = nil
          elsif callable_bpm
            begin
              b = callable_bpm.call
              new_bpm = b.to_f if b
            rescue
              callable_bpm = nil  # disable on error
            end
          elsif sync && (loop_start - last_sync_time) > 200
            ext = sync.external_bpm
            new_bpm = ext if ext > 0
            last_sync_time = loop_start
          end

          if new_bpm && new_bpm > 20.0 && new_bpm < 300.0 && new_bpm != current_bpm
            old_interval = clock_interval_ms
            current_bpm = new_bpm
            clock_interval_ms = 60000.0 / current_bpm / PPQ
            # Re-anchor the clock when BPM changes by more than 5% to
            # avoid catch-up storms.
            if (old_interval - clock_interval_ms).abs > old_interval * 0.05
              next_clock_time = loop_start
            end
          end

          # Walk forward through any clocks that should already have
          # fired since the last iteration. Caps catch-up to avoid
          # storms on long pauses.
          clocks_to_send = 0
          now = _uptime_ms
          while output && now >= next_clock_time
            clocks_to_send += 1
            next_clock_time += clock_interval_ms
            if clocks_to_send >= 48
              next_clock_time = _uptime_ms + clock_interval_ms
              break
            end
          end

          if clocks_to_send > 0
            i = 0
            while i < clocks_to_send
              output.send_clock
              if block_given? && (clock_count % clocks_per_subdivision) == 0
                yield clock_count
              end
              clock_count += 1
              i += 1
            end
            process_all_inputs
          elsif !output
            # No clock output: run the block at the subdivision rate so
            # the loop still has a useful cadence.
            subdivision_interval_ms = 60000.0 / current_bpm / subdivisions
            if block_given?
              yield clock_count
              process_all_inputs
            end
            clock_count += 1

            elapsed = _uptime_ms - loop_start
            remaining = subdivision_interval_ms - elapsed
            if remaining > 1
              Kernel.sleep_ms(remaining.to_i)
            else
              Kernel.sleep_ms(1)
            end
            next
          end

          now = _uptime_ms
          sleep_time = next_clock_time - now
          if sleep_time > 1
            Kernel.sleep_ms(sleep_time.to_i)
          else
            Kernel.sleep_ms(1)
          end

          process_all_inputs
        end
      ensure
        off_bpm_change(bpm_change_handler) if bpm_change_handler
        output&.send_stop
      end
    end

    # BPM-based loop with automatic task switching and MIDI clock output
    # Use this instead of `loop do end` to ensure FreeRTOS task switching
    # Sends MIDI clock at 24 PPQ when output device is provided
    #
    # @param bpm [Integer] Beats per minute (default: 120)
    # @param output [MIDI::Device, nil] MIDI output device for clock (optional)
    # @param subdivisions [Integer] Subdivisions per beat for block execution
    #   1 = quarter notes (every 24 clocks)
    #   2 = eighth notes (every 12 clocks)
    #   4 = sixteenth notes (every 6 clocks)
    #   8 = 32nd notes (every 3 clocks)
    #   24 = every clock (for MML with maximum timing precision)
    # @param send_start [Boolean] Send MIDI Start message before loop (default: true)
    # @param sync [Boolean] Sync to external MIDI clock BPM (default: false)
    # @param input [MIDI::Input, nil] MIDI input for external BPM sync (required if sync: true)
    # @param on_loop [Proc, nil] Callback executed on each loop iteration (optional)
    # @param on_error [Proc, nil] Callback executed on error (optional)
    # @yieldparam clock_count [Integer] Current MIDI clock count (24 PPQ)
    # @example MML playback with eighth note resolution
    #   MIDI.bpm_loop(120, output: device, subdivisions: 2) do |clock|
    #     player.tick(clock)
    #   end
    # @example MML playback with full resolution
    #   MIDI.bpm_loop(120, output: device, subdivisions: 24) do |clock|
    #     player.tick(clock)
    #   end
    def bpm_loop(bpm = 120, output: nil, subdivisions: 1, send_start: true, sync: false, input: nil, on_loop: nil, on_error: nil, bpm_source: nil)
      # Current BPM (may be updated by sync, bpm_source, or
      # MIDI._notify_bpm_change)
      current_bpm = bpm.to_f

      # Calculate initial intervals
      # Clock interval: 60000ms / BPM / 24 PPQ
      clock_interval_ms = 60000.0 / current_bpm / PPQ

      # Block execution interval: every N clocks
      # For subdivisions=1 (quarter note): every 24 clocks
      # For subdivisions=2 (eighth note): every 12 clocks
      # For subdivisions=4 (sixteenth note): every 6 clocks
      clocks_per_subdivision = PPQ / subdivisions

      # Send MIDI Start if output provided
      if output && send_start
        output.send_start
      end

      clock_count = 0
      next_clock_time = _uptime_ms
      last_sync_time = _uptime_ms

      # External BPM updates from MIDI._notify_bpm_change land here.
      pending_external_bpm = nil
      bpm_change_handler = on_bpm_change do |new_bpm|
        pending_external_bpm = new_bpm.to_f
      end

      # If bpm_source is now set (auto-detected or provided), get initial BPM
      if bpm_source
        new_bpm = bpm_source.call
        if new_bpm > 20.0 && new_bpm < 300.0
          current_bpm = new_bpm
          clock_interval_ms = 60000.0 / current_bpm / PPQ
          next_clock_time = _uptime_ms
        end
      # If sync enabled, try to get initial BPM from external source
      elsif sync && input
        ext_bpm = input.external_bpm
        if ext_bpm > 20.0 && ext_bpm < 300.0
          current_bpm = ext_bpm
          clock_interval_ms = 60000.0 / current_bpm / PPQ
          next_clock_time = _uptime_ms
        end
      end

      begin
      while true
        # Record loop start time for accurate timing
        loop_start = _uptime_ms

        # Apply pending BPM update from MIDI._notify_bpm_change first
        if pending_external_bpm
          new_bpm = pending_external_bpm
          pending_external_bpm = nil
          if new_bpm > 20.0 && new_bpm < 300.0 && new_bpm != current_bpm
            old_interval = clock_interval_ms
            current_bpm = new_bpm
            clock_interval_ms = 60000.0 / current_bpm / PPQ
            if (old_interval - clock_interval_ms).abs > old_interval * 0.05
              next_clock_time = loop_start
            end
          end
        # Update BPM from bpm_source if provided
        elsif bpm_source
          begin
            new_bpm = bpm_source.call
            if new_bpm > 20.0 && new_bpm < 300.0 && new_bpm != current_bpm
              old_interval = clock_interval_ms
              current_bpm = new_bpm
              clock_interval_ms = 60000.0 / current_bpm / PPQ
              # Reset next_clock_time when BPM changes significantly (>5%)
              if (old_interval - clock_interval_ms).abs > old_interval * 0.05
                next_clock_time = loop_start
              end
            end
          rescue => e
            # Disable bpm_source on error
            bpm_source = nil
          end
        # Sync to external BPM periodically (every ~200ms)
        elsif sync && input && (loop_start - last_sync_time) > 200
          ext_bpm = input.external_bpm
          if ext_bpm > 20.0 && ext_bpm < 300.0
            old_interval = clock_interval_ms
            current_bpm = ext_bpm
            clock_interval_ms = 60000.0 / current_bpm / PPQ
            # Reset next_clock_time when BPM changes significantly (>5%)
            if (old_interval - clock_interval_ms).abs > old_interval * 0.05
              next_clock_time = loop_start
            end
          end
          last_sync_time = loop_start
        end

        # Process clocks - send multiple if we're behind
        clocks_to_send = 0
        now = _uptime_ms
        while output && now >= next_clock_time
          clocks_to_send += 1
          next_clock_time += clock_interval_ms
          # Limit catch-up to prevent infinite loop
          if clocks_to_send >= 48
            # Too far behind - reset timing
            next_clock_time = _uptime_ms + clock_interval_ms
            break
          end
        end

        # Send clocks and execute user block
        if clocks_to_send > 0
          i = 0
          while i < clocks_to_send
            output.send_clock

            # Execute user block at subdivision boundaries
            if block_given? && (clock_count % clocks_per_subdivision) == 0
              yield clock_count
            end

            clock_count += 1
            i += 1
          end
          # Process inputs once after all clocks sent
          process_all_inputs
        elsif !output
          # No output device - execute block at subdivision interval
          subdivision_interval_ms = 60000.0 / current_bpm / subdivisions

          if block_given?
            yield clock_count
            process_all_inputs
          end
          clock_count += 1

          # Execute on_loop callback if provided
          begin
            on_loop.call if on_loop
          rescue Exception => e
            on_error.call(e) if on_error
          end

          # Calculate sleep time accounting for loop overhead
          elapsed = _uptime_ms - loop_start
          remaining = subdivision_interval_ms - elapsed
          if remaining > 1
            Kernel.sleep_ms(remaining.to_i)
          else
            Kernel.sleep_ms(1)
          end
          next
        end

        # Calculate sleep time until next clock
        now = _uptime_ms
        sleep_time = next_clock_time - now

        # Sleep until next clock time
        if sleep_time > 1
          Kernel.sleep_ms(sleep_time.to_i)
        else
          # Minimum sleep for task switching
          Kernel.sleep_ms(1)
        end

        # Execute on_loop callback if provided
        begin
          on_loop.call if on_loop
        rescue Exception => e
          on_error.call(e) if on_error
        end

        # Process inputs after sleep (before next loop iteration)
        process_all_inputs
      end
      ensure
        off_bpm_change(bpm_change_handler) if bpm_change_handler
      end
    end

    # Get external BPM from the first active input
    # @return [Float] Detected BPM from external MIDI clock, or 0.0 if none
    def external_bpm
      inp = $__midi_active_inputs__.first
      inp ? inp.external_bpm : 0.0
    end

    # Run a block for a specified duration with task switching
    # @param duration_ms [Integer] Total duration in milliseconds
    # @param interval_ms [Integer] Interval between iterations (default: 10ms)
    # @yield Block to execute each iteration
    # @return [void]
    def run_for(duration_ms, interval_ms: 10)
      end_time = _uptime_ms + duration_ms

      while _uptime_ms < end_time
        yield if block_given?
        sleep_ms(interval_ms)
      end
    end
  end

  class Input
    # @param device [MIDI::Device] MIDI device to read from
    # @param auto_process [Boolean] If true, register for auto-processing
    def initialize(device, auto_process: true)
      @device = device
      @transport = device.transport
      @handlers = {}
      @task_started = false
      @auto_process = auto_process

      # Determine which queue to use based on transport
      @queue_type = determine_queue_type(@transport)

      # Auto-start and register
      start
      MIDI.register_input(self) if auto_process
    end

    # Determine which queue to use based on transport class
    # @param transport [Object] Transport instance (SAM2695, UART_MIDI,
    #   USB_MIDI_HOST, etc.)
    # @return [Symbol] :usb or :sam2695
    def determine_queue_type(transport)
      case transport.class.to_s
      when "SAM2695", "UART_MIDI"
        :sam2695
      else
        :usb  # USB_MIDI_HOST or unknown — default to USB queue
      end
    end

    # Start background input processing task
    # Call this to begin receiving MIDI events
    def start
      return self if @task_started

      # Serial transports (SAM2695, UART_MIDI) own their own input task.
      # Use class-name comparison instead of is_a? so this works on boards
      # that don't load the class (constant would be undefined).
      cls = @transport.class.to_s
      if cls == "SAM2695" || cls == "UART_MIDI"
        @transport.start_input
      end

      ret = _start_task
      @task_started = (ret == 0)
      self
    end

    # Stop background input processing task
    def stop
      return self unless @task_started
      _stop_task
      @task_started = false
      MIDI.unregister_input(self)

      cls = @transport.class.to_s
      if cls == "SAM2695" || cls == "UART_MIDI"
        @transport.stop_input
      end

      self
    end

    # Check if background task is running
    def running?
      _task_running?
    end

    # Register an event handler
    # @param event_type [Symbol] Event type:
    #   :note_on, :note_off, :control_change, :program_change,
    #   :pitch_bend, :poly_aftertouch, :channel_pressure,
    #   :clock, :start, :stop, :continue, :system_reset,
    #   :sysex, :any
    # @yield [event] Block to call when event occurs
    # @yieldparam event [Hash] Event data
    #   SysEx events: { type: :sysex, source: :usb|:sam2695,
    #                   data: Array<Integer> (incl. F0..F7),
    #                   truncated: true } (truncated only set when
    #                   message exceeded MIDI_SYSEX_MAX_LEN)
    def on(event_type, &block)
      @handlers[event_type] ||= []
      @handlers[event_type] << block
      self
    end

    # Process pending MIDI events from background task
    # @param max_events [Integer] Maximum events to process per call
    # @return [Integer] Number of events processed
    def process(max_events = 64)
      count = 0
      while count < max_events
        # Pop event from the appropriate queue based on transport type
        event = if @queue_type == :sam2695
          _pop_event_sam
        else
          _pop_event_usb
        end
        break if event.nil?

        dispatch_event(event)
        count += 1
      end

      count
    end

    # Get number of pending events
    def events_available
      _events_available
    end

    # Get external BPM calculated from incoming MIDI clock
    # Returns BPM from the appropriate source based on transport type
    # @return [Float] Detected BPM, or 0.0 if not enough data
    def external_bpm
      # Return BPM from the appropriate source based on transport.
      # Class-name comparison avoids NameError when the class isn't loaded.
      cls = @transport.class.to_s
      if cls == "SAM2695" || cls == "UART_MIDI"
        _external_bpm_sam
      else
        # USB-MIDI Host or other transports
        _external_bpm_usb
      end
    end

    # Get external BPM from USB-MIDI source
    # @return [Float] Detected BPM, or 0.0 if not enough data
    def external_bpm_usb
      _external_bpm_usb
    end

    # Get external BPM from SAM2695 (MIDI-DIN) source
    # @return [Float] Detected BPM, or 0.0 if not enough data
    def external_bpm_sam
      _external_bpm_sam
    end

    # Reset external clock tracking
    # Call this when starting to sync to a new clock source
    def reset_external_clock
      _reset_external_clock
    end

    # Reset USB-MIDI external clock tracking
    def reset_external_clock_usb
      _reset_external_clock_usb
    end

    # Reset SAM2695 (MIDI-DIN) external clock tracking
    def reset_external_clock_sam
      _reset_external_clock_sam
    end

    private

    def dispatch_event(event)
      # Call :any handlers
      @handlers[:any]&.each { |h| h.call(event) }

      # Call specific type handlers
      @handlers[event[:type]]&.each { |h| h.call(event) }
    end
  end
end
