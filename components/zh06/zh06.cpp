#include "zh06.h"
#include "esphome/core/log.h"

namespace esphome {
namespace zh06 {

static const char *const TAG = "zh06";


void ZH06Component::set_pm_1_0_sensor(sensor::Sensor *pm_1_0_sensor) { pm_1_0_sensor_ = pm_1_0_sensor; }
void ZH06Component::set_pm_2_5_sensor(sensor::Sensor *pm_2_5_sensor) { pm_2_5_sensor_ = pm_2_5_sensor; }
void ZH06Component::set_pm_10_0_sensor(sensor::Sensor *pm_10_0_sensor) { pm_10_0_sensor_ = pm_10_0_sensor; }

void ZH06Component::loop() {
  const uint32_t now = millis();

  // If we update less often than it takes the device to stabilise, spin the fan down
  // rather than running it constantly. It does take some time to stabilise, so we
  // need to keep track of what state we're in.
  if (this->update_interval_ > PMS_STABILISING_MS) {
    if (this->initialised_ == 0) {
      this->send_command_(PMS_CMD_AUTO_MANUAL, 0x41);
      this->send_command_(PMS_CMD_ON_STANDBY, 0);
      this->initialised_ = 1;
    }
    switch (this->state_) {
      case ZH06_STATE_IDLE:
        // Power on the sensor now so it'll be ready when we hit the update time
        if (now - this->last_update_ < (this->update_interval_ - PMS_STABILISING_MS))
          return;

        this->state_ = ZH06_STATE_STABILISING;
        this->send_command_(PMS_CMD_ON_STANDBY, 0);
        this->fan_on_time_ = now;
        return;
      case ZH06_STATE_STABILISING:
        // wait for the sensor to be stable
        if (now - this->fan_on_time_ < PMS_STABILISING_MS)
          return;
        // consume any command responses that are in the serial buffer
        while (this->available())
          this->read_byte(&this->data_[0]);
        // Trigger a new read
        this->send_command_(PMS_CMD_TRIG_MANUAL, 0);
        this->state_ = ZH06_STATE_WAITING;
        break;
      case ZH06_STATE_WAITING:
        // Just go ahead and read stuff
        break;
    }
  } else if (now - this->last_update_ < this->update_interval_) {
    // Otherwise just leave the sensor powered up and come back when we hit the update
    // time
      if (this->initialised_ == 0) {
        this->send_command_(PMS_CMD_AUTO_MANUAL, 0x40);
        this->initialised_ = 1;
      }
    return;
  }

  if (now - this->last_transmission_ >= 500) {
    // last transmission too long ago. Reset RX index.
    this->data_index_ = 0;
  }

  if (this->available() == 0)
    return;

  this->last_transmission_ = now;
  while (this->available() != 0) {
    this->read_byte(&this->data_[this->data_index_]);
    auto check = this->check_byte_();
    if (!check.has_value()) {
      // finished
      this->parse_data_();
      this->data_index_ = 0;
      this->last_update_ = now;
      this->expecting_response_ = false;
    } else if (!*check) {
      // wrong data
      this->data_index_ = 0;
    } else {
      // next byte
      this->data_index_++;
    }
  }
}
float ZH06Component::get_setup_priority() const { return setup_priority::DATA; }
optional<bool> ZH06Component::check_byte_() {
  if(this->expecting_response_)
    return this->check_byte_response_();
  uint8_t index = this->data_index_;
  uint8_t byte = this->data_[index];

  if (index == 0)
    return byte == 0x42;

  if (index == 1)
    return byte == 0x4D;

  if (index == 2)
    return true;

  uint16_t payload_length = this->get_16_bit_uint_(2);
  if (index == 3) {
    bool length_matches = payload_length == 28;

    if (!length_matches) {
      ESP_LOGW(TAG, "ZH06 length %u doesn't match. Are you using the correct ZH06 type?", payload_length);
      return false;
    }
    return true;
  }

  // start (16bit) + length (16bit) + DATA (payload_length-2 bytes) + checksum (16bit)
  uint8_t total_size = 4 + payload_length;

  if (index < total_size - 1)
    return true;

  // checksum is without checksum bytes
  uint16_t checksum = 0;
  for (uint8_t i = 0; i < total_size - 2; i++)
    checksum += this->data_[i];

  uint16_t check = this->get_16_bit_uint_(total_size - 2);
  if (checksum != check) {
    ESP_LOGW(TAG, "ZH06 checksum mismatch! 0x%02X!=0x%02X", checksum, check);
    return false;
  }

  return {};
}

optional<bool> ZH06Component::check_byte_response_() {
  uint8_t index = this->data_index_;
  uint8_t byte = this->data_[index];

  if (index == 0)
    return byte == 0xFF;

  if(index < 8)
    return true;

  // checksum is without checksum bytes
  uint8_t checksum = 0xFF;
  for (uint8_t i = 0; i < 8; i++)
    checksum -= this->data_[i];

  uint16_t check = this->data_[8];
  if (checksum != check) {
    ESP_LOGW(TAG, "ZH06 checksum mismatch! 0x%02X!=0x%02X", checksum, check);
    return false;
  }

  return {};
}

void ZH06Component::send_command_(uint8_t cmd, uint8_t data) {
  this->data_index_ = 0;
  this->data_[data_index_++] = 0xFF;
  this->data_[data_index_++] = 0x01;
  this->data_[data_index_++] = cmd;
  this->data_[data_index_++] = data;
  this->data_[data_index_++] = 0;
  this->data_[data_index_++] = 0;
  this->data_[data_index_++] = 0;
  this->data_[data_index_++] = 0;
  uint8_t sum = 0xFF;
  for (int i = 0; i < data_index_; i++) {
    sum -= this->data_[i];
  }
  this->data_[data_index_++] = sum;
  for (int i = 0; i < data_index_; i++) {
    this->write_byte(this->data_[i]);
  }
  this->data_index_ = 0;

  this->expecting_response_ = cmd==0x86;
}

void ZH06Component::parse_data_() {
  uint8_t cmd = this->data_[1];
  if(cmd == 0x86 && this->expecting_response_) {
    uint16_t pm_1_0_concentration, pm_2_5_concentration, pm_10_0_concentration;

    pm_1_0_concentration = this->get_16_bit_uint_(6);
    pm_2_5_concentration = this->get_16_bit_uint_(2);
    pm_10_0_concentration = this->get_16_bit_uint_(4);
    

    ESP_LOGD(TAG,
              "Got PM1.0 Concentration: %u µg/m^3, PM2.5 Concentration %u µg/m^3, PM10.0 Concentration: %u µg/m^3",
              pm_1_0_concentration, pm_2_5_concentration, pm_10_0_concentration);

    if (this->pm_1_0_sensor_ != nullptr)
      this->pm_1_0_sensor_->publish_state(pm_1_0_concentration);
    if (this->pm_2_5_sensor_ != nullptr)
      this->pm_2_5_sensor_->publish_state(pm_2_5_concentration);
    if (this->pm_10_0_sensor_ != nullptr)
      this->pm_10_0_sensor_->publish_state(pm_10_0_concentration);
  }
  

  // Spin down the sensor again if we aren't going to need it until more time has
  // passed than it takes to stabilise
  if (this->update_interval_ > PMS_STABILISING_MS) {
    this->send_command_(PMS_CMD_ON_STANDBY, 1);
    this->state_ = ZH06_STATE_IDLE;
  }

  this->status_clear_warning();
}


uint16_t ZH06Component::get_16_bit_uint_(uint8_t start_index) {
  return (uint16_t(this->data_[start_index]) << 8) | uint16_t(this->data_[start_index + 1]);
}
void ZH06Component::dump_config() {
  ESP_LOGCONFIG(TAG, "ZH06:");

  LOG_SENSOR("  ", "PM1.0", this->pm_1_0_sensor_);
  LOG_SENSOR("  ", "PM2.5", this->pm_2_5_sensor_);
  LOG_SENSOR("  ", "PM10.0", this->pm_10_0_sensor_);
  this->check_uart_settings(9600);
}

}  // namespace zh06
}  // namespace esphome
