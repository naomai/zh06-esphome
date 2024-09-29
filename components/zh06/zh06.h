#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/uart/uart.h"

namespace esphome {
namespace zh06 {

// known command bytes
#define PMS_CMD_AUTO_MANUAL 0x78  // data=0x41: perform measurement manually, data=0x40: perform measurement automatically (Q&A/Initiative upload mode)
#define PMS_CMD_TRIG_MANUAL 0x86  // trigger a manual measurement (Question & answer mode)
#define PMS_CMD_ON_STANDBY 0xA7   // data=0x01: go to standby mode, data=0x00: go to normal mode

static const uint16_t PMS_STABILISING_MS = 45000;  // time taken for the sensor to become stable after power on

enum ZH06State {
  ZH06_STATE_IDLE = 0,
  ZH06_STATE_STABILISING,
  ZH06_STATE_WAITING,
};

class ZH06Component : public uart::UARTDevice, public Component {
 public:
  ZH06Component() = default;
  void loop() override;
  float get_setup_priority() const override;
  void dump_config() override;
 
  void set_update_interval(uint32_t val) { update_interval_ = val; };

  void set_pm_1_0_sensor(sensor::Sensor *pm_1_0_sensor);
  void set_pm_2_5_sensor(sensor::Sensor *pm_2_5_sensor);
  void set_pm_10_0_sensor(sensor::Sensor *pm_10_0_sensor);

 protected:
  optional<bool> check_byte_();
  optional<bool> check_byte_response_();
  void parse_data_();
  void send_command_(uint8_t cmd, uint8_t data);
  uint16_t get_16_bit_uint_(uint8_t start_index);

  uint8_t data_[64];
  uint8_t data_index_{0};
  uint8_t initialised_{0};
  uint32_t fan_on_time_{0};
  uint32_t last_update_{0};
  uint32_t last_transmission_{0};
  uint32_t update_interval_{0};
  bool expecting_response_{false};
  ZH06State state_{ZH06_STATE_IDLE};

  // "Under Atmospheric Pressure"
  sensor::Sensor *pm_1_0_sensor_{nullptr};
  sensor::Sensor *pm_2_5_sensor_{nullptr};
  sensor::Sensor *pm_10_0_sensor_{nullptr};

};

}  // namespace zh06
}  // namespace esphome
