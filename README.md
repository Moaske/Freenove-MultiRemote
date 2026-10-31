# Freenove MultiRemote

A finger-friendly touch remote built on the **Freenove ESP32 Display 2.8"
(FNK0114B_2P8)**. It controls old audio equipment over IR, using codes
captured with a Flipper Zero, and (next step) the media player on a
Windows 11 PC over Bluetooth.

## Status

| Part | State |
|---|---|
| Screen layout (portrait, 240×320) | done |
| Touch, with on-screen calibration | done |
| IR from Flipper `.ir` files on the SD card | done |
| Battery gauge (bq25185 charger on BAT+) | done |
| Settings: theme, brightness, dim, sleep | done |
| Bluetooth to the PC: media keys | next |
| Track title, artist, album art (tray app on the PC) | next |

## Hardware

- Freenove ESP32 Display 2.8" FNK0114B_2P8
- Adafruit High Power IR LED Emitter (5639)
- Adafruit bq25185 USB/DC/solar charger, a 3.7 V Li-Ion cell and an on/off switch

| From | To |
|---|---|
| Charger output (after the switch) | Freenove BAT+ **and** IR breakout V+ |
| Charger GND | Freenove BAT− **and** IR breakout GND |
| Freenove SPI header **IO27** | IR breakout **IN** |

A 100–470 µF capacitor across the IR breakout's V+ and GND helps with the
LED current pulses. The battery is read on GPIO34 through the board's
own 100K/100K divider.

## SD card

Put the Flipper Zero `.ir` files in a folder called `remote`:

```
/remote/
   amp.ir   -> "Amplifier" tab, and the volume row
   cd.ir    -> "CD" tab
   md.ir    -> "MiniDisc" tab
```

The files used for this build are in [`sd-card/remote`](sd-card/remote).
After changing the card, use **Settings → Load .ir files**.

### How a file becomes buttons

1. Only the **first 8 signals** of a file count. Reorder the file to choose.
2. In `amp.ir`, `Vol_up`, `Vol_down` and `Mute` go to the volume row.
3. If both `Play` and `Pause` are there, they become **one toggle button**
   where `Play` is. A long press flips its icon without sending anything,
   to get it back in step with the device.
4. What is left fills the 6 panel buttons in file order.

Known audio functions get an icon (case and `_` ignored):

| Icon | Names |
|---|---|
| Power | `Power`, `On_off`, `Standby` |
| Play / Pause | `Play`, `Pause`, `Play_pause` |
| Stop | `Stop` |
| Previous / Next | `Prev`, `Previous`, `Next` |
| Rewind / Fast forward | `Rew`, `FF` |
| Eject | `Eject`, `Open`, `Open_close` |
| Record | `Rec`, `Record` |

Any other name is a source select and shows as text: up to 3 letters in
capitals (`cd` → CD), longer with a capital first letter (`video` → Video).

Supported protocols: NEC, NECext, Samsung32, SIRC, SIRC15, SIRC20, RC5,
RC6 and raw. Sony codes are sent three times, as Sony devices require.

## Building and flashing

GitHub Actions builds the firmware on every push. Open the latest run under
**Actions**, download **freenove-multiremote-firmware** from Artifacts, and
flash it with the browser tool at <https://espressif.github.io/esptool-js/>:

- **First install:** `merged-full-install.bin` at offset `0x0`
- **Updates:** `firmware.bin` at offset `0x10000` (keeps your settings)

On first start the screen asks you to tap three crosses to calibrate touch.
Hold **BOOT** while powering on to calibrate again.

## Power saving

- CPU at 80 MHz, Wi-Fi never used, the on-board speaker amp switched off
- Screen dims after 5/15/30 s and sleeps after 1/2/5 min (Settings)
- Sleep wakes on a touch or the BOOT button
- Below 3.38 V on the cell it shows a message and powers down cleanly

## Credits

Board pins and the battery curve come from the `freenove28` build of
[Moaske/SquachWatch-CYD-BATT](https://github.com/Moaske/SquachWatch-CYD-BATT).
