# Meshtastic Victron firmware

The external PlatformIO overlay supplies two custom targets:

| Target | Hardware | BLE scanner |
| --- | --- | --- |
| `tlora-v1-victron` | LILYGO/TTGO T-LoRa V1 | ESP32 raw NimBLE GAP observer |
| `t-echo-victron` | Original LILYGO T-Echo | nRF52840 Bluefruit observer |

Both listen directly for encrypted Victron Instant Readout advertisements and
send the decoded battery values over a normal Meshtastic sub-GHz LoRa mesh.
They do not use ESPHome, UART, or a second microcontroller. Passive scanning
shares the BLE stack with Meshtastic's normal phone service. On T-Echo, the
module pauses scanning for an active phone connection and resumes within about
one second after disconnecting; this avoids starving nRF52 connection events.

All Victron code lives in `meshtastic/victron`, outside the firmware submodule.
The overlay's pre-script injects that code into Meshtastic as a project object
and supplies a strong `lateInitVariant()` registration hook. The pinned
Meshtastic fork has only a generic switch that suppresses construction of its
stock `DeviceTelemetryModule`; this small fork remains necessary because the
internal module API is not a stable plugin ABI.

## Telemetry sent to Meshtastic

The custom module replaces Meshtastic's normal local device-battery telemetry.
The node battery icon therefore represents Victron SOC and voltage. The TTGO
or T-Echo's own battery telemetry is intentionally not sent, including when a
T-Echo battery is installed.

| Meshtastic payload | Source | Values |
| --- | --- | --- |
| `DeviceMetrics` | SmartShunt | State of charge and battery voltage |
| `PowerMetrics` channel 1 | SmartShunt | Battery voltage and current |
| `PowerMetrics` channel 2 | SmartSolar, when configured | Battery voltage and charge current |
| `TEXT_MESSAGE_APP` | SmartShunt | Broadcast low-SOC warning |

Meshtastic power telemetry represents current in milliamps. The firmware
converts the SmartShunt and SmartSolar amp readings before placing them in the
native payload.

The default packet behavior is:

- Send device and power telemetry every 10 minutes.
- Send after SOC changes by 1 percentage point, voltage changes by 100 mV, or
  current changes by 1 A.
- Limit change-triggered updates to one per minute.
- Stop transmitting telemetry when the latest SmartShunt advertisement is
  more than two minutes old.
- Send one low-SOC text message at or below 25%.
- Re-arm the low-SOC alert only after SOC recovers to at least 30%.

The text warning is broadcast on the primary Meshtastic channel. A phone or
another Meshtastic client subscribed to that channel will receive it like a
normal group message.

## Prerequisites

You need:

- An original LILYGO T-Echo or a LILYGO/TTGO T-LoRa V1 with the correct antenna
  for the intended sub-GHz band. T-Echo Lite, Plus, and Card are not these
  targets.
- A USB data cable. Continuous USB power is suitable when the node has no local
  battery.
- A Victron SmartShunt with Instant Readout enabled.
- Optionally, a Victron SmartSolar with Instant Readout enabled.
- The Bluetooth MAC address and 16-byte advertisement key for each configured
  Victron device.
- Nix with flakes enabled.

Attach a suitable antenna before powering the LoRa radio. For operation in the
United States, use legal 902–928 MHz hardware and select the United States
region in Meshtastic after flashing. The runtime region setting controls the
radio plan; the encryption keys in this guide are Victron BLE keys and do not
configure Meshtastic channel encryption.

## Obtain the Victron identities

For each Victron device in VictronConnect:

1. Open the device settings.
2. Open **Product info** from the three-dot menu.
3. Enable **Instant readout via Bluetooth**.
4. Open **Instant readout details**, select **SHOW**, and record the Bluetooth
   MAC address and advertisement encryption key.

The bridge is read-only. It passively receives these advertisements and never
connects to or changes the Victron devices.

## Configure the build

From the repository root, copy the environment template if `.env` does not
already exist:

```sh
cp .env.example .env
```

Set at least the SmartShunt identity:

```dotenv
VICTRON_SHUNT_MAC=AA:BB:CC:DD:EE:01
VICTRON_SHUNT_KEY=00000000000000000000000000000000
```

To include SmartSolar telemetry, also set both solar values:

```dotenv
VICTRON_SOLAR_MAC=AA:BB:CC:DD:EE:02
VICTRON_SOLAR_KEY=00000000000000000000000000000000
```

The solar MAC and key are optional as a pair. Supplying only one is a build
error. MAC addresses are case-insensitive and keys must contain exactly 32
hexadecimal characters.

The behavior can be adjusted with these optional settings:

