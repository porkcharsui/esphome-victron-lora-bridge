# SX1280 component

The local component targets ESPHome 2026.7.3 and RadioLib 7.7.1. RadioLib types
do not escape the component. Its listener/transport boundary follows ESPHome's
built-in SX126x packet transport design.

`sx1280` is an ESPHome SPI device. It accepts `rst_pin`, `busy_pin`, `dio1_pin`,
the required `tx_enable_pin` and `rx_enable_pin`, frequency, bandwidth, spreading
factor, coding rate, preamble length, CRC, output power, `rx_start`, bounded TX
queue size, and recovery timing. Code generation rejects PA drive above 3 dBm,
missing or aliased control pins, unsupported modulation combinations, and queue
sizes outside the bound.

`rx_start: false` selects transmit-only operation. The radio retains its
configuration in sleep between queued transmissions, and the T3S3 SX1280PA
TX/RX front-end pins switch to input pulldowns so the PA and LNA can sleep. The
default `rx_start: true` retains continuous reception for receiver nodes.

`packet_transport: platform: sx1280` accepts `sx1280_id`, standard ESPHome packet
transport options, `repeat_count`, `repeat_jitter`, `transmit_enabled`, and
`send_on_update`. Setting `send_on_update: false` suppresses event-driven packets
while retaining full snapshots on the configured polling interval. Packet size
is reported to the packet-transport core so entity snapshots split into valid
frames instead of being truncated. Encryption and rolling codes are enabled in
both production configurations; ping-pong is deliberately disabled.

`on_first_packet` runs an automation once, when the first authenticated packet is
received after boot. Rejected, corrupt, and replayed packets do not fire it.

ISRs only set completion flags. Payload copying, authentication, callbacks, and
fault recovery run from the ESPHome loop. The driver bounds transmit queues,
receive buffers, and repeat scheduling. Overflow drops the newest packet. PA
control is TX=1/RX=0 while sending, TX=0/RX=1 while listening, and both disabled
during initialization, reset, and recovery.

The sensor platform exposes TX/RX counts, authenticated packet count, radio/CRC
failures, dropped and oversized packets, recovery count, last error, last packet
age, RSSI, and SNR. Counters use saturating increments so long-running nodes do
not wrap silently. Every completed transmission or recovery returns to
continuous reception.
