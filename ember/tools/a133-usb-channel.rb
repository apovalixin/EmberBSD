#!/usr/bin/env ruby
# Origin: EmberBSD - bounded binary ADB subprocess streams with exit checking.
require 'digest'
require 'open3'
require 'timeout'

module A133Usb
  class Invalid < StandardError
    attr_accessor :write_attempted
  end

  class Channel
    def initialize(adb, timeout)
      raise Invalid, 'invalid_usb_options' unless adb.is_a?(String) && !adb.empty? &&
        timeout.is_a?(Integer) && (1..7200).cover?(timeout)
      @adb, @timeout = adb, timeout
    end

    def run(arguments, source: nil, input_bytes: 0, limit: 16384)
      raise Invalid, 'invalid_usb_options' unless arguments.is_a?(Array) &&
        arguments.all? { |value| value.is_a?(String) } && input_bytes.is_a?(Integer) &&
        input_bytes >= 0 && limit.is_a?(Integer) && limit >= 0 && (source || input_bytes == 0)
      output = ''.b
      digest = Digest::SHA256.new
      Open3.popen3(@adb, *arguments, pgroup: true) do |input, out, err, waiter|
        workers = []
        begin
          [input, out, err].each(&:binmode)
          writer = Thread.new do
            remaining = input_bytes
            while remaining > 0
              chunk = source.read([remaining, 1048576].min)
              raise Invalid, 'usb_input_short' if chunk.nil? || chunk.empty?
              digest.update(chunk)
              input.write(chunk)
              remaining -= chunk.bytesize
            end
            input.close
          end
          workers << writer
          reader = Thread.new do
            while (chunk = out.read(65536))
              if block_given?
                yield chunk
              else
                raise Invalid, 'usb_output_too_large' if output.bytesize + chunk.bytesize > limit
                output << chunk
              end
            end
          end
          workers << reader
          diagnostics = Thread.new do
            noisy = false
            noisy = true while err.read(16384)
            noisy
          end
          workers << diagnostics
          workers.each { |worker| worker.report_on_exception = false }
          Timeout.timeout(@timeout) do
            writer.value
            reader.value
            noisy = diagnostics.value
            raise Invalid, 'usb_command_failed' unless waiter.value.success?
            raise Invalid, 'usb_diagnostics' if noisy
          end
        rescue Timeout::Error
          raise Invalid, 'usb_timeout'
        rescue SystemCallError, IOError
          raise Invalid, 'usb_pipe_failed'
        ensure
          begin
            Process.kill('KILL', -waiter.pid)
          rescue Errno::ESRCH
            # Also terminate descendants retaining a pipe after the parent exits.
          end
          workers.each(&:kill)
          [input, out, err].each { |io| io.close unless io.closed? }
          workers.each do |worker|
            begin
              worker.join
            rescue StandardError
              # A worker's failure was propagated by value; do not replace it in cleanup.
            end
          end
        end
      end
      {output: output, input_sha256: source ? digest.hexdigest : nil}
    rescue Errno::ENOENT, Errno::EACCES
      raise Invalid, 'usb_executable_unavailable'
    end
  end
end