| Variable | Default | Purpose |
| --- | ---: | --- |
| `VICTRON_TELEMETRY_INTERVAL_SECONDS` | `600` | Periodic mesh update interval; allowed range 300–3600 seconds |
| `VICTRON_MIN_SEND_INTERVAL_SECONDS` | `60` | Minimum interval between change-triggered updates |
| `VICTRON_STALE_AFTER_SECONDS` | `120` | Maximum SmartShunt reading age allowed for transmission |
| `VICTRON_SOC_DELTA_PERCENT` | `1` | SOC change that triggers an update |
| `VICTRON_VOLTAGE_DELTA_MV` | `100` | Voltage change that triggers an update |
| `VICTRON_CURRENT_DELTA_MA` | `1000` | Current change that triggers an update |
| `VICTRON_LOW_SOC_PERCENT` | `25` | SOC threshold that sends the warning |
| `VICTRON_LOW_SOC_REARM_PERCENT` | `30` | SOC recovery required before another warning |

`.env` is ignored by Git. During the build, the target validates these values
and creates an ignored header under the selected target's `.pio/build`
directory.
The build log does not print the keys. The resulting firmware does contain the
keys, so treat firmware binaries as secrets and do not publish them.

## Build the firmware

Run either build from the repository root. `--project-conf` is required; it is
what activates the external overlay instead of the submodule's stock target
list.

```sh
nix develop --command uv run pio run \
  --project-dir meshtastic/firmware \
  --project-conf "$PWD/meshtastic/platformio-victron.ini" \
  -e tlora-v1-victron

nix develop --command uv run pio run \
  --project-dir meshtastic/firmware \
  --project-conf "$PWD/meshtastic/platformio-victron.ini" \
  -e t-echo-victron
```

The project pins PlatformIO 6.1.19 in its Python lockfile. The first build can
take several minutes while PlatformIO downloads the pinned board toolchains
and compiles Meshtastic.

Successful output is written under:

```text
meshtastic/firmware/.pio/build/tlora-v1-victron/
├── firmware-tlora-v1-victron-<version>.bin
└── firmware-tlora-v1-victron-<version>.factory.bin

meshtastic/firmware/.pio/build/t-echo-victron/
├── firmware-t-echo-victron-<version>.uf2
└── firmware-t-echo-victron-<version>-ota.zip
```

Use the factory image for an initial full serial flash. The regular `.bin` is
the application image used by update workflows that expect an application
binary.

## Flash the T-LoRa V1 over USB

Connect the TTGO V1 with a USB data cable, then let PlatformIO locate and flash
the serial port:

```sh
nix develop --command uv run pio run \
  --project-dir meshtastic/firmware \
  --project-conf "$PWD/meshtastic/platformio-victron.ini" \
  -e tlora-v1-victron \
  -t upload
```

If more than one serial device is connected, specify the port:

```sh
nix develop --command uv run pio run \
  --project-dir meshtastic/firmware \
  --project-conf "$PWD/meshtastic/platformio-victron.ini" \
  -e tlora-v1-victron \
  -t upload \
  --upload-port /dev/cu.usbserial-0001
```

The exact device name varies by operating system and USB serial adapter. The
upload command asks esptool to reset into the serial bootloader automatically.
If that does not work, hold BOOT, tap RESET, start the upload, and release BOOT
when esptool begins connecting.

## Flash the original T-Echo

The simplest initial flash uses its UF2 bootloader:

1. Connect the T-Echo over USB.
2. Double-press RESET to mount the bootloader volume.
3. Copy `firmware-t-echo-victron-<version>.uf2` from the build directory to
   that volume.
4. Wait for the volume to eject and the node to reboot.

PlatformIO can also upload over a detected serial bootloader:

```sh
nix develop --command uv run pio run \
  --project-dir meshtastic/firmware \
  --project-conf "$PWD/meshtastic/platformio-victron.ini" \
  -e t-echo-victron \
  -t upload
```

The generated `-ota.zip` is for a supported nRF52 OTA update workflow after an
initial firmware installation. Do not copy that ZIP to the UF2 drive.

## Configure Meshtastic

After flashing, connect with the Meshtastic mobile or web client over
Bluetooth and configure the node normally:

1. Set the LoRa region to **United States** when using legal US-band hardware.
   Do not transmit until the correct region is selected.
2. Configure or join the desired Meshtastic channel. Channel encryption and
   the Victron advertisement keys are independent.
3. Keep Meshtastic Bluetooth enabled. The firmware reuses the running NimBLE
   or Bluefruit stack for passive scanning.
4. Set the device role to **Sensor** if this node is dedicated to telemetry.
   The custom module marks its telemetry packets reliable for the Sensor role;
   other roles use background priority.
5. Give the node a recognizable long and short name, such as `Victron Van` and
   `VAN`.

