#!/usr/bin/env ruby
# Origin: EmberBSD - read-only USB backup acquisition into exclusive private host files.
require 'json'
require 'optparse'
require 'timeout'
require_relative 'a133-usb-backup-source'
require_relative 'a133-capture-check'

module A133UsbBackup
  class Deadline < StandardError; end
  module_function

  def directory_sync(directory)
    File.open(directory,File::RDONLY) { |file| file.fsync }
  end

  def same_inode!(path,stat)
    current = File.lstat(path)
    raise Invalid,'usb_backup_output_changed' unless current.file? && current.dev==stat.dev && current.ino==stat.ino
  end

  def publish(partial,final,stat)
    same_inode!(partial,stat)
    File.link(partial,final)
    same_inode!(final,stat)
    same_inode!(partial,stat)
    File.unlink(partial)
    directory_sync(File.dirname(final))
  rescue Errno::EEXIST
    raise Invalid,'usb_backup_output_exists',cause:nil
  end

  def save_stream(directory,name)
    final = File.join(directory,name)
    partial = final+'.partial'
    digest = Digest::SHA256.new
    count = 0
    stat = nil
    File.open(partial,File::WRONLY|File::CREAT|File::EXCL,0600) do |file|
      stat = file.stat
      yield lambda { |chunk|
        raise Invalid,'usb_backup_host_file_error' unless file.write(chunk)==chunk.bytesize
        digest.update(chunk); count += chunk.bytesize
      }
      file.flush; file.fsync
      raise Invalid,'usb_backup_output_changed' unless file.stat.size==count && file.stat.nlink==1 && file.stat.mode&07777==0600
    end
    publish(partial,final,stat)
    {bytes:count,sha256:digest.hexdigest}
  end

  def collect(directory:,adb:'adb',serial:nil,state:'device',root_method:'vendor_su',timeout:3600)
    raise Invalid,'invalid_arguments' unless directory.is_a?(String) && timeout.is_a?(Integer) && (1..7200).cover?(timeout)
    directory = File.expand_path(directory)
    parent = File.dirname(directory)
    stat = File.lstat(parent)
    raise Invalid,'usb_backup_unsafe_parent' unless stat.directory? && stat.uid==Process.uid && stat.mode&07777==0700
    directory = File.join(File.realpath(parent),File.basename(directory))
    raise Invalid,'usb_backup_destination_exists' if File.exist?(directory) || File.symlink?(directory)
    source = Source.new(adb:adb,serial:serial,state:state,root_method:root_method,timeout:timeout)
    Timeout.timeout(timeout,Deadline) do
      identity = source.inspect!
      begin
        Dir.mkdir(directory,0700)
      rescue Errno::EEXIST
        raise Invalid,'usb_backup_destination_exists'
      end
      results = {}
      names = {'disk'=>'emmc.raw','bootloader'=>'bootloader.bin','env'=>'env.bin','boot'=>'boot.bin',
        'recovery'=>'recovery.bin','boot0'=>'boot0.bin','boot1'=>'boot1.bin'}
      names.each do |role,name|
        results[role] = save_stream(directory,name) { |sink| source.read(role) { |chunk| sink.call(chunk) } }
      end
      manifest = {'schema'=>2,'board'=>'ys-m33-a133','serial'=>identity[:serial],'cid'=>identity[:cid],
        'capture_state'=>identity[:state],'root_method'=>identity[:root_method],
        'uncompressed_sha256'=>results.fetch('disk').fetch(:sha256),
        'backup'=>{'file'=>'emmc.raw','format'=>'raw'},
        'partitions'=>%w[bootloader env boot recovery].map { |role| {'role'=>role,'file'=>names.fetch(role)} },
        'hardware_boot'=>%w[boot0 boot1].map { |role| {'role'=>role,'file'=>names.fetch(role),
          'bytes'=>results.fetch(role).fetch(:bytes),'sha256'=>results.fetch(role).fetch(:sha256)} }}
      candidate = File.join(directory,'capture.json.partial')
      candidate_stat = nil
      File.open(candidate,File::WRONLY|File::CREAT|File::EXCL,0600) do |file|
        candidate_stat = file.stat
        file.write(JSON.generate(manifest)); file.flush; file.fsync
      end
      verified = A133Capture.verify(candidate,serial:identity[:serial],cid:identity[:cid],timeout:timeout)
      source.inspect!
      publish(candidate,File.join(directory,'capture.json'),candidate_stat)
      {status:'usb_backup_captured',host_files_created:8,saved_bytes:results.values.sum { |entry| entry[:bytes] },
        hardware_boot_copies_verified:verified.fetch('hardware_boot_copies_verified'),
        filesystem_consistency:verified.fetch('filesystem_consistency'),writes_performed:0,installation_ready:false}
    end
  rescue Invalid,A133Capture::Invalid,A133Usb::Invalid,A133Backup::Invalid => error
    raise Invalid,error.message,cause:nil
  rescue Deadline,Timeout::Error
    raise Invalid,'usb_backup_timeout',cause:nil
  rescue SystemCallError,IOError
    raise Invalid,'usb_backup_host_file_error',cause:nil
  rescue ArgumentError
    raise Invalid,'invalid_arguments',cause:nil
  end
  private_class_method :directory_sync,:same_inode!,:publish,:save_stream
end

if $PROGRAM_NAME==__FILE__
  result = {writes_performed:0,installation_ready:false}
  begin
    options = {adb:'adb',state:'device',root_method:'vendor_su',timeout:3600}
    OptionParser.new do |parser|
      parser.on('--adb PATH') { |value| options[:adb]=value }
      parser.on('--serial ID') { |value| options[:serial]=value }
      parser.on('--state STATE') { |value| options[:state]=value }
      parser.on('--root-method METHOD') { |value| options[:root_method]=value }
      parser.on('--timeout SECONDS',Integer) { |value| options[:timeout]=value }
    end.parse!
    raise A133UsbBackup::Invalid,'invalid_arguments' unless ARGV.size==1
    result.merge!(A133UsbBackup.collect(directory:ARGV.first,**options))
    puts JSON.pretty_generate(result)
  rescue A133UsbBackup::Invalid,OptionParser::ParseError => error
    result.merge!(status:'capture_stopped',reason:error.is_a?(A133UsbBackup::Invalid) ? error.message : 'invalid_arguments')
    puts JSON.pretty_generate(result)
    exit 1
  end
end
