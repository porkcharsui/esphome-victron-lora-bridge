#pragma once

#include "esphome/components/sensor/sensor.h"
#include "esphome/components/spi/spi.h"
#include "esphome/core/component.h"
#include "esphome/core/gpio.h"

#include <cstdint>
#include <deque>
#include <limits>
#include <vector>

class Module;
class RadioLibHal;
class SX1280;

namespace esphome::sx1280 {

class SX1280Listener {
 public:
  virtual void on_packet(const std::vector<uint8_t> &packet, float rssi, float snr) = 0;
};

class SX1280 : public Component,
               public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW,
                                    spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_8MHZ> {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  void set_rst_pin(GPIOPin *pin) { this->rst_pin_ = pin; }
  void set_busy_pin(GPIOPin *pin) { this->busy_pin_ = pin; }
  void set_dio1_pin(GPIOPin *pin) { this->dio1_pin_ = pin; }
  void set_tx_enable_pin(GPIOPin *pin) { this->tx_enable_pin_ = pin; }
  void set_rx_enable_pin(GPIOPin *pin) { this->rx_enable_pin_ = pin; }
  void set_frequency(float value) { this->frequency_mhz_ = value; }
  void set_bandwidth(float value) { this->bandwidth_khz_ = value; }
  void set_spreading_factor(uint8_t value) { this->spreading_factor_ = value; }
  void set_coding_rate(uint8_t value) { this->coding_rate_ = value; }
  void set_preamble(uint16_t value) { this->preamble_ = value; }
  void set_crc(bool value) { this->crc_ = value; }
  void set_output_power(int8_t value) { this->output_power_ = value; }
  void set_rx_start(bool value) { this->rx_start_ = value; }
  void set_tx_queue_size(size_t value) { this->tx_queue_size_ = value; }
  void set_recovery_threshold(uint8_t value) { this->recovery_threshold_ = value; }

  void set_tx_count_sensor(sensor::Sensor *sensor) { this->tx_count_sensor_ = sensor; }
  void set_rx_count_sensor(sensor::Sensor *sensor) { this->rx_count_sensor_ = sensor; }
  void set_valid_packets_sensor(sensor::Sensor *sensor) { this->valid_packets_sensor_ = sensor; }
  void set_radio_failures_sensor(sensor::Sensor *sensor) { this->radio_failures_sensor_ = sensor; }
  void set_dropped_packets_sensor(sensor::Sensor *sensor) { this->dropped_packets_sensor_ = sensor; }
  void set_oversized_packets_sensor(sensor::Sensor *sensor) { this->oversized_packets_sensor_ = sensor; }
  void set_recovery_count_sensor(sensor::Sensor *sensor) { this->recovery_count_sensor_ = sensor; }
  void set_last_error_sensor(sensor::Sensor *sensor) { this->last_error_sensor_ = sensor; }
  void set_last_packet_age_sensor(sensor::Sensor *sensor) { this->last_packet_age_sensor_ = sensor; }
  void set_rssi_sensor(sensor::Sensor *sensor) { this->rssi_sensor_ = sensor; }
  void set_snr_sensor(sensor::Sensor *sensor) { this->snr_sensor_ = sensor; }

  void register_listener(SX1280Listener *listener) { this->listeners_.push_back(listener); }
  void transmit_packet(const std::vector<uint8_t> &packet, uint8_t repeat_count, uint32_t repeat_jitter_ms);
  size_t get_max_packet_size() const { return 255; }

  // Used only by the RadioLib HAL adapter.
  void hal_spi_begin_transaction() { this->enable(); }
  void hal_spi_transfer(uint8_t *out, size_t length, uint8_t *in);
  void hal_spi_end_transaction() { this->disable(); }

 protected:
  enum class State : uint8_t { INITIALIZING, IDLE, RECEIVING, TRANSMITTING, RECOVERING, FAILED };

  struct QueuedPacket {
    std::vector<uint8_t> data;
    uint32_t due_ms;
  };

  static void dio1_interrupt_();
  bool initialize_radio_();
  bool start_receive_();
  bool start_transmit_(const std::vector<uint8_t> &packet);
  bool enter_idle_mode_();
  bool enter_sleep_();
  void handle_interrupt_();
  void handle_receive_();
  void handle_transmit_();
  void set_pa_disabled_();
  void set_pa_sleep_();
  void set_pa_receive_();
  void set_pa_transmit_();
  void record_error_(int16_t error);
  void recover_();
  void publish_diagnostics_();
  bool enqueue_(const std::vector<uint8_t> &packet, uint32_t due_ms);
  static void increment_counter_(uint32_t &counter) {
    if (counter != std::numeric_limits<uint32_t>::max())
      counter++;
  }

  GPIOPin *rst_pin_{nullptr};
  GPIOPin *busy_pin_{nullptr};
  GPIOPin *dio1_pin_{nullptr};
  GPIOPin *tx_enable_pin_{nullptr};
  GPIOPin *rx_enable_pin_{nullptr};

  float frequency_mhz_{2400.5f};
  float bandwidth_khz_{406.25f};
  uint8_t spreading_factor_{7};
  uint8_t coding_rate_{6};
  uint16_t preamble_{12};
  bool crc_{true};
  int8_t output_power_{3};
  bool rx_start_{true};
  size_t tx_queue_size_{8};
  uint8_t recovery_threshold_{3};

  RadioLibHal *hal_{nullptr};
  Module *module_{nullptr};
  ::SX1280 *radio_{nullptr};
  State state_{State::INITIALIZING};
  volatile bool irq_pending_{false};
  uint8_t consecutive_errors_{0};
  uint32_t recovery_due_ms_{0};
  uint32_t last_diagnostic_ms_{0};
  uint32_t last_packet_ms_{0};
  size_t active_tx_size_{0};
  std::deque<QueuedPacket> tx_queue_;
  std::vector<SX1280Listener *> listeners_;

  uint32_t tx_count_{0};
  uint32_t rx_count_{0};
  uint32_t valid_packets_{0};
  uint32_t radio_failures_{0};
  uint32_t dropped_packets_{0};
  uint32_t oversized_packets_{0};
  uint32_t recovery_count_{0};
  int16_t last_error_{0};
  float last_rssi_{NAN};
  float last_snr_{NAN};

  sensor::Sensor *tx_count_sensor_{nullptr};
  sensor::Sensor *rx_count_sensor_{nullptr};
  sensor::Sensor *valid_packets_sensor_{nullptr};
  sensor::Sensor *radio_failures_sensor_{nullptr};
  sensor::Sensor *dropped_packets_sensor_{nullptr};
  sensor::Sensor *oversized_packets_sensor_{nullptr};
  sensor::Sensor *recovery_count_sensor_{nullptr};
  sensor::Sensor *last_error_sensor_{nullptr};
  sensor::Sensor *last_packet_age_sensor_{nullptr};
  sensor::Sensor *rssi_sensor_{nullptr};
  sensor::Sensor *snr_sensor_{nullptr};

  static SX1280 *isr_instance_;
};

}  // namespace esphome::sx1280
