# Device preparation

The steps below prepare a Google Pixel 7 (panther) for starting Genode
images. They are needed once per device.

**Unlocking the boot loader erases all user data of the phone.** Back up
whatever you want to keep before you start.

The commands use `adb` and `fastboot` of the Android platform tools on the
host.

## Unlocking the boot loader

1. On the phone, open *Settings → About phone* and tap *Build number* seven
   times. This enables the developer options.

2. In *Settings → System → Developer options*, enable *OEM unlocking* and
   *USB debugging*. *OEM unlocking* needs an internet connection the first
   time, and it is not available on carrier-locked devices.

3. Connect the phone via USB and reboot it into the boot loader:

   ```
   adb reboot bootloader
   ```

   Alternatively, switch the phone off and hold *volume down* while switching
   it on.

4. Unlock the boot loader and confirm on the phone with the volume and power
   buttons:

   ```
   fastboot flashing unlock
   ```

   The phone erases its user data and reboots.

## Serial console

The debug UART of the SoC is routed to the SBU pins of the USB-C connector.
It can be accessed with a USB-Cereal adapter, an open-hardware debug board by
Google that is plugged in between the phone and the host:

<https://github.com/google/usb-cereal>

The UART output is switched off by default. With the phone in the boot
loader, switch it on:

```
fastboot oem uart enable
```

The setting can be checked with `fastboot oem uart status`, and the baud
rate is set with `fastboot oem uart config <baud>`. Genode uses the UART as
the boot loader configured it, the examples in this repository assume
115200 baud.

On the host, the adapter appears as a USB serial device, e.g.:

```
picocom -b 115200 /dev/ttyUSB0
```

## Boot-loader watchdog

The boot loader arms a watchdog before it starts the kernel. Switch it off,
the boot loader keeps the setting across boots:

```
fastboot oem watchdog disable
```

Genode's bootstrap code additionally stops the watchdog timers of the SoC
when it starts.

## Next step

Building an image and starting it via `fastboot boot` is described in the
[README](../README) of this repository.
