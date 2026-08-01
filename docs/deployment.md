# Deployment and acceptance

## Hardware qualification

The PCB label is not proof that a board has the PA front end. Before using this
firmware, photograph both boards and confirm that each is a populated V1.1
SX1280PA assembly. Attach the correct 2.4 GHz antenna before any transmission.

Flash LILYGO's matching `SX1280PA_PingPong` example and test both directions at
an SX1280 drive setting of 3 dBm. With a scope or logic analyzer, verify GPIO10
is high and GPIO21 low only during TX, and GPIO10 low and GPIO21 high during RX.
Both must be low through initialization and reset. Do not continue if both are
ever high. LILYGO warns that excessive SX1280 drive may damage the PA front end.

ESPHome may warn that GPIO3 is a strapping pin and GPIO36 is associated with
PSRAM. Those assignments are intentional for this board revision; do not remap
them without checking the board schematic and hardware.

## Build and flash

The commands below assume the repository's development shell is active and the
locked dependencies have been installed as described in the README's
Installation section:

```sh
cp .env.example .env
# Populate .env with freshly generated credentials and the two Victron identities.
uv run python scripts/generate_secrets.py
uv run esphome compile esphome/victron.yml
uv run esphome compile esphome/bridge.yml
```

Flash `victron.yml` to the Victron BLE LoRa board over USB. It contains no
production network services. Flash `bridge.yml` to the uplink board. Use
`victron-maintenance.yml` only while servicing the Victron BLE LoRa node, then
restore the production image. Rotate any previously exposed Wi-Fi, MQTT, or
radio credentials; none of those values belong in `.env`.

## Bench and installed-path acceptance

- Exchange at least 1,000 raw counter packets using `tests/fixtures/raw-radio.yml`.
  Confirm no reset, deadlock, progressive heap loss, simultaneous PA enable, or
  failure to return to RX after TX.
- Force queue overflow and malformed frames. The newest queued packet must be
  dropped, the relevant counter must increase, and the receiver must recover.
- Power-cycle either node independently and verify encrypted traffic resumes.
  Replayed captures must be rejected by rolling-code checks.
- Stop each Victron source for more than two minutes; its readings must become
  unavailable and recover when advertisements resume.
- Receive at least 995 of 1,000 complete bench snapshots.
- On the installed source-to-uplink path, receive at least 99% of snapshots for 24
  hours with no gap longer than five minutes. The link sensor must clearly show
  the outage while stale remote values remain visible for diagnosis.
- Repeat with heavy uplink Wi-Fi and source BLE traffic and record the measured loss.
- Perform ten independent power cycles of each node, with automatic recovery on
  every cycle.
- Finish with a 72-hour soak: no reset, deadlock, queue growth, or progressive
  heap loss.

For the battery-powered node, measure current at the battery input with the OLED
off for at least five minutes before and after power-related firmware changes.
Record idle current, TX peak current, and average current across at least five
scheduled transmissions. Between transmissions GPIO10 and GPIO21 must both be
input pulldowns; during TX only GPIO10 may be high. Target at least a 20% average
current reduction without falling below the packet-delivery criteria above.

If a target is missed, tune in this order: antenna placement, carrier frequency,
physical separation from the access point, repeat timing, then scheduled BLE
scan pauses around transmission. Keep the SX1280 architecture and the 3 dBm cap.
