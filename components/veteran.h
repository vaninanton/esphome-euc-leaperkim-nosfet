// Copyright 2025 <Tony V>
#pragma once

#include <cstddef>
#include <deque>
#include <vector>
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/number/number.h"
#include "esphome/components/switch/switch.h"

namespace esphome::veteran {

// --- Единый источник оффсетов протокола (см. docs/veteran-protocol.md) ---
namespace proto {
constexpr size_t HEADER_SIZE = 3;
constexpr size_t LENGTH_BYTE_OFFSET = 3;
// Длина пакета = HEADER_SIZE + 1 + length_byte (length_byte = payload + 4 байта CRC)
constexpr size_t COMMON_PAYLOAD_OFFSET = 4;
constexpr size_t COMMON_PAYLOAD_SIZE = 32;
constexpr size_t EXTENDED_SUBTYPE_OFFSET = 46;
constexpr size_t EXTENDED_MIN_SIZE = 47;
constexpr size_t LIVE_SIZE = 77;
constexpr size_t LIVE_TEMP_CTRL_OFFSET = 59;
constexpr size_t LIVE_BMS_LEFT_CURRENT_OFFSET = 69;
constexpr size_t LIVE_BMS_RIGHT_CURRENT_OFFSET = 71;
constexpr size_t BMS_CELLS_BASE = 53;
constexpr size_t BMS_TEMPS_BASE = 47;
constexpr size_t BMS_CELLS_31_BASE = 59;
constexpr size_t BMS_CELLS_37_BASE = 71;  // ячейки 37–42 (42S), при size >= 83
constexpr size_t SETTINGS_HEADLIGHT_OFFSET = 47;
constexpr size_t SETTINGS_LOW_POWER_OFFSET = 60;
constexpr size_t SETTINGS_HIGH_SPEED_OFFSET = 61;
constexpr size_t SETTINGS_CUT_OFF_OFFSET = 62;
constexpr size_t SETTINGS_CHARGE_V_OFFSET = 63;
constexpr size_t SETTINGS_THO_RA_OFFSET = 66;
constexpr size_t SETTINGS_MIN_SIZE = 67;
}  // namespace proto

/// Данные одного BMS-блока (ячейки в V, температуры в °C, ток в A)
struct BMSBlockData {
  static constexpr size_t NUM_CELLS = 42;
  static constexpr size_t NUM_TEMPS = 6;
  float cells[NUM_CELLS]{};
  float temps[NUM_TEMPS]{};
  float current{};
};

struct BMSData {
  BMSBlockData left;
  BMSBlockData right;
};

struct EUCData {
  bool charging{};
  bool headlight{};
  bool high_speed_mode{};
  bool low_power_mode{};
  char firmware_version[9]{};
  float mileage_current{};
  float mileage_total{};
  float temperature_controller{};
  float temperature_motor{};
  uint16_t auto_off{};  // секунды; 0xFFFF/900+ = выключено
  uint16_t charging_stop_voltage{};
  uint16_t cut_off_angle{};
  uint16_t modelVersion{};
  uint16_t tho_ra{};
  uint16_t voltage{};

  BMSData bms;
};

class VeteranComponent : public Component {
 public:
  /// Только чтение текущих данных (для тестов и расширений). Обновляется при парсинге.
  const EUCData &get_euc_data() const { return euc_; }

  /// Очищает realtime-данные при отключении от колеса. Сохраняются: пробег, tho_ra, low_power_mode, high_speed_mode, firmware_version, charging_stop_voltage.
  void clear_realtime_data();

  /// Добавляет сырые байты в буфер и ставит готовые пакеты в очередь для разбора в loop().
  void parse_ble_packet(const std::vector<uint8_t> &x);

  /// Парсинг пакета и запись в euc_ (без публикации в сенсоры). Вызывается из loop() после извлечения из очереди.
  void parse_packet(const uint8_t *data, size_t size);

