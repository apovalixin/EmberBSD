#!/usr/bin/env ruby
# Origin: EmberBSD - strict file-backed ADB boundary and real range-write tests.
require 'digest'
require 'json'
require 'open3'
require 'rbconfig'
require 'tmpdir'
require 'zlib'
require_relative 'a133-usb-transfer'

module UsbFixture
  CID = '0123456789abcdef0123456789abcdef'
  PARTS = [
    ['bootloader',73728,65536], ['env',139264,32768], ['boot',172032,65536],
    ['super',237568,4194304], ['misc',4431872,32768], ['recovery',4464640,65536],
    ['cache',4530176,1572864], ['vbmeta',6103040,32768], ['vbmeta_system',6135808,32768],
    ['vbmeta_vendor',6168576,32768], ['metadata',6201344,32768], ['private',6234112,32768],
    ['frp',6266880,1024], ['empty',6267904,31744], ['dtbo',6299648,4096],
    ['media_data',6303744,262144], ['UDISK',6565888,54054879]
  ].freeze
  module_function
  def header(current, alternate, array_lba, crc)
    data = 'EFI PART'.b + [0x10000,92,0,0].pack('V4') +
      [current,alternate,73728,60620766].pack('Q<4') + 'D' * 16 +
      [array_lba].pack('Q<') + [17,128,crc].pack('V3')
    data[16,4] = [Zlib.crc32(data)].pack('V')
    data.ljust(512, "\0")
  end
  def env
    vars = {
      'bootcmd'=>'run setargs_mmc boot_normal', 'boot_normal'=>'run ember_recovery_once',
      'boot_android'=>'sunxi_flash read 45000000 boot;bootm 45000000',
      'boot_recovery'=>'sunxi_flash read 45000000 recovery;bootm 45000000',
      'ember_recovery_once'=>'fdt addr ${fdtcontroladdr}; fdt set /soc@03000000/usbc0@0 usb_port_type <0>; run boot_recovery'
    }
    body = (vars.map { |key,value| "#{key}=#{value}\0" }.join + "\0").ljust(131068,"\0")
    [Zlib.crc32(body)].pack('V') + body
  end
  def create(dir)
    entries = PARTS.each_with_index.map do |(name,start,size),i|
      'T' * 16 + [i+1].pack('V').ljust(16,"G") + [start,start+size-1,0].pack('Q<3') +
        name.encode('UTF-16LE').b.ljust(72,"\0")
    end.join
    File.open(File.join(dir,'disk'), 'wb') do |file|
      file.truncate(31037849600)
      mbr = "\0" * 512
      mbr[446,16] = [0,0,0,0,0xee,0,0,0].pack('C8') + [1,60620799].pack('V2')
      mbr[510,2] = "\x55\xaa".b
      file.write(mbr); file.write(header(1,60620799,2,Zlib.crc32(entries))); file.write(entries)
      file.seek(60620794*512); file.write(entries)
      file.seek(60620799*512); file.write(header(60620799,1,60620794,Zlib.crc32(entries)))
    end
    [1,2,3,6,17].each do |index|
      File.open(File.join(dir,"part#{index}"),'wb') { |file| file.truncate(PARTS[index-1][2]*512) }
    end
    File.open(File.join(dir,'part2'),'r+b') { |file| file.write(env) }
    File.open(File.join(dir,'part17'),'r+b') { |file| file.seek(2048); file.write('TAIL_PRESERVED') }
    File.binwrite(File.join(dir,'source'), ("\0\r\n\xffABCD".b * 128))
    File.write(File.join(dir,'adb'), File.read(File.join(__dir__, 'a133-usb-transfer-fixture.rb')))
    File.chmod(0700, File.join(dir,'adb'))
  end
  def state(dir, updates={})
    File.write(File.join(dir,'state.json'), JSON.generate(updates))
    File.unlink(File.join(dir,'written')) if File.exist?(File.join(dir,'written'))
  end
  def backup(dir)
    {'status'=>'backup_integrity_verified', 'bytes'=>31037849600, 'cid'=>CID,
     'uncompressed_sha256'=>'a'*64, 'gpt_headers_crc'=>true, 'gpt_arrays_crc'=>true,
     'partition_layout_verified'=>true, 'partition_sha256'=>{
       'bootloader'=>'b'*64,'env'=>'c'*64,'boot'=>'d'*64,
       'recovery'=>Digest::SHA256.file(File.join(dir,'part6')).hexdigest}}
  end
