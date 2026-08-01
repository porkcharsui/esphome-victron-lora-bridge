#include "sx1280.h"

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <RadioLib.h>
#include <algorithm>

#ifdef USE_ARDUINO
#include <Arduino.h>
#endif

namespace esphome::sx1280 {

static const char *const TAG = "sx1280";

class ESPHomeRadioLibHal final : public RadioLibHal {
 public:
  explicit ESPHomeRadioLibHal(SX1280 *parent) : RadioLibHal(INPUT, OUTPUT, LOW, HIGH, RISING, FALLING), parent_(parent) {}

  void pinMode(uint32_t pin, uint32_t mode) override { ::pinMode(pin, mode); }
  void digitalWrite(uint32_t pin, uint32_t value) override { ::digitalWrite(pin, value); }
  uint32_t digitalRead(uint32_t pin) override { return ::digitalRead(pin); }
  void attachInterrupt(uint32_t interrupt_num, void (*callback)(), uint32_t mode) override {
    ::attachInterrupt(interrupt_num, callback, mode);
  }
  void detachInterrupt(uint32_t interrupt_num) override { ::detachInterrupt(interrupt_num); }
  void delay(RadioLibTime_t ms) override { ::delay(ms); }
  void delayMicroseconds(RadioLibTime_t us) override { ::delayMicroseconds(us); }
  RadioLibTime_t millis() override { return ::millis(); }
  RadioLibTime_t micros() override { return ::micros(); }
  long pulseIn(uint32_t pin, uint32_t state, RadioLibTime_t timeout) override { return ::pulseIn(pin, state, timeout); }
  void spiBegin() override {}
  void spiBeginTransaction() override { this->parent_->hal_spi_begin_transaction(); }
  void spiTransfer(uint8_t *out, size_t length, uint8_t *in) override {
    this->parent_->hal_spi_transfer(out, length, in);
  }
  void spiEndTransaction() override { this->parent_->hal_spi_end_transaction(); }
  void spiEnd() override {}
  void yield() override { ::yield(); }
  uint32_t pinToInterrupt(uint32_t pin) override { return digitalPinToInterrupt(pin); }