  /// Номинальное напряжение батареи (V), используется для расчёта процента заряда.
  void set_nominal_voltage(float v) { nominal_voltage_ = v; }
  void set_cell_count(int n) { cell_count_ = n; }
  /// Смещение для кодирования напряжения в пакете зарядки (опционально, иначе 145.0).
  void set_charge_voltage_offset(float v) { charge_voltage_offset_ = v; }
  /// Смещение для декодирования charging_stop_voltage из входящих пакетов (682 для Lynx, 0 для Nosfet).
  void set_charge_stop_voltage_offset(float v) { charge_stop_voltage_offset_ = v; }

  /// Процент заряда по напряжению (0–100). Линейная интерполяция между v_empty и v_full.
  float compute_battery_percentage(uint16_t voltage_raw) const;

  /// Пакет BLE для установки максимального напряжения зарядки (V). Записывается в FFE1.
  std::vector<uint8_t> get_charge_packet(float voltage);
  /// Пакет BLE для включения фары. Два чанка 9+4 CRC, итого 26 байт.
  std::vector<uint8_t> get_headlight_on_packet() const;
  /// Пакет BLE для выключения фары. Два чанка 9+4 CRC, итого 26 байт.
  std::vector<uint8_t> get_headlight_off_packet() const;

  // Сеттеры сущностей (все опциональны в YAML — указатели могут быть nullptr)
  void set_binary_sensor_charging(binary_sensor::BinarySensor *s) { binary_sensor_charging_ = s; }
  void set_switch_lights(switch_::Switch *s) { switch_lights_ = s; }
  void set_binary_sensor_high_speed_mode(binary_sensor::BinarySensor *s) { binary_sensor_high_speed_mode_ = s; }
  void set_binary_sensor_low_power_mode(binary_sensor::BinarySensor *s) { binary_sensor_low_power_mode_ = s; }
  void set_sensor_auto_off(sensor::Sensor *s) { sensor_auto_off_ = s; }
  void set_sensor_battery_percentage(sensor::Sensor *s) { sensor_battery_percentage_ = s; }
  void set_sensor_bms_left_current(sensor::Sensor *s) { sensor_bms_left_current_ = s; }
  void set_sensor_bms_right_current(sensor::Sensor *s) { sensor_bms_right_current_ = s; }
  void set_sensor_mileage_current(sensor::Sensor *s) { sensor_mileage_current_ = s; }
  void set_sensor_mileage_total(sensor::Sensor *s) { sensor_mileage_total_ = s; }
  void set_sensor_power(sensor::Sensor *s) { sensor_power_ = s; }
  void set_sensor_temperature_controller(sensor::Sensor *s) { sensor_temperature_controller_ = s; }
  void set_sensor_temperature_motor(sensor::Sensor *s) { sensor_temperature_motor_ = s; }
  void set_sensor_tho_ra(sensor::Sensor *s) { sensor_tho_ra_ = s; }
  void set_sensor_voltage(sensor::Sensor *s) { sensor_voltage_ = s; }
  void set_text_sensor_firmware_version(text_sensor::TextSensor *s) { text_sensor_firmware_version_ = s; }
  void set_binary_sensor_headlight(binary_sensor::BinarySensor *s) { binary_sensor_headlight_ = s; }
  void set_max_charging_voltage_number(number::Number *n) { max_charging_voltage_number_ = n; }

  void set_sensor_bms_left_temp_1(sensor::Sensor *s) { bms_temp_sensors_[0][0] = s; }
  void set_sensor_bms_left_temp_2(sensor::Sensor *s) { bms_temp_sensors_[0][1] = s; }
  void set_sensor_bms_left_temp_3(sensor::Sensor *s) { bms_temp_sensors_[0][2] = s; }
  void set_sensor_bms_left_temp_4(sensor::Sensor *s) { bms_temp_sensors_[0][3] = s; }
  void set_sensor_bms_left_temp_5(sensor::Sensor *s) { bms_temp_sensors_[0][4] = s; }
  void set_sensor_bms_left_temp_6(sensor::Sensor *s) { bms_temp_sensors_[0][5] = s; }
  void set_sensor_bms_right_temp_1(sensor::Sensor *s) { bms_temp_sensors_[1][0] = s; }
  void set_sensor_bms_right_temp_2(sensor::Sensor *s) { bms_temp_sensors_[1][1] = s; }
  void set_sensor_bms_right_temp_3(sensor::Sensor *s) { bms_temp_sensors_[1][2] = s; }
  void set_sensor_bms_right_temp_4(sensor::Sensor *s) { bms_temp_sensors_[1][3] = s; }
  void set_sensor_bms_right_temp_5(sensor::Sensor *s) { bms_temp_sensors_[1][4] = s; }
  void set_sensor_bms_right_temp_6(sensor::Sensor *s) { bms_temp_sensors_[1][5] = s; }