end

checks = 0
Dir.mktmpdir('a133-usb-') do |dir|
  UsbFixture.create(dir)
  client = A133Usb::Client.new(adb: File.join(dir,'adb'), serial: 'USB_SAMPLE', cid: UsbFixture::CID, timeout: 5)
  source = File.join(dir,'source')
  original = File.binread(source)
  backup = UsbFixture.backup(dir)
  options = {role: 'root', path: source, bytes: 1024, sha256: Digest::SHA256.hexdigest(original),
    backup: backup, protected_env: UsbFixture.env}
  UsbFixture.state(dir)
  result = client.write_verified(**options)
  written = File.open(File.join(dir,'part17'),'rb') { |file| file.read(1024) }
  tail = File.open(File.join(dir,'part17'),'rb') { |file| file.seek(2048); file.read(14) }
  abort 'FAIL: verified range write' unless written == original && tail == 'TAIL_PRESERVED' &&
    result[:installation_ready] == false && result[:written_bytes] == 1024 &&
    File.open(File.join(dir,'part2'),'rb') { |file| file.read(131072) } == UsbFixture.env
  checks += 1
  reject = lambda do |updates, reason, opts=options, attempted=false|
    UsbFixture.state(dir,updates)
    before = File.open(File.join(dir,'part17'),'rb') { |file| file.read(4096) }
    begin
      client.write_verified(**opts)
      abort "FAIL: accepted #{reason}"
    rescue A133Usb::Invalid => error
      abort "FAIL: #{reason}: #{error.message}" unless error.message == reason && !!error.write_attempted == attempted
    end
    after = File.open(File.join(dir,'part17'),'rb') { |file| file.read(4096) }
    abort 'FAIL: rejected gate wrote bytes' unless attempted || before == after
    abort 'FAIL: protection changed' unless File.open(File.join(dir,'part2'),'rb') { |file| file.read(131072) } == UsbFixture.env
    checks += 1
  end
  [ [{'cid'=>'f'*32},'usb_identity_changed'], [{'network'=>true},'usb_device_not_ready'],
    [{'duplicate'=>true},'usb_device_not_ready'], [{'state'=>'device'},'usb_device_not_ready'],
    [{'features'=>''},'usb_shell_v2_required'], [{'root'=>'2000'},'usb_root_required'],
    [{'locked'=>'1'},'usb_recovery_unlock_required'], [{'verified'=>'green'},'usb_recovery_unlock_required'],
    [{'model'=>'OTHER'},'usb_board_mismatch'], [{'compatible'=>'allwinner,a64'},'usb_board_mismatch'],
    [{'numbers_bad'=>true},'usb_partition_layout_mismatch'], [{'mapping_bad'=>true},'usb_partition_mapping_mismatch'],
    [{'mounts'=>"/dev/block/by-name/UDISK /mnt ext4 rw 0 0\n"},'usb_target_mounted'],
    [{'mounts'=>"/dev/block/dm-0 /system ext4 ro 0 0\n"},'usb_target_mounted'],
    [{'mountinfo'=>"42 1 179:17 / /mnt rw - ext4 /dev/custom-alias rw\n"},'usb_target_mounted'],
    [{'mountinfo'=>''},'usb_mount_inventory_invalid'],
    [{'mountinfo'=>"42 1 253:0 / /mnt rw - ext4 /dev/custom-alias rw\n",
      'mounts'=>"/dev/custom-alias /mnt ext4 rw 0 0\n", 'block_ids'=>['253:0']},'usb_target_mounted'],
    [{'mounts'=>"tmpfs /data tmpfs rw 0 0\n"},'usb_target_mounted'] ].each { |updates,reason| reject.call(updates,reason) }
  reject.call({},'invalid_backup_receipt',options.merge(backup: backup.merge('cid'=>'f'*32)))
  reject.call({},'invalid_backup_receipt',options.merge(backup: backup.merge('gpt_arrays_crc'=>false)))
  reject.call({},'invalid_protected_environment',options.merge(protected_env: "\0"*131072))
  unsafe = A133Env.patch(UsbFixture.env, {'boot_normal'=>'run boot_android'})
  reject.call({},'invalid_protected_environment',options.merge(protected_env: unsafe))
  reject.call({},'usb_environment_changed',options.merge(protected_env: A133Env.patch(UsbFixture.env, {'extra'=>'different'})))
  reject.call({},'usb_recovery_changed',options.merge(backup: backup.merge('partition_sha256'=>backup['partition_sha256'].merge('recovery'=>'0'*64))))
  reject.call({},'invalid_write_role',options.merge(role: 'env'))
  reject.call({},'invalid_write_size',options.merge(bytes: 0))
  reject.call({},'invalid_write_size',options.merge(role: 'boot'))
  reject.call({},'image_sha256_mismatch',options.merge(sha256: '0'*64))
  reject.call({},'image_size_mismatch',options.merge(bytes: 512))
  File.symlink(source, File.join(dir,'link'))
  reject.call({},'image_not_regular',options.merge(path: File.join(dir,'link')))
  system('mkfifo',File.join(dir,'fifo')) || abort('FAIL: FIFO fixture')
  reject.call({},'image_not_regular',options.merge(path: File.join(dir,'fifo')))
  reject.call({'mode'=>'sync_failed'},'usb_command_failed',options,true)
  reject.call({'mode'=>'corrupt'},'usb_readback_mismatch',options,true)
  reject.call({'mode'=>'short_read'},'usb_readback_size_mismatch',options,true)
  reject.call({'mode'=>'source_changed'},'image_changed',options,true)
  reject.call({'mode'=>'partial'},'usb_command_failed',options,true)
  UsbFixture.state(dir)
  out,err,status = Open3.capture3(RbConfig.ruby,File.join(__dir__,'a133-usb-transfer.rb'),
    '--adb',File.join(dir,'adb'),'--serial','USB_SAMPLE','--cid',UsbFixture::CID)
  parsed = JSON.parse(out)
  abort 'FAIL: read-only CLI' unless status.success? && err.empty? &&
    parsed['status'] == 'recovery_inspection_complete' && parsed['writes_performed'] == 0 &&
    parsed['installation_ready'] == false && !out.include?(dir) && !out.include?(UsbFixture::CID)
  checks += 1
  primary = File.open(File.join(dir,'disk'),'rb') { |file| file.seek(512); file.read(512) }
  File.open(File.join(dir,'disk'),'r+b') { |file| file.seek(512+16); file.write("\0\0\0\0") }
  reject.call({},'gpt_header_crc_mismatch')
  File.open(File.join(dir,'disk'),'r+b') { |file| file.seek(512); file.write(primary) }
  File.open(File.join(dir,'disk'),'r+b') do |file|
    file.seek(60620799*512); header = file.read(512)
    header[56,16] = 'Z'*16
    header[16,4] = "\0"*4
    header[16,4] = [Zlib.crc32(header.byteslice(0,92))].pack('V')
    file.seek(60620799*512); file.write(header)
  end
  reject.call({},'gpt_copies_differ')
  # The CLI cannot accept a write invocation, even with an otherwise valid fixture.
  out,err,status = Open3.capture3(RbConfig.ruby,File.join(__dir__,'a133-usb-transfer.rb'),'--write')
  abort 'FAIL: write CLI available' unless !status.success? && err.empty? &&
    JSON.parse(out)['writes_performed'] == 0 && !out.include?(dir)
  checks += 1
end
puts "USB guarded transfer: #{checks} file-backed policy/write cases passed"
