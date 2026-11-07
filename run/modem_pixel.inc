#
# The drivers the modem scenarios share: the platform driver with the
# devices of the modem, the pins of the CP, and the UFS of modem_a
#
# Returns the '<start>' nodes (HID) for 'install_config'. The scenario adds
# part_block, the VFS of modem_a, pixel_modem, and its clients.
#

#
# The phone's properties: 'tool/pull_modem_files' puts them into the device
# archive (they name the phone); an archive of an earlier version of the
# tool has them in the vendor archive
#
proc getprop_file { } {
	foreach f [list [run_dir]/device/getprop.txt [run_dir]/android_root/getprop.txt] {
		if {[file isfile $f]} { return $f } }
	puts stderr "Error: no getprop.txt in the archives, see 'tool/pull_modem_files'"
	exit 1
}


proc modem_pixel_drivers { } {
	return {
+ start platform | caps: 150 | ram: 2M | managing_system: yes
  + binary gs201_platform
  + provides
    + service Platform
    + service Dma
  + config
    + policy | label_prefix: exynos_ufs | info: yes
      + device ufs
    + policy | label_prefix: gs201_pio | info: yes
      + device gpio_alive
      + device gpio_far_alive
      + device gpio_hsi1
    + policy | label_prefix: pixel_modem | info: yes
      + device pcie0
      + device sysmmu_hsi1
      + device cp_shmem
  + route
    + service Timer | + child timer
    + any-service   | + parent

+ start gs201_pio | caps: 150 | ram: 2M
  + provides
    + service Pin_state
    + service Pin_control
    + service IRQ
  + config
    + out    pcie0_perst     | bank: gph0 | index: 0 | default: no
    + select pcie0_clkreq    | bank: gph0 | index: 1 | function: 2 | pull: up
    + out    ap2cp_wakeup    | bank: gph0 | index: 2 | default: no
    + out    ap2cp_dump_noti | bank: gph0 | index: 3 | default: no
    + out    ap2cp_ap_active | bank: gph1 | index: 1 | default: no
    + out    ap2cp_cp_pwr    | bank: gph1 | index: 2 | default: no
    + out    ap2cp_nreset    | bank: gph1 | index: 3 | default: no
    + out    ap2cp_cp_wrst_n | bank: gph1 | index: 6 | default: no
    + out    ap2cp_pm_wrst_n | bank: gpa0 | index: 3 | default: no
    + in     cp2ap_cp_wrst_n | bank: gpa0 | index: 2 | pull: down | irq: edges
    + in     cp2ap_ps_hold   | bank: gpa8 | index: 2 | pull: down
    + in     cp2ap_wakeup    | bank: gpa8 | index: 3 | pull: down | irq: edges
    + in     cp2ap_active    | bank: gpa8 | index: 4 | pull: down | irq: edges
    + policy | label: pixel_modem -> gph0-0 | pin: pcie0_perst
    + policy | label: pixel_modem -> gph0-2 | pin: ap2cp_wakeup
    + policy | label: pixel_modem -> gph0-3 | pin: ap2cp_dump_noti
    + policy | label: pixel_modem -> gph1-1 | pin: ap2cp_ap_active
    + policy | label: pixel_modem -> gph1-2 | pin: ap2cp_cp_pwr
    + policy | label: pixel_modem -> gph1-3 | pin: ap2cp_nreset
    + policy | label: pixel_modem -> gph1-6 | pin: ap2cp_cp_wrst_n
    + policy | label: pixel_modem -> gpa0-3 | pin: ap2cp_pm_wrst_n
    + policy | label: pixel_modem -> gpa0-2 | pin: cp2ap_cp_wrst_n
    + policy | label: pixel_modem -> gpa8-2 | pin: cp2ap_ps_hold
    + policy | label: pixel_modem -> gpa8-3 | pin: cp2ap_wakeup
    + policy | label: pixel_modem -> gpa8-4 | pin: cp2ap_active
    + policy | label: pixel_modem -> 34     | pin: cp2ap_cp_wrst_n
    + policy | label: pixel_modem -> 91     | pin: cp2ap_wakeup
    + policy | label: pixel_modem -> 92     | pin: cp2ap_active
      . an interrupt session of a pin is labeled by its GIC interrupt
  + route
    + service Platform | + child platform
    + any-service      | + parent

+ start exynos_ufs | caps: 500 | ram: 128M
  + provides | + service Block
  + config
    + default-policy | device: sda | writeable: no
  + route
    + service ROM | label: dtb | + parent | label: exynos_ufs-google_pixel7.dtb
    + service Platform | + child platform
    + service Dma      | + child platform
    + service Timer    | + child timer
    + any-service      | + parent
}
}
