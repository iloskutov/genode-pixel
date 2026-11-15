#
# The phone's modem stack, shared by the modem scenarios
#
# The phone's own modem daemons run under 'modem_orchestrator', as Android's
# init runs them by their .rc files: cbd boots the CP, rild serves the radio
# interfaces, rfsd the CP's file requests, shared_modem_platform configures
# the CP. All of them use the device nodes of 'pixel_modem'. 'rild_modem'
# talks to rild's radio interfaces and offers the modem as config ROM
# ("rild_modem -> config") and state report ("rild_modem -> state") of
# 'report_rom'.
#
# The files of the phone are not part of Genode. 'tool/pull_modem_files'
# copies them from the phone running Android into
# 'contrib/pixel_modem_vendor.tar' (the daemons and their libraries) and
# 'contrib/pixel_modem_device.tar' (NV data, CP signature, IMEIs). Nothing
# is written to the phone's storage: the daemons' /data and the NV files
# they write live in RAM.
#
# Needs 'modem_pixel.inc' for the drivers below the stack.
#

proc modem_stack_targets { } {
	return {
		server/report_rom
		driver/platform/gs201
		driver/pin/gs201
		driver/ufs/exynos
		server/part_block
		server/vfs
		lib/vfs lib/vfs_ext4 lib/vfs_import lib/vfs_jitterentropy
		lib/libc lib/libm lib/posix lib/stdcxx lib/sandbox
		driver/modem/pixel
	}
}


#
# The components of 'goa-packages', from the depot of this tree at the
# versions below (README of 'goa-packages')
#
proc modem_stack_import { } {
	import_from_depot iloskutov_pixel7/src/android_loader/2026-11-14 \
	                  iloskutov_pixel7/src/android_prop/2026-11-07 \
	                  iloskutov_pixel7/src/android_binder/2026-11-07 \
	                  iloskutov_pixel7/src/modem_orchestrator/2026-11-07 \
	                  iloskutov_pixel7/src/rild_modem/2026-11-07 \
	                  iloskutov_pixel7/src/modem_console/2026-11-07
}


