---
name: pixel-device-test
description: >
  Build a run scenario of this repository and test it on the Google Pixel 7
  (KERNEL=hw BOARD=google_pixel7): build the image with REBOOT=serial, boot
  it with 'fastboot boot', capture the debug UART, reset the phone over the
  UART, and read the log split per boot. Use it whenever a scenario is to be
  run, tested or checked for regressions on the phone ("run X on the device",
  "test on hardware", "does UFS still work", "capture the log"). Drive the
  phone only through tool/device_test, never with adb, fastboot or the
  serial device by hand.
---

# Pixel 7 device test

All device control goes through `repos/pixel/tool/device_test`, run from the
Genode directory:

```sh
repos/pixel/tool/device_test <scenario> [seconds]   # build, boot, capture, reset
repos/pixel/tool/device_test --capture [seconds] [name]
repos/pixel/tool/device_test --reset
```

`<scenario>` is a run script (`exynos_ufs_pixel` or `run/exynos_ufs_pixel`).
The tool builds it itself with
`make run/<scenario> KERNEL=hw BOARD=google_pixel7 REBOOT=serial` (a failed
build boots nothing), brings the phone into fastboot (from Android via
`adb reboot bootloader`), boots the image with `fastboot boot`, captures for
`[seconds]` (default 30), and then sends `RST` over the UART. It keeps
capturing until the phone is back.

Rules:

- **Nothing is flashed.** Only `fastboot boot`.
- **The only reset is `RST` over the UART**, sent by the tool. No reboot
  component on the phone, no `REBOOT_S`, no watchdog tricks.
- Run one test at a time: the tool refuses while another run (lock in the
  log directory) or another reader of the serial device is active.
- Variables of a run script come from the environment, e.g.
  `GENODE_WIFI_SSID=... GENODE_WIFI_PSK=... repos/pixel/tool/device_test wifi_fetchurl_pixel 90`.
  Ask the user for credentials, never write them into a file.

After `RST` the phone comes back in fastboot: the reset monitor writes the
boot mode to the PMU and to RSBM of the MAX77759 (boot loader:
`Reboot mode: 0xfc - Bootloader`, kernel: `RSBM: response 0x82 ... (ok)`).
If the tool reports "phone came back in Android", the RSBM write failed;
look for the `RSBM:` lines at the end of `serial.01.genode.log`. The
`Reboot reason: 0xcbea` line of the boot loader is a stale value, not a
watchdog reset.

## Logs

Every run has its own directory, `~/tmp/pixel-logs/<scenario>_<date>_<time>`
(`PIXEL_LOG_DIR` to change), and `~/tmp/pixel-logs/latest` points to the
newest:

| File | Content |
|------|---------|
| `run.log` | steps of the tool, revisions of both repos (`-dirty`), image size and sha256, adb/fastboot output |
| `build.log` | make output |
| `serial.log` | raw capture, never edited |
| `serial.NN.boot_loader.log` | boot loader of boot NN |
| `serial.NN.genode.log` | Genode started by boot NN |
| `serial.NN.other_os.log` | another OS of boot NN (Android after the reset) |

`tool/split_serial_log [--running] <capture>.log` does the split; use it on
any capture the user sends.

## Reading the log

These rules exist because logs were misread before.

1. **Read `serial.01.genode.log`, not `serial.log`.** The raw capture mixes
   the boot loader, Genode and the Android boot after the reset. Check the
   summary's `Genode boots:` line: 1 is normal. 0 means the image never
   started (look at `serial.01.boot_loader.log` and `run.log`); more than 1
   means the phone reset during the test, each boot is a separate run.
2. **Check what was tested.** `run.log` names the revisions and the image
   hash. A `-dirty` revision is uncommitted code; say so in the report.
3. **Attribute each line** to its component by the `[init -> <name>]`
   prefix. A success line of one component says nothing about another.
4. **Quote raw lines.** Every claim in the report is backed by a line
   grepped from the file, not from memory or an earlier summary.
5. **Compare inputs before code.** For a regression, compare the same file
   of the last good run (same scenario directory pattern) with the failing
   one: revisions, image, kernel line, RAM line, reported devices. Change
   one thing per test.
6. **A silent end is a finding.** If the log stops, report the last lines
   and which component printed them; do not guess the cause.

## Scenarios

The header comment of a run script says what the scenario does and when it
passed. Beyond that:

- Capture times: 30 s (default) suits most scenarios; give
  `exynos_ufs_pixel` 45 s, `wifi_fetchurl_pixel` and `usb_gadget_pixel`
  90 s.
- `keys_pixel` logs nothing at start; only a key press shows that it works.
- Touch, keys, the panel (`exynos_fb_pixel`, `demo_pixel`) need the user at
  the phone, `usb_gadget_pixel` needs `repos/pixel/tool/usb_echo_check` on
  the PC while the capture runs. Start the tool with a long capture in the
  background, ask the user, then read the log. Do not claim a visual or
  manual result the user did not confirm.
