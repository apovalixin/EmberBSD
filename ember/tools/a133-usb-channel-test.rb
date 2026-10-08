#!/usr/bin/env ruby
# Origin: EmberBSD - actual subprocess contracts for bounded binary USB streams.
require 'digest'
require 'rbconfig'
require 'stringio'
require 'tmpdir'
require_relative 'a133-usb-channel'

checks = 0
Dir.mktmpdir('a133-channel-') do |dir|
  child = File.join(dir, 'child')
  File.write(child, <<~'CHILD')
    #!/usr/bin/env ruby
    STDIN.binmode; STDOUT.binmode; STDERR.binmode
    case ARGV.first
    when 'echo'
      while (chunk = STDIN.read(8192)); STDOUT.write(chunk); end
    when 'failure'
      STDERR.write('HOST_SECRET'); exit 2
    when 'noise'
      out = Thread.new { STDOUT.write('x' * 2000000) }
      STDERR.write('HOST_SECRET' * 200000)
      out.join
    when 'large'
      STDOUT.write('x' * 2000000)
    when 'orphan'
      pid = fork { STDOUT.close; sleep 20 }
      File.write(ARGV.fetch(1), pid.to_s)
      exit 0
    when 'stall'
      sleep 20
    when 'close'
      STDIN.close
      exit 3
    else
      abort 'unsupported fixture'
    end
  CHILD
  File.chmod(0700, child)
  channel = A133Usb::Channel.new(child, 1)
  binary = ("\0\r\n\xffABC".b * 200000)
  result = channel.run(['echo'], source: StringIO.new(binary), input_bytes: binary.bytesize,
    limit: binary.bytesize)
  abort 'FAIL: binary stream changed' unless result[:output] == binary &&
    result[:input_sha256] == Digest::SHA256.hexdigest(binary)
  checks += 1
  sha = Digest::SHA256.new
  result = channel.run(['echo'], source: StringIO.new(binary), input_bytes: 512) { |chunk| sha.update(chunk) }
  abort 'FAIL: input bound ignored' unless sha.hexdigest == Digest::SHA256.hexdigest(binary.byteslice(0, 512)) &&
    result[:input_sha256] == sha.hexdigest && result[:output].empty?
  checks += 1
  [ ['failure', 'usb_command_failed'], ['noise', 'usb_diagnostics'],
    ['large', 'usb_output_too_large'], ['stall', 'usb_timeout'],
    ['orphan', 'usb_timeout'] ].each do |mode, reason|
    pid_path = File.join(dir, 'descendant.pid')
    start = Process.clock_gettime(Process::CLOCK_MONOTONIC)
    begin
      if mode == 'noise'
        channel.run([mode]) { |_chunk| }
      else
        channel.run([mode, pid_path])
      end
      abort "FAIL: accepted #{mode}"
    rescue A133Usb::Invalid => error
      abort "FAIL: #{mode}: #{error.message}" unless error.message == reason &&
        Process.clock_gettime(Process::CLOCK_MONOTONIC) - start < 4
    end
    if mode == 'orphan'
      pid = Integer(File.read(pid_path))
      deadline = Process.clock_gettime(Process::CLOCK_MONOTONIC) + 0.5
      begin
        loop do
          Process.kill(0, pid)
          abort 'FAIL: surviving USB descendant' if Process.clock_gettime(Process::CLOCK_MONOTONIC) >= deadline
          sleep 0.02
        end
      rescue Errno::ESRCH
        # Allow bounded system reaping after process-group termination.
      end
    end
    checks += 1
  end
  begin
    channel.run(['echo'], source: StringIO.new('x'), input_bytes: 512)
    abort 'FAIL: short input accepted'
  rescue A133Usb::Invalid => error
    abort 'FAIL: short input reason' unless error.message == 'usb_input_short'
  end
  checks += 1
  begin
    channel.run(['close'], source: StringIO.new(binary), input_bytes: binary.bytesize)
    abort 'FAIL: closed receiver accepted'
  rescue A133Usb::Invalid => error
    abort 'FAIL: closed receiver reason' unless %w[usb_command_failed usb_pipe_failed].include?(error.message)
  end
  checks += 1
end
puts "USB channel: #{checks} actual subprocess cases passed"