  void setup() override;
  void loop() override;
  void dump_config() override;

 protected:
  EUCData euc_;

  std::vector<uint8_t> ble_buffer_;
  static constexpr size_t BLE_BUFFER_MAX = 512;
  static constexpr size_t PACKET_QUEUE_MAX = 4;
  std::deque<std::vector<uint8_t>> packet_queue_;

  // Throttle: не публиковать сенсоры чаще чем раз в PUBLISH_INTERVAL_MS мс.
  static constexpr uint32_t PUBLISH_INTERVAL_MS = 2000;
  uint32_t last_publish_ms_{0};

  float nominal_voltage_{151.2f};
  size_t cell_count_{36};
  float charge_voltage_offset_{145.0f};
  float charge_stop_voltage_offset_{682.0f};

  binary_sensor::BinarySensor *binary_sensor_charging_{nullptr};
  switch_::Switch *switch_lights_{nullptr};
  binary_sensor::BinarySensor *binary_sensor_high_speed_mode_{nullptr};
  binary_sensor::BinarySensor *binary_sensor_low_power_mode_{nullptr};
  sensor::Sensor *sensor_auto_off_{nullptr};
  sensor::Sensor *sensor_battery_percentage_{nullptr};
  sensor::Sensor *sensor_bms_left_current_{nullptr};
  sensor::Sensor *sensor_bms_right_current_{nullptr};
  sensor::Sensor *sensor_mileage_current_{nullptr};
  sensor::Sensor *sensor_mileage_total_{nullptr};
  sensor::Sensor *sensor_power_{nullptr};
  sensor::Sensor *sensor_temperature_controller_{nullptr};
  sensor::Sensor *sensor_temperature_motor_{nullptr};
  sensor::Sensor *sensor_tho_ra_{nullptr};
  sensor::Sensor *sensor_voltage_{nullptr};
  sensor::Sensor *bms_temp_sensors_[2][BMSBlockData::NUM_TEMPS]{};
  text_sensor::TextSensor *text_sensor_firmware_version_{nullptr};
  binary_sensor::BinarySensor *binary_sensor_headlight_{nullptr};
  number::Number *max_charging_voltage_number_{nullptr};

  /// Парсит общий payload (offset 4–35): напряжение, скорость, пробег, температура, заряд и т.д.
  void parse_common_payload(const uint8_t *data, size_t size);
  /// Парсит расширенный payload по sub-type (offset 46): Live, BMS, Settings.
  void parse_extended_payload(const uint8_t *data, size_t size);
  /// Парсит ячейки BMS: base_idx — смещение в пакете, cell_offset — индекс в bms.cells[], count — кол-во.
  void parse_bms_cells(const uint8_t *data, size_t size, size_t base_idx, BMSBlockData &bms, size_t cell_offset,
                       size_t count);
  /// Парсит температуры и ячейки 31–36 (и 37–42 для 42S) BMS-блока; side 0=left, 1=right.
  void parse_bms_temps_and_cells(const uint8_t *data, size_t size, BMSBlockData &bms, int side);
  /// Парсит sub-type 0x08 Settings: фара, low_power, high_speed, cut_off, tho_ra, charging_stop_voltage.
  void parse_settings_subtype(const uint8_t *data, size_t size);

  /// Копирует euc_ в сенсоры и бинарные сенсоры; вызывается после parse_packet и clear_realtime_data.
  void publish_state_from_euc();

  /// Проверяет CRC32 (последние 4 байта big-endian) пакета. Возвращает true при совпадении.
  bool check_crc32(const uint8_t *data, size_t size);
};

}  // namespace esphome::veteran