 protected:
  SX1280 *parent_;
};

SX1280 *SX1280::isr_instance_{nullptr};

void SX1280::hal_spi_transfer(uint8_t *out, size_t length, uint8_t *in) {
  for (size_t i = 0; i < length; i++)
    in[i] = this->transfer_byte(out[i]);
}

void SX1280::setup() {
  this->set_pa_disabled_();
  this->rst_pin_->setup();
  this->busy_pin_->setup();
  this->dio1_pin_->setup();
  this->spi_setup();
  this->isr_instance_ = this;
  if (!this->initialize_radio_()) {
    this->mark_failed();
    this->state_ = State::FAILED;
  }
}

bool SX1280::initialize_radio_() {
  this->set_pa_disabled_();
  const int cs = spi::Utility::get_pin_no(this->cs_);
  const int rst = spi::Utility::get_pin_no(this->rst_pin_);
  const int busy = spi::Utility::get_pin_no(this->busy_pin_);
  const int dio1 = spi::Utility::get_pin_no(this->dio1_pin_);
  if (std::min({cs, rst, busy, dio1}) < 0) {
    ESP_LOGE(TAG, "SX1280 requires non-inverted internal GPIO pins");
    return false;
  }
  if (this->radio_ == nullptr) {
    this->hal_ = new ESPHomeRadioLibHal(this);
    this->module_ = new Module(this->hal_, cs, dio1, rst, busy);
    this->radio_ = new ::SX1280(this->module_);
  }
  int16_t result = this->radio_->begin(this->frequency_mhz_, this->bandwidth_khz_, this->spreading_factor_,
                                      this->coding_rate_, 0x12, this->output_power_, this->preamble_);
  if (result == RADIOLIB_ERR_NONE)
    result = this->radio_->setCRC(this->crc_ ? 2 : 0);
  if (result != RADIOLIB_ERR_NONE) {
    this->record_error_(result);
    ESP_LOGE(TAG, "Radio initialization failed: %d", result);
    return false;
  }
  this->radio_->setDio1Action(SX1280::dio1_interrupt_);
  this->consecutive_errors_ = 0;
  this->state_ = State::IDLE;
  if (this->rx_start_) {
    if (!this->start_receive_())
      return false;
    ESP_LOGI(TAG, "📻 LoRa receiver listening — %.3f MHz, SF%u, BW %.2f kHz", this->frequency_mhz_,
             this->spreading_factor_, this->bandwidth_khz_);
  } else {
    if (!this->enter_sleep_())
      return false;
    ESP_LOGI(TAG, "💤 LoRa transmitter sleeping between scheduled packets");
  }
  return true;
}

void SX1280::loop() {
  if (this->state_ == State::FAILED)
    return;
  if (this->state_ == State::RECOVERING) {
    if (static_cast<int32_t>(millis() - this->recovery_due_ms_) >= 0) {
      increment_counter_(this->recovery_count_);
      if (!this->initialize_radio_())
        this->recovery_due_ms_ = millis() + 1000;
    }
    return;
  }
  if (this->irq_pending_)
    this->handle_interrupt_();
  if (this->state_ != State::TRANSMITTING && !this->tx_queue_.empty()) {
    const uint32_t now = millis();
    if (static_cast<int32_t>(now - this->tx_queue_.front().due_ms) >= 0) {
      auto packet = std::move(this->tx_queue_.front().data);
      this->tx_queue_.pop_front();
      this->start_transmit_(packet);
    }
  }
  if (millis() - this->last_diagnostic_ms_ >= 10000) {
    this->last_diagnostic_ms_ = millis();
    this->publish_diagnostics_();
  }
}

void SX1280::dio1_interrupt_() {
  if (SX1280::isr_instance_ != nullptr)
    SX1280::isr_instance_->irq_pending_ = true;
}

void SX1280::handle_interrupt_() {
  this->irq_pending_ = false;
  if (this->state_ == State::TRANSMITTING)
    this->handle_transmit_();
  else if (this->state_ == State::RECEIVING)
    this->handle_receive_();
}

void SX1280::handle_transmit_() {
  const int16_t result = this->radio_->finishTransmit();
  const size_t transmitted_size = this->active_tx_size_;
  this->active_tx_size_ = 0;
  this->set_pa_disabled_();
  if (result == RADIOLIB_ERR_NONE) {
    increment_counter_(this->tx_count_);
    this->consecutive_errors_ = 0;
    ESP_LOGI(TAG, "📡 LoRa transmission complete — %u bytes sent (TX #%lu)",
             static_cast<unsigned>(transmitted_size), static_cast<unsigned long>(this->tx_count_));
  } else {
    this->record_error_(result);
  }
  if (this->state_ != State::RECOVERING)
    this->enter_idle_mode_();
}

void SX1280::handle_receive_() {
  increment_counter_(this->rx_count_);
  const size_t length = this->radio_->getPacketLength(true);
  if (length == 0 || length > this->get_max_packet_size()) {
    increment_counter_(this->oversized_packets_);
    this->radio_->finishReceive();
    this->start_receive_();
    return;
  }
  std::vector<uint8_t> packet(length);
  const int16_t result = this->radio_->readData(packet.data(), packet.size());
  if (result == RADIOLIB_ERR_NONE) {
    this->last_rssi_ = this->radio_->getRSSI();
    this->last_snr_ = this->radio_->getSNR();
    this->last_packet_ms_ = millis();
    increment_counter_(this->valid_packets_);
    this->consecutive_errors_ = 0;
    for (auto *listener : this->listeners_)
      listener->on_packet(packet, this->last_rssi_, this->last_snr_);
  } else {
    this->record_error_(result);
  }
  if (this->state_ != State::RECOVERING)
    this->start_receive_();
}

bool SX1280::start_receive_() {
  this->set_pa_receive_();
  const int16_t result = this->radio_->startReceive();
  if (result != RADIOLIB_ERR_NONE) {
    this->set_pa_disabled_();
    this->record_error_(result);
    return false;
  }
  this->state_ = State::RECEIVING;
  return true;
}

bool SX1280::start_transmit_(const std::vector<uint8_t> &packet) {
  this->active_tx_size_ = 0;
  const int16_t standby_result = this->radio_->standby();
  if (standby_result != RADIOLIB_ERR_NONE) {
    this->set_pa_disabled_();
    this->record_error_(standby_result);
    if (this->state_ != State::RECOVERING)
      this->enter_idle_mode_();
    return false;
  }
  this->set_pa_transmit_();
  const int16_t result = this->radio_->startTransmit(packet.data(), packet.size());
  if (result != RADIOLIB_ERR_NONE) {
    this->set_pa_disabled_();
    this->record_error_(result);
    if (this->state_ != State::RECOVERING)
      this->enter_idle_mode_();
    return false;
  }
  this->active_tx_size_ = packet.size();
  this->state_ = State::TRANSMITTING;
  return true;
}

bool SX1280::enter_idle_mode_() {
  if (this->rx_start_)
    return this->start_receive_();
  return this->enter_sleep_();
}

bool SX1280::enter_sleep_() {
  // Disable the front end before changing radio state, then release both PA/LNA
  // control pins to input pulldowns as required by the T3S3 SX1280PA hardware.
  this->set_pa_disabled_();
  const int16_t result = this->radio_->sleep(true);
  if (result != RADIOLIB_ERR_NONE) {
    this->record_error_(result);
    return false;
  }
  this->set_pa_sleep_();
  this->state_ = State::IDLE;
  return true;
}

bool SX1280::enqueue_(const std::vector<uint8_t> &packet, uint32_t due_ms) {
  if (packet.size() > this->get_max_packet_size()) {
    increment_counter_(this->oversized_packets_);
    return false;
  }
  if (this->tx_queue_.size() >= this->tx_queue_size_) {
    increment_counter_(this->dropped_packets_);
    return false;
  }
  this->tx_queue_.push_back({packet, due_ms});
  return true;
}

void SX1280::transmit_packet(const std::vector<uint8_t> &packet, uint8_t repeat_count, uint32_t repeat_jitter_ms) {
  uint32_t due = millis();
  for (uint8_t repeat = 0; repeat < repeat_count; repeat++) {
    if (repeat != 0)
      due += repeat_jitter_ms == 0 ? 0 : random_uint32() % (repeat_jitter_ms + 1);
    if (!this->enqueue_(packet, due))
      break;  // Drop this newest packet and all later repeats.
  }
}

void SX1280::record_error_(int16_t error) {
  this->last_error_ = error;
  increment_counter_(this->radio_failures_);
  if (++this->consecutive_errors_ >= this->recovery_threshold_)
    this->recover_();
}

void SX1280::recover_() {
  this->active_tx_size_ = 0;
  this->set_pa_disabled_();
  if (this->radio_ != nullptr)
    this->radio_->standby();
  this->state_ = State::RECOVERING;
  this->recovery_due_ms_ = millis() + 100;
}

void SX1280::set_pa_disabled_() {
  if (this->tx_enable_pin_ != nullptr) {
    this->tx_enable_pin_->setup();
    this->tx_enable_pin_->digital_write(false);
  }
  if (this->rx_enable_pin_ != nullptr) {
    this->rx_enable_pin_->setup();
    this->rx_enable_pin_->digital_write(false);
  }
}

void SX1280::set_pa_sleep_() {
  if (this->tx_enable_pin_ != nullptr)
    this->tx_enable_pin_->pin_mode(gpio::FLAG_INPUT | gpio::FLAG_PULLDOWN);
  if (this->rx_enable_pin_ != nullptr)
    this->rx_enable_pin_->pin_mode(gpio::FLAG_INPUT | gpio::FLAG_PULLDOWN);
}

void SX1280::set_pa_receive_() {
  this->set_pa_disabled_();
  this->rx_enable_pin_->digital_write(true);
}

void SX1280::set_pa_transmit_() {
  this->set_pa_disabled_();
  this->tx_enable_pin_->digital_write(true);
}

void SX1280::publish_diagnostics_() {
#define SX1280_PUBLISH(sensor, value) \
  if ((sensor) != nullptr) (sensor)->publish_state(value)
  SX1280_PUBLISH(this->tx_count_sensor_, this->tx_count_);
  SX1280_PUBLISH(this->rx_count_sensor_, this->rx_count_);
  SX1280_PUBLISH(this->valid_packets_sensor_, this->valid_packets_);
  SX1280_PUBLISH(this->radio_failures_sensor_, this->radio_failures_);
  SX1280_PUBLISH(this->dropped_packets_sensor_, this->dropped_packets_);
  SX1280_PUBLISH(this->oversized_packets_sensor_, this->oversized_packets_);
  SX1280_PUBLISH(this->recovery_count_sensor_, this->recovery_count_);
  SX1280_PUBLISH(this->last_error_sensor_, this->last_error_);
  SX1280_PUBLISH(this->last_packet_age_sensor_, this->last_packet_ms_ == 0 ? NAN : (millis() - this->last_packet_ms_) / 1000.0f);
  SX1280_PUBLISH(this->rssi_sensor_, this->last_rssi_);
  SX1280_PUBLISH(this->snr_sensor_, this->last_snr_);
#undef SX1280_PUBLISH
}

void SX1280::dump_config() {
  ESP_LOGCONFIG(TAG, "SX1280:");
  LOG_SPI_DEVICE(this);
  ESP_LOGCONFIG(TAG, "  Frequency: %.3f MHz", this->frequency_mhz_);
  ESP_LOGCONFIG(TAG, "  Bandwidth: %.3f kHz", this->bandwidth_khz_);
  ESP_LOGCONFIG(TAG, "  SF%u CR 4/%u, preamble %u, CRC %s", this->spreading_factor_, this->coding_rate_,
                this->preamble_, YESNO(this->crc_));
  ESP_LOGCONFIG(TAG, "  SX1280 drive: %d dBm", this->output_power_);
  ESP_LOGCONFIG(TAG, "  TX queue: %u packets", static_cast<unsigned>(this->tx_queue_size_));
}

}  // namespace esphome::sx1280