The board does not need its own battery and may remain powered over USB. The
battery percentage and voltage shown for this node intentionally represent
the SmartShunt battery, not the board supply or a T-Echo internal battery.

## Confirm operation

With the node near the Victron devices, reboot it and watch its 115200-baud
serial log if needed (replace the example device path for your system):

```sh
nix develop --command uv run pio device monitor \
  --baud 115200 \
  --port /dev/cu.usbserial-0001
```

Expected log messages include:

```text
Victron BLE passive scan started
Victron BLE valid SmartShunt packet counter=...
Victron telemetry sent: SOC=... voltage=... current=...
```

The packet line is emitted at debug log level. The scanner filters discovery
events by the configured Victron MAC addresses before parsing or allocating
advertiser objects, so unrelated BLE devices are intentionally silent. Seeing
Unknown records, malformed packets, duplicates, and failed decryptions are
also silent. Seeing continuous `Updated advertiser` messages or
`BLEAdvertisedDevice` allocation errors indicates that an older build using
Arduino's high-level scanner is still installed.

On T-Echo, a debug build also emits `Victron BLE scan paused for phone
connection` after a phone connects. The app can read the most recently
published Victron telemetry during that session. Fresh BLE reception resumes
after the app disconnects.

On another Meshtastic client, open the node's telemetry details. SOC is sent as
the node's native battery level, not as a power channel. It therefore appears
in the node battery indicator and, where the client provides it, the Device
Metrics log. Channel 1 only contains SmartShunt voltage and current. SmartSolar
values appear only when both solar build settings are present and a fresh
solar advertisement has been received.

Test the warning behavior with a safe threshold above the current SOC rather
than deliberately discharging a battery. Temporarily change
`VICTRON_LOW_SOC_PERCENT`, keep the re-arm threshold higher, rebuild, and
confirm that exactly one text message is received. Restore the intended
thresholds before deployment.

## Troubleshooting

### The build says a Victron setting is missing or invalid

Run the build from the repository root so the target can find the root `.env`.
Check for a six-byte colon-separated MAC address and a 32-character hexadecimal
key. SmartSolar must have both its MAC and key or neither.

### The node works in Meshtastic but has no Victron telemetry

- Confirm Instant Readout is enabled in VictronConnect.
- Confirm the MAC and key belong to the same Victron device.
- Keep the node within BLE range and move it away from metal enclosures.
- Keep Meshtastic Bluetooth enabled and reboot after changing that setting.
- Check the serial log for `Victron BLE passive scan started`.

### SOC briefly appears and then stops updating

The firmware deliberately suppresses transmission when the SmartShunt value is
stale. Check BLE range, USB power stability, and Instant Readout. Increasing
`VICTRON_STALE_AFTER_SECONDS` can tolerate slower advertisements, but should
not be used to conceal an unreliable BLE link.

### Channel 1 is visible but SOC is not

Channel 1 and SOC are separate telemetry packets. Channel 1 is native
`PowerMetrics`; SOC is the `battery_level` field in native `DeviceMetrics`.
Look at the bridge node's battery indicator or Device Metrics log rather than
the Power Metrics channels.

In a debug build, a successfully decoded SmartShunt advertisement reports
`SOC=...` or `SOC=unavailable`. The subsequent `Victron telemetry sent` line
reports the same distinction. If it says `SOC=unavailable`, confirm that the
SmartShunt itself shows SOC in VictronConnect and has completed any required
synchronization. If the log contains a numeric SOC but the client does not
show it, reconnect the client and wait for the next Victron telemetry send so
its node database receives a fresh Device Metrics packet.

### The node reports Victron voltage but no current

Victron uses sentinel values when a measurement is unavailable. The decoder
omits those fields instead of placing invalid values on the mesh. Confirm that
the measurement is available in VictronConnect and that the configured device
is broadcasting a supported Battery Monitor or Solar Charger record.

### Telemetry is visible but low-SOC messages are not

The warning is sent on the primary channel as a broadcast text message. Confirm
the receiving client shares that channel, can receive normal messages from the
node, and has not muted it. The alert remains latched until SOC reaches the
configured re-arm threshold.

## Test and update

Run the external decoder suite with:

```sh
nix develop --command uv run pio test \
  --project-dir meshtastic/firmware \
  --project-conf "$PWD/meshtastic/platformio-victron.ini" \
  -e victron-native
```

The firmware directory is a Git submodule pinned to the small project fork.
When rebasing it onto a newer Meshtastic release, preserve only the two
`MESHTASTIC_EXCLUDE_DEVICE_TELEMETRY` guards in `src/modules/Modules.cpp`.
Victron sources, targets, secrets generation, registration, and tests remain
owned by this repository. After changing the pin, run the decoder tests and
complete builds of both external targets before flashing hardware.
