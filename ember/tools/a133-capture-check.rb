#!/usr/bin/env ruby
# Origin: EmberBSD - fresh private backup/copy checks against a trusted capture record.
require 'json'
require 'optparse'
require_relative 'a133-backup-check'

module A133Capture
  class Invalid < StandardError; end
  class Object < Hash
    def []=(key,value)
      raise Invalid, 'duplicate_capture_key' if key?(key)
      super
    end
  end
  ROLES = %w[bootloader env boot recovery].freeze
  module_function

  def fields!(value,fields,reason)
    raise Invalid,reason unless value.is_a?(Hash) && value.keys.sort==fields.sort
  end

  def identity!(serial,cid)
    raise Invalid,'invalid_capture_identity' unless serial.is_a?(String) &&
      serial.bytesize.between?(1,128) && serial.match?(/\A[a-zA-Z0-9._-]+\z/) &&
      cid.is_a?(String) && cid.match?(/\A[0-9a-f]{32}\z/)
  end

  def private_file(path,identities)
    before = File.lstat(path)
    safe = ->(stat) {stat.file? && stat.uid==Process.uid && stat.nlink==1 && stat.mode&07777==0600}
    raise Invalid,'unsafe_capture_file' unless safe.call(before)
    File.open(path,File::RDONLY|File::NOFOLLOW|File::NONBLOCK) do |file|
      opened = file.stat
      raise Invalid,'unsafe_capture_file' unless safe.call(opened) &&
        before.dev==opened.dev && before.ino==opened.ino
      identity = [opened.dev,opened.ino]
      raise Invalid,'duplicate_capture_inode' if identities.key?(identity)
      identities[identity] = true
      result = yield file,opened.size
      current = File.lstat(path)
      after = file.stat
      raise Invalid,'capture_file_changed' unless [:dev,:ino,:size,:mtime,:ctime].all? do |field|
        opened.public_send(field)==after.public_send(field) &&
          opened.public_send(field)==current.public_send(field)
      end
      result
    end
  end

  def private_files(paths,identities,opened=[],&block)
    return yield opened if paths.empty?
    private_file(paths.first,identities) do |file,size|
      private_files(paths.drop(1),identities,opened+[[file,size]],&block)
    end
  end

  def read_record(file,size)
    raise Invalid,'capture_manifest_too_large' if size>65536
    text = file.read(65537) || ''.b
    raise Invalid,'capture_manifest_too_large' if text.bytesize>65536
    JSON.parse(text,object_class:Object,allow_duplicate_key:false)
  end

  def validate_record!(record,serial,cid,manifest_name)
    fields!(record,%w[schema board serial cid uncompressed_sha256 backup partitions],'invalid_capture_fields')
    raise Invalid,'unsupported_capture' unless record['schema'].is_a?(Integer) &&
      record['schema']==1 && record['board']=='ys-m33-a133'
    identity!(record['serial'],record['cid'])
    raise Invalid,'capture_identity_mismatch' unless record['serial']==serial && record['cid']==cid
    raise Invalid,'invalid_capture_sha256' unless record['uncompressed_sha256'].is_a?(String) &&
      record['uncompressed_sha256'].match?(/\A[0-9a-f]{64}\z/)
    fields!(record['backup'],%w[file format],'invalid_capture_backup')
    raise Invalid,'invalid_capture_backup' unless %w[raw zstd].include?(record['backup']['format'])
    parts = record['partitions']
    raise Invalid,'invalid_capture_partitions' unless parts.is_a?(Array) && parts.size==4
    parts.each { |part| fields!(part,%w[role file],'invalid_capture_partitions') }
    roles = parts.map { |part| part['role'] }
    raise Invalid,'invalid_capture_partitions' unless roles.all? { |role| role.is_a?(String) } && roles.sort==ROLES.sort
    names = [record['backup']['file']] + parts.map { |part| part['file'] }
    raise Invalid,'invalid_capture_filename' unless names.all? do |name|
      name.is_a?(String) && name.bytesize.between?(1,128) && name.match?(/\A[a-zA-Z0-9][a-zA-Z0-9._-]*\z/)
    end
    raise Invalid,'duplicate_capture_filename' unless names.uniq.size==5 && !names.include?(manifest_name)
  end

  def verify(path,serial:,cid:,zstd:'zstd',timeout:3600)
    identity!(serial,cid)
    raise Invalid,'invalid_arguments' unless path.is_a?(String) && timeout.is_a?(Integer) &&
      (1..7200).cover?(timeout) && zstd.is_a?(String) && !zstd.empty?
    serial,cid = serial.dup.freeze,cid.dup.freeze
    path = File.expand_path(path)
    directory = File.dirname(path)
    stat = File.lstat(directory)
    raise Invalid,'unsafe_capture_directory' unless stat.directory? && stat.uid==Process.uid && stat.mode&07777==0700
    directory = File.realpath(directory)
    path = File.join(directory,File.basename(path))
    Timeout.timeout(timeout) do
      identities = {}
      private_file(path,identities) do |manifest,manifest_size|
        record = read_record(manifest,manifest_size)
        validate_record!(record,serial,cid,File.basename(path))
        backup_path = File.join(directory,record['backup']['file'])
        paths = [backup_path] + record['partitions'].map { |part| File.join(directory,part['file']) }
        # Keep every input open; check each inode/path again before returning.
        private_files(paths,identities) do |opened|
          receipt = A133Backup.verify_file(backup_path,sha256:record['uncompressed_sha256'],
            format:record['backup']['format'],zstd:zstd,timeout:timeout)
          record['partitions'].each_with_index do |part,index|
            role = part['role']
            size = A133Backup::INVENTORY.find { |entry| entry[:name]==role }.fetch(:sectors)*512
            file,actual_size = opened.fetch(index+1)
            raise Invalid,'critical_copy_size_mismatch' unless actual_size==size && file.stat.size==size
            digest = Digest::SHA256.new
            while (chunk=file.read(1048576))
              digest.update(chunk)
            end
            raise Invalid,'critical_copy_hash_mismatch' unless digest.hexdigest==receipt[:partition_sha256].fetch(role)
          end
          bound = receipt.each_with_object({}) { |(key,value),out| out[key.to_s]=value }
          bound.merge('serial'=>serial,'cid'=>cid,'critical_copies_verified'=>true,
            'device_binding'=>'matches_trusted_capture_identifiers')
        end
      end
    end
  rescue Invalid => error
    raise Invalid,error.message,cause:nil
  rescue A133Backup::Invalid => error
    raise Invalid,error.message,cause:nil
  rescue Timeout::Error
    raise Invalid,'capture_read_timeout',cause:nil
  rescue JSON::ParserError => error
    reason = error.message.start_with?('duplicate key ') ? 'duplicate_capture_key' : 'invalid_capture_json'
    raise Invalid,reason,cause:nil
  rescue EncodingError,ArgumentError
    raise Invalid,'invalid_capture_json',cause:nil
  rescue SystemCallError,IOError
    raise Invalid,'capture_file_unavailable',cause:nil
  end
  private_class_method :fields!,:identity!,:private_file,:private_files,:read_record,:validate_record!
end

if $PROGRAM_NAME==__FILE__
  result = {writes_performed:0,installation_ready:false}
  begin
    options = {zstd:'zstd',timeout:3600}
    OptionParser.new do |parser|
      parser.on('--serial ID') { |value| options[:serial]=value }
      parser.on('--cid HEX') { |value| options[:cid]=value }
      parser.on('--zstd PATH') { |value| options[:zstd]=value }
      parser.on('--timeout SECONDS',Integer) { |value| options[:timeout]=value }
    end.parse!
    raise A133Capture::Invalid,'invalid_arguments' unless ARGV.size==1 &&
      options.key?(:serial) && options.key?(:cid)
    receipt = A133Capture.verify(ARGV.first,**options)
    result.merge!(status:'capture_integrity_verified',bytes:receipt['bytes'],
      critical_copies_verified:true,identity_matches_record:true,
      filesystem_consistency:receipt['filesystem_consistency'])
    puts JSON.pretty_generate(result)
  rescue A133Capture::Invalid,OptionParser::ParseError => error
    result.merge!(status:'inspection_stopped',reason:error.is_a?(A133Capture::Invalid) ? error.message : 'invalid_arguments')
    puts JSON.pretty_generate(result)
    exit 1
  end
end