#
# Files of the phone: the device data as ROM modules, the daemons and their
# libraries as tar archive 'android.tar', the orchestrator's rules
#
# Returns the version directory of modem.bin on modem_a.
#
proc modem_stack_files { } {

	set vendor_tar [genode_dir]/contrib/pixel_modem_vendor.tar
	set device_tar [genode_dir]/contrib/pixel_modem_device.tar

	foreach f [list $vendor_tar $device_tar] {
		if {![file isfile $f]} {
			puts stderr "Error: $f missing, see 'tool/pull_modem_files'"
			exit 1
		}
	}

	set g [run_dir]/genode

	exec rm -rf [run_dir]/device [run_dir]/android_root
	exec mkdir -p [run_dir]/device [run_dir]/android_root
	exec tar xf $device_tar -C [run_dir]/device
	exec tar xf $vendor_tar -C [run_dir]/android_root

	foreach f { efs/nv_normal.bin efs/nv_protected.bin persist/cpsha
	            modem_userdata/replay/dds.bin devicetree/rfid devicetree/imei1
	            devicetree/imei2 cmdline } {
		exec cp [run_dir]/device/$f $g/[file tail $f] }

	# rfsd checks the NV files against their salted checksums
	foreach f {nv_normal.bin nv_protected.bin} {
		set md5 [lindex [exec sh -c "{ cat $g/$f; printf Samsung_SIT_RIL; } | md5sum"] 0]
		set fd [open $g/$f.md5 w]
		puts -nonewline $fd $md5
		close $fd
	}

	#
	# Property defaults: the phone's properties without those the daemons and
	# init publish at run time, a cold boot has none of them
	#
	set runtime_re {^\[(vendor\.cbd\.|vendor\.modem\.|vendor\.ril\.|vendor\.radio\.|gsm\.|init\.svc\.|init\.svc_debug_pid\.)}
	set kept [list]
	foreach l [split [exec cat [getprop_file]] "\n"] {
		if {![regexp $runtime_re $l]} { lappend kept $l } }
	set fd [open $g/getprop.txt w]
	puts $fd [join $kept "\n"]
	close $fd

	exec tar cf $g/android.tar -C [run_dir]/android_root .

	# the orchestrator's rules, from the modem's .rc files of the phone
	set init [run_dir]/android_root/vendor/etc/init
	exec [repository_contains tool/rc_to_rules]/tool/rc_to_rules -o $g/rules \
	     $init/cbd.rc $init/rild_exynos.rc $init/rfsd.rc $init/init.modem.rc \
	     $init/init.shared_modem_platform_l.rc

	# rild_modem's settings: rild's liveness as the orchestrator reports it
	set fd [open $g/rild_modem_settings w]
	puts $fd "settings\n+ rild | liveness_property: init.svc.ril-daemon\n-"
	close $fd

	return [string trim [exec cat [run_dir]/device/VERSION]]
}


#
# The nodes each daemon may open: cbd boots the CP through umts_boot0, rfsd
# serves its files, the others talk to it. umts_boot0 also tells the CP's
# state, which all of them ask. Each line gets the prefix 'indent'.
#
proc modem_chardev_policies { indent } {
	set policies {+ policy | label_prefix: modem_orchestrator -> cpboot-daemon
  + node | prefix: umts_boot
+ policy | label_prefix: modem_orchestrator -> RFS-daemon
  + node | name: umts_rfs0
  + node | name: umts_boot0
+ policy | label_prefix: modem_orchestrator -> ril-daemon
  + node | prefix: umts_
  + node | prefix: oem_
+ policy | label_prefix: modem_orchestrator -> init.shared_modem_platform
  + node | prefix: umts_
  + node | prefix: oem_}
	set out ""
	foreach line [split $policies "\n"] { append out "$indent$line\n" }
	return $out
}


#
# The '<start>' nodes of the stack (HID), from part_block to rild_modem
#
# 'pixel_modem_config' is the HID of pixel_modem's config, if any, and its
# '+ route' line with the routes beyond those of the drivers, indented as
# sub nodes of the '<start>' node, without a blank line (it would end the
# node) and without a trailing newline.
#
proc modem_stack_start_nodes { fw_version ril_log_level pixel_modem_config } {
	return [string cat {
+ start part_block | ram: 10M
  + provides | + service Block
  + config
    + report | partitions: yes
    + policy | label_prefix: vfs_fw | partition: 19 | writeable: no
      . modem_a
  + route
    + service Block  | + child exynos_ufs
    + service Report | + child report_rom
    + any-service    | + parent

+ start vfs_fw | caps: 400 | ram: 40M
  . the version directory of modem_a that 'images/default' links to, mounted
  . at that path by the daemons
  + binary vfs
  + provides | + service File_system
  + config
    + vfs | + ext4
    + policy | label_suffix: modem_img -> / | root: /images/} $fw_version {/ | writeable: no
  + route
    + service Block | + child part_block
    + any-service   | + parent

+ start pixel_modem | caps: 1000 | ram: 128M | managing_system: yes
  . on a CPU of its own: with the daemons on the same CPU, rild's first
  . requests timed out while shared_modem_platform configured the CP
  + affinity | xpos: 1 | width: 1 | height: 1
  + provides | + service Android_chardev
} $pixel_modem_config {
    + service ROM | label: dtb | + parent | label: pixel_modem-google_pixel7.dtb
    + service Platform    | + child platform
    + service Dma         | + child platform
    + service Pin_state   | + child gs201_pio
    + service Pin_control | + child gs201_pio
    + service IRQ         | + child gs201_pio
    + service Timer       | + child timer
    + any-service         | + parent

+ start vfs | ram: 16M
  . /data of the daemons, in RAM. /data/vendor/radio exists on Android
  . before the modem's .rc files run.
  + provides | + service File_system
  + config
    + vfs
      + ram
      + import | + dir vendor | + dir radio | + inline .keep | : keep
    + policy | label_prefix: modem_orchestrator | root: / | writeable: yes
    + default-policy | root: / | writeable: no
  + route | + any-service | + parent

+ start efs | caps: 150 | ram: 8M
  . rfsd's /mnt/vendor/efs, in RAM, with the phone's NV files
  + binary vfs
  + provides | + service File_system
  + config
    + vfs
      + ram
      + import
        + rom nv_normal.bin
        + rom nv_normal.bin.md5    | binary: no
        + rom nv_protected.bin
        + rom nv_protected.bin.md5 | binary: no
    + default-policy | root: / | writeable: yes
  + route | + any-service | + parent

+ start android_prop | caps: 200 | ram: 8M
  + provides | + service Android_property
  + config | permissive: no
    + defaults | file: /etc/getprop.txt
    . cbd reads modem_a; rfsd starts with cbd and reads modem.bin's path
    . before cbd publishes it
    + property | name: ro.boot.slot_suffix       | value: _a
    + property | name: vendor.modem.bin.path     | value: /mnt/vendor/modem_img/images/default//modem.bin
    + property | name: vendor.cbd.modem_bin_type | value: 2
    + persist | file: /data/property/persistent
    + policy | label: modem_orchestrator -> properties | watch: yes
      + allow | prefix:
    + policy | label_prefix: modem_orchestrator -> cpboot-daemon ->
      + allow | prefix: vendor.cbd.
      + allow | prefix: vendor.modem.
      + allow | name: vendor.sys.rild_reset
    + policy | label_prefix: modem_orchestrator -> ril-daemon ->
      + allow | prefix: vendor.ril.
      + allow | prefix: persist.vendor.radio.
      + allow | prefix: persist.radio.uim.remote.
      + allow | name: gsm.version.ril-impl
    + policy | label_prefix: modem_orchestrator -> RFS-daemon ->
      + allow | name: vendor.ril.cbd.rfs_check_done
      + allow | name: persist.vendor.modem.efs.clear.action
      + allow | name: persist.vendor.modem.baseband.version
    + policy | label_prefix: modem_orchestrator -> init.shared_modem_platform ->
      + allow | name: persist.vendor.modem.experiment.volte_mif_off
    + policy | label: rild_modem -> properties | watch: yes
    + vfs
      + dir etc  | + rom getprop.txt
      + dir data | + ram
      + dir dev  | + log
    + libc | stdout: /dev/log | stderr: /dev/log
  + route
    + service Timer | + child timer
    + any-service   | + parent

+ start modem_orchestrator | caps: 3700 | ram: 1100M
  + provides | + service Android_binder
  + config
    . Android's init: 'class_start core' (on boot), then 'class_start main'
    + class_start core
    + class_start main
    + parent-provides
      + service ROM
      + service PD
      + service CPU
      + service LOG
      + service RM
      + service Timer
      + service Android_property
      + service Android_chardev
      + service File_system
    . rild's radio interfaces for rild_modem
    + service Android_binder
      + default-policy | + child ril-daemon
    + template | service: cpboot-daemon | caps: 600 | ram: 400M
      + loader | log_level: } $ril_log_level {
        + env | value: ANDROID_ROOT=/system
        + property_service
        + chardev | prefix: /dev/umts_
        + profile | lib: android_profile_cbd.lib.so
        + vfs
          + tar android.tar
          + dir etc | + rom getprop.txt
          + dir mnt
            + dir vendor
              + dir modem_img
                + dir images
                  + dir default | + fs | label: modem_img -> / | buffer_size: 1M | writeable: no
              + dir efs
                + rom nv_normal.bin
                + rom nv_protected.bin
              + dir persist | + dir modem | + rom cpsha
              + dir modem_userdata | + ram
          + dir sys
            + dir firmware
              + dir devicetree
                + dir base
                  + dir chosen
                    + dir plat   | + rom rfid
                    + dir config
                      + rom imei1
                      + rom imei2
          + dir proc | + rom cmdline
          + dir data | + fs
          + dir dev
            + log
            + jitterentropy random
          + import
            + dir mnt
              + dir vendor
                + dir modem_userdata
                  + dir replay | + rom dds.bin
        + libc | stdout: /dev/log | stderr: /dev/log | rng: /dev/random | cwd: /
    + template | service: ril-daemon | caps: 1000 | ram: 200M
      + provides | + service Android_binder
      + loader | object_map: yes | log_level: } $ril_log_level {
        + env | value: ANDROID_ROOT=/system
        + property_service
        + chardev | prefix: /dev/umts_
        + net_interfaces | prefix: rmnet | count: 30
        + export_binder
        + vfs
          + tar android.tar
          + dir etc  | + rom getprop.txt
          + dir data | + fs
          + dir dev
            + log
            + jitterentropy random
            + inline rtc | : 2026-11-07 12:00
          + import
            + dir data
              + dir vendor
                + dir rild  | + inline .keep | : keep
                + dir radio | + inline .keep | : keep
        + libc | stdout: /dev/log | stderr: /dev/log | rtc: /dev/rtc | rng: /dev/random | cwd: /
    + template | service: RFS-daemon | caps: 600 | ram: 200M
      + loader | log_level: } $ril_log_level {
        + env | value: ANDROID_ROOT=/system
        + property_service
        + chardev | prefix: /dev/umts_
        + vfs
          + tar android.tar
          + dir etc | + rom getprop.txt
          + dir mnt
            + dir vendor
              + dir modem_img
                + dir images
                  + dir default | + fs | label: modem_img -> / | buffer_size: 1M | writeable: no
              + dir efs | + fs | label: efs -> /
          + dir dev
            + log
            + jitterentropy random
            + inline rtc | : 2026-11-07 12:00
          + dir data | + fs
          + import
            + dir data | + dir vendor | + dir log | + dir rfsd | + inline .keep | : keep
        + libc | stdout: /dev/log | stderr: /dev/log | rtc: /dev/rtc | rng: /dev/random | cwd: /
    + template | service: init.shared_modem_platform | caps: 600 | ram: 200M
      + loader | log_level: } $ril_log_level {
        + env | value: ANDROID_ROOT=/system
        + property_service
        + chardev | prefix: /dev/umts_
        + chardev | prefix: /dev/oem_ipc
        + vfs
          + tar android.tar
          + dir etc | + rom getprop.txt
          + dir mnt
            + dir vendor
              + dir modem_img
                + dir images
                  + dir default | + fs | label: modem_img -> / | buffer_size: 1M | writeable: no
              + dir modem_userdata | + ram
          + dir data | + fs
          + dir dev
            + log
            + jitterentropy random
            + inline rtc | : 2026-11-07 12:00
          + import
            + dir data | + dir vendor | + dir radio | + inline .keep | : keep
            + dir mnt
              + dir vendor
                + dir modem_userdata
                  + dir replay | + inline .keep | : keep
        + libc | stdout: /dev/log | stderr: /dev/log | rtc: /dev/rtc | rng: /dev/random | cwd: /
    + vfs
      + dir data | + fs
      + dir dev  | + log
    + libc | stdout: /dev/log | stderr: /dev/log
  + route
    + service Android_chardev  | + child pixel_modem
    + service Android_property | + child android_prop
    + service Timer            | + child timer
    + service File_system | label_prefix: RFS-daemon -> efs | + child efs
    + service File_system | label_suffix: modem_img -> /     | + child vfs_fw
    + service File_system | + child vfs
    + any-service         | + parent

+ start rild_modem | caps: 300 | ram: 16M
  + route
    + service Android_binder   | + child modem_orchestrator
    + service ROM | label: config   | + child report_rom
    + service ROM | label: settings | + parent | label: rild_modem_settings
    + service Report           | + child report_rom
    + service Android_property | + child android_prop
    + service Timer            | + child timer
    + any-service              | + parent
}]
}
