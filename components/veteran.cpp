// Copyright 2025 <Tony V>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <vector>
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "veteran.h"
#include "esp_crc.h"

namespace esphome::veteran {

static const char *const TAG = "veteran";

using namespace proto;

static constexpr uint8_t BLE_HEADER[] = {0xDC, 0x5A, 0x5C};
static constexpr size_t PACKET_MIN_SIZE = HEADER_SIZE + 1 + COMMON_PAYLOAD_SIZE;  // 3+1+32 = 36
static constexpr uint16_t AUTO_OFF_DISABLED_THRESHOLD = 900;  // значения ≥ этого — "выключено" (NAN)

enum SubType : uint8_t {
  SUBTYPE_LIVE = 0x00,
  SUBTYPE_LIVE_ALT = 0x04,
  SUBTYPE_BMS_LEFT_CELLS_1_15 = 0x01,
  SUBTYPE_BMS_LEFT_CELLS_16_30 = 0x02,
  SUBTYPE_BMS_LEFT_TEMPS_CELLS_31_36 = 0x03,
  SUBTYPE_BMS_RIGHT_CELLS_1_15 = 0x05,
  SUBTYPE_BMS_RIGHT_CELLS_16_30 = 0x06,
  SUBTYPE_BMS_RIGHT_TEMPS_CELLS_31_36 = 0x07,
  SUBTYPE_SETTINGS = 0x08,
};

static inline uint16_t read_u16_be(const uint8_t *p) {
  return (uint16_t)((p[0] << 8) | p[1]);
}
static inline int16_t read_s16_be(const uint8_t *p) {
  return (int16_t)read_u16_be(p);
}
static inline uint32_t read_u32_mid_le(const uint8_t *p) {
  return ((uint32_t)p[2] << 24) | ((uint32_t)p[3] << 16) | ((uint32_t)p[0] << 8) | p[1];
}

/// Добавляет 4 байта CRC32 (big-endian) к out. CRC считается esp_crc32_le над data[0..len).
static void append_crc32_be(std::vector<uint8_t> &out, const uint8_t *data, size_t len) {
  uint32_t crc = esp_crc32_le(0, data, len);
  out.push_back((crc >> 24) & 0xFF);
  out.push_back((crc >> 16) & 0xFF);
  out.push_back((crc >> 8) & 0xFF);
  out.push_back(crc & 0xFF);
}

/// Добавляет payload + CRC32 big-endian. Общий метод для формирования исходящих пакетов настроек (фары, заряд и т.д.).
static void append_settings_chunk(std::vector<uint8_t> &out, const uint8_t *payload, size_t len) {
  out.insert(out.end(), payload, payload + len);
  append_crc32_be(out, payload, len);
}

void VeteranComponent::setup() {}

void VeteranComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Veteran EUC:");
  ESP_LOGCONFIG(TAG, "  Nominal voltage: %.1f V", nominal_voltage_);
  ESP_LOGCONFIG(TAG, "  Cell count: %zu", cell_count_);
  ESP_LOGCONFIG(TAG, "  Charge voltage offset: %.1f", charge_voltage_offset_);
  ESP_LOGCONFIG(TAG, "  Charge stop voltage offset: %.1f", charge_stop_voltage_offset_);
}

/// Обрабатывает очередь пакетов: parse_packet → publish_state_from_euc (throttled).
void VeteranComponent::loop() {
  if (packet_queue_.empty())
    return;
  // Process at most one packet per loop() call to avoid blocking the main loop.
  std::vector<uint8_t> packet = std::move(packet_queue_.front());
  packet_queue_.pop_front();
  parse_packet(packet.data(), packet.size());

  uint32_t now = millis();
  if (now - last_publish_ms_ >= PUBLISH_INTERVAL_MS) {
    last_publish_ms_ = now;
    publish_state_from_euc();
  }
}

/// Очищает realtime-данные при отключении; сохраняет пробег, tho_ra, режимы, firmware, charging_stop.
void VeteranComponent::clear_realtime_data() {
  // Сохраняем поля, которые переживают отключение
  float mileage_cur = euc_.mileage_current;
  float mileage_tot = euc_.mileage_total;
  uint16_t tho_ra_val = euc_.tho_ra;
  bool low_power = euc_.low_power_mode;
  bool high_speed = euc_.high_speed_mode;
  uint16_t charge_stop_v = euc_.charging_stop_voltage;
  char fw_ver[sizeof(euc_.firmware_version)];
  memcpy(fw_ver, euc_.firmware_version, sizeof(fw_ver));

  euc_ = {};

  euc_.mileage_current = mileage_cur;
  euc_.mileage_total = mileage_tot;
  euc_.tho_ra = tho_ra_val;
  euc_.low_power_mode = low_power;
  euc_.high_speed_mode = high_speed;
  euc_.charging_stop_voltage = charge_stop_v;
  memcpy(euc_.firmware_version, fw_ver, sizeof(fw_ver));

  ble_buffer_.clear();
  packet_queue_.clear();
  last_publish_ms_ = 0;
  publish_state_from_euc();
}

/// Добавляет сырые байты в буфер, ищет заголовок 0xDC5A5C, извлекает пакеты с проверкой CRC в очередь.
void VeteranComponent::parse_ble_packet(const std::vector<uint8_t> &x) {
  if (ble_buffer_.size() + x.size() > BLE_BUFFER_MAX)
    ble_buffer_.clear();
  ble_buffer_.insert(ble_buffer_.end(), x.begin(), x.end());

  size_t pos = 0;
  const size_t buf_size = ble_buffer_.size();
  while (pos + HEADER_SIZE <= buf_size) {
    if (ble_buffer_[pos] != BLE_HEADER[0] || ble_buffer_[pos + 1] != BLE_HEADER[1] ||
        ble_buffer_[pos + 2] != BLE_HEADER[2]) {
      pos++;
      continue;
    }
    if (pos + HEADER_SIZE + 1 > buf_size)
      break;
    uint8_t length_byte = ble_buffer_[pos + LENGTH_BYTE_OFFSET];
    size_t packet_len = HEADER_SIZE + 1 + length_byte;
    if (pos + packet_len > buf_size)
      break;

    const uint8_t *p = ble_buffer_.data() + pos;
    if (check_crc32(p, packet_len)) {
      if (packet_queue_.size() < PACKET_QUEUE_MAX)
        packet_queue_.push_back(std::vector<uint8_t>(p, p + packet_len));
    }
    pos += packet_len;
  }

  // Keep only unprocessed tail.
  if (pos > 0)
    ble_buffer_.erase(ble_buffer_.begin(), ble_buffer_.begin() + pos);
}

/// Разбирает один пакет: common payload + extended (если modelVersion≥5).
void VeteranComponent::parse_packet(const uint8_t *data, size_t size) {
  if (size < PACKET_MIN_SIZE)
    return;

  parse_common_payload(data, size);

  if (euc_.modelVersion >= 5 && size >= EXTENDED_MIN_SIZE)
    parse_extended_payload(data, size);
}

/// Парсит общий блок (offset 4–35): voltage, speed, mileage, temps, charging, fw и т.д.
void VeteranComponent::parse_common_payload(const uint8_t *data, size_t size) {
  (void)size;
  const uint8_t *p = data + COMMON_PAYLOAD_OFFSET;
  euc_.voltage = read_u16_be(p + 0);
  euc_.mileage_current = read_u32_mid_le(p + 4) / 1000.0f;
  euc_.mileage_total = read_u32_mid_le(p + 8) / 1000.0f;
  euc_.temperature_motor = read_u16_be(p + 14) / 100.0f;
  euc_.auto_off = read_u16_be(p + 16);
  euc_.charging = data[COMMON_PAYLOAD_OFFSET + 19] == 0x01;

  uint16_t fw = read_u16_be(p + 24);
  euc_.modelVersion = fw / 1000;
  // Формат как в официальном приложении (декомпиляция): major.minor.patch (major без ведущего нуля).
  snprintf(euc_.firmware_version, sizeof(euc_.firmware_version), "%d.%01d.%02d",
           euc_.modelVersion, (fw / 100) % 10, fw % 100);
}

/// Диспетчер по sub-type: Live (0x00/0x04), BMS left/right, Settings (0x08).
void VeteranComponent::parse_extended_payload(const uint8_t *data, size_t size) {
  uint8_t subtype = data[EXTENDED_SUBTYPE_OFFSET];
  switch (subtype) {
    case SUBTYPE_LIVE:
    case SUBTYPE_LIVE_ALT:
      if (size >= LIVE_SIZE) {
        euc_.temperature_controller = read_u16_be(data + LIVE_TEMP_CTRL_OFFSET) / 100.0f;
        euc_.bms.left.current = read_s16_be(data + LIVE_BMS_LEFT_CURRENT_OFFSET) / -100.0f;
        euc_.bms.right.current = read_s16_be(data + LIVE_BMS_RIGHT_CURRENT_OFFSET) / -100.0f;
        // headlight_level из LIVE не читаем — байт 70 является младшим байтом bms_left_current
      }
      break;

    case SUBTYPE_BMS_LEFT_CELLS_1_15:
      parse_bms_cells(data, size, BMS_CELLS_BASE, euc_.bms.left, 0, 15);
      break;
    case SUBTYPE_BMS_LEFT_CELLS_16_30:
      parse_bms_cells(data, size, BMS_CELLS_BASE, euc_.bms.left, 15, 15);
      break;
    case SUBTYPE_BMS_LEFT_TEMPS_CELLS_31_36:
      parse_bms_temps_and_cells(data, size, euc_.bms.left, 0);
      break;

    case SUBTYPE_BMS_RIGHT_CELLS_1_15:
      parse_bms_cells(data, size, BMS_CELLS_BASE, euc_.bms.right, 0, 15);
      break;
    case SUBTYPE_BMS_RIGHT_CELLS_16_30:
      parse_bms_cells(data, size, BMS_CELLS_BASE, euc_.bms.right, 15, 15);
      break;
    case SUBTYPE_BMS_RIGHT_TEMPS_CELLS_31_36:
      parse_bms_temps_and_cells(data, size, euc_.bms.right, 1);
      break;

    case SUBTYPE_SETTINGS:
      parse_settings_subtype(data, size);
      break;

    default:
      break;
  }
}

/// Читает count ячеек из data[base_idx..] в bms.cells[cell_offset..]; масштаб ÷1000 → V.
void VeteranComponent::parse_bms_cells(const uint8_t *data, size_t size, size_t base_idx, BMSBlockData &bms,
                                       size_t cell_offset, size_t count) {
  size_t required = base_idx + count * 2;
  if (size < required)
    return;
  for (size_t i = 0; i < count; i++)
    bms.cells[cell_offset + i] = read_u16_be(data + base_idx + i * 2) / 1000.0f;
}

/// Парсит 6 температур и ячейки 31–36 (42S: +37–42); публикует BMS temp сенсоры.
void VeteranComponent::parse_bms_temps_and_cells(const uint8_t *data, size_t size, BMSBlockData &bms, int side) {
  constexpr size_t NUM_LAST_CELLS = 6;
  if (size < BMS_TEMPS_BASE + BMSBlockData::NUM_TEMPS * 2 + NUM_LAST_CELLS * 2)
    return;
  for (size_t i = 0; i < BMSBlockData::NUM_TEMPS; i++)
    bms.temps[i] = read_u16_be(data + BMS_TEMPS_BASE + i * 2) / 100.0f;
  for (size_t i = 0; i < NUM_LAST_CELLS; i++)
    bms.cells[30 + i] = read_u16_be(data + BMS_CELLS_31_BASE + i * 2) / 1000.0f;
  // 42S: при длине пакета ≥ 83 байт — ячейки 37–42
  if (size >= BMS_CELLS_37_BASE + NUM_LAST_CELLS * 2) {
    for (size_t i = 0; i < NUM_LAST_CELLS; i++)
      bms.cells[36 + i] = read_u16_be(data + BMS_CELLS_37_BASE + i * 2) / 1000.0f;
  }
  // Температуры публикуются через publish_state_from_euc() по общему throttle.
}

/// Парсит Settings: headlight, low_power_mode, high_speed_mode, cut_off, tho_ra, charging_stop_voltage.
void VeteranComponent::parse_settings_subtype(const uint8_t *data, size_t size) {
  if (size < SETTINGS_MIN_SIZE)
    return;
  euc_.headlight = data[SETTINGS_HEADLIGHT_OFFSET] == 0x01;
  // 0x01 = включён, 0x00 = выключен, 0x80 = не поддерживается устройством (игнорируем)
  if (data[SETTINGS_LOW_POWER_OFFSET] != 0x80)
    euc_.low_power_mode = data[SETTINGS_LOW_POWER_OFFSET] == 0x01;
  if (data[SETTINGS_HIGH_SPEED_OFFSET] != 0x80)
    euc_.high_speed_mode = data[SETTINGS_HIGH_SPEED_OFFSET] == 0x01;
  euc_.cut_off_angle = data[SETTINGS_CUT_OFF_OFFSET];
  euc_.tho_ra = data[SETTINGS_THO_RA_OFFSET];
  euc_.charging_stop_voltage = (uint16_t)(read_u16_be(data + SETTINGS_CHARGE_V_OFFSET) + (int)charge_stop_voltage_offset_);
}

/// Синхронизирует euc_ с сенсорами: voltage, mileage, temps, BMS, headlight, режимы и т.д.
void VeteranComponent::publish_state_from_euc() {
  if (sensor_voltage_ != nullptr)
    sensor_voltage_->publish_state(euc_.voltage / 100.0f);
  if (sensor_mileage_current_ != nullptr)
    sensor_mileage_current_->publish_state(euc_.mileage_current);
  if (sensor_mileage_total_ != nullptr)
    sensor_mileage_total_->publish_state(euc_.mileage_total);
  if (sensor_temperature_motor_ != nullptr)
    sensor_temperature_motor_->publish_state(euc_.temperature_motor);
  if (sensor_auto_off_ != nullptr)
    sensor_auto_off_->publish_state(euc_.auto_off < AUTO_OFF_DISABLED_THRESHOLD ? (float)euc_.auto_off : NAN);
  if (binary_sensor_charging_ != nullptr)
    binary_sensor_charging_->publish_state(euc_.charging);
  if (text_sensor_firmware_version_ != nullptr)
    text_sensor_firmware_version_->publish_state(euc_.firmware_version);
  if (sensor_battery_percentage_ != nullptr)
    sensor_battery_percentage_->publish_state(compute_battery_percentage(euc_.voltage));

  if (sensor_bms_left_current_ != nullptr)
    sensor_bms_left_current_->publish_state(euc_.bms.left.current);
  if (sensor_bms_right_current_ != nullptr)
    sensor_bms_right_current_->publish_state(euc_.bms.right.current);
  for (size_t i = 0; i < BMSBlockData::NUM_TEMPS; i++) {
    if (bms_temp_sensors_[0][i] != nullptr)
      bms_temp_sensors_[0][i]->publish_state(euc_.bms.left.temps[i]);
    if (bms_temp_sensors_[1][i] != nullptr)
      bms_temp_sensors_[1][i]->publish_state(euc_.bms.right.temps[i]);
  }
  if (sensor_power_ != nullptr) {
    float power = euc_.voltage * (euc_.bms.left.current + euc_.bms.right.current) / 100.0f;
    sensor_power_->publish_state(power);
  }
  if (sensor_temperature_controller_ != nullptr)
    sensor_temperature_controller_->publish_state(euc_.temperature_controller);

  if (switch_lights_ != nullptr)
    switch_lights_->publish_state(euc_.headlight);
  if (binary_sensor_headlight_ != nullptr)
    binary_sensor_headlight_->publish_state(euc_.headlight);
  if (binary_sensor_high_speed_mode_ != nullptr)
    binary_sensor_high_speed_mode_->publish_state(euc_.high_speed_mode);
  if (binary_sensor_low_power_mode_ != nullptr)
    binary_sensor_low_power_mode_->publish_state(euc_.low_power_mode);
  if (sensor_tho_ra_ != nullptr)
    sensor_tho_ra_->publish_state(euc_.tho_ra);
  if (max_charging_voltage_number_ != nullptr)
    max_charging_voltage_number_->publish_state(euc_.charging_stop_voltage / 10.0f);
}

/// Логирует исходящий BLE-пакет (label, размер, hex).
static void log_ble_out(const char *label, const std::vector<uint8_t> &packet) {
  ESP_LOGD(TAG, "BLE out [%s] size=%zu hex=%s", label, packet.size(), format_hex_pretty(packet).c_str());
}

/// Формирует пакет включения фары: два чанка 4C6B/4C64 + CRC.
std::vector<uint8_t> VeteranComponent::get_headlight_on_packet() const {
  const uint8_t chunk1[9] = {0x4C, 0x6B, 0x41, 0x70, 0x0D, 0x01, 0x80, 0x80, 0x01};
  const uint8_t chunk2[9] = {0x4C, 0x64, 0x41, 0x70, 0x0D, 0x01, 0x00, 0x80, 0x01};
  std::vector<uint8_t> out;
  out.reserve(26);
  append_settings_chunk(out, chunk1, 9);
  append_settings_chunk(out, chunk2, 9);
  return out;
}

/// Формирует пакет выключения фары: два чанка 4C6B/4C64 + CRC.
std::vector<uint8_t> VeteranComponent::get_headlight_off_packet() const {
  const uint8_t chunk1[9] = {0x4C, 0x6B, 0x41, 0x70, 0x0D, 0x01, 0x80, 0x80, 0x00};
  const uint8_t chunk2[9] = {0x4C, 0x64, 0x41, 0x70, 0x0D, 0x01, 0x00, 0x80, 0x00};
  std::vector<uint8_t> out;
  out.reserve(26);
  append_settings_chunk(out, chunk1, 9);
  append_settings_chunk(out, chunk2, 9);
  return out;
}

/// Линейная интерполяция: v_full=nominal, v_empty=nominal*3/4.2; clamp 0..100.
float VeteranComponent::compute_battery_percentage(uint16_t voltage_raw) const {
  float v = voltage_raw / 100.0f;
  float v_full = nominal_voltage_;
  float v_empty = nominal_voltage_ * (3.0f / 4.2f);
  if (v >= v_full)
    return 100.0f;
  if (v <= v_empty)
    return 0.0f;
  float pct = (v - v_empty) / (v_full - v_empty) * 100.0f;
  return pct < 0.0f ? 0.0f : (pct > 100.0f ? 100.0f : pct);
}

/// Формирует пакет установки max charging voltage. Байт 24 = (voltage - offset) * 10, clamp 0..255.
std::vector<uint8_t> VeteranComponent::get_charge_packet(float voltage) {
  uint8_t payload[25] = {
    0x4C, 0x64, 0x41, 0x70, 0x1D, 0x01, 0x02,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x00
  };
  float v = (voltage - charge_voltage_offset_) * 10.0f;
  int clamped = (v < 0) ? 0 : ((v > 255) ? 255 : (int)(v + 0.5f));
  payload[24] = (uint8_t)clamped;
  std::vector<uint8_t> out;
  out.reserve(29);
  append_settings_chunk(out, payload, 25);
  log_ble_out("charge_packet", out);
  return out;
}

/// Проверяет CRC32: последние 4 байта big-endian должны совпадать с esp_crc32_le над data[0..size-4).
bool VeteranComponent::check_crc32(const uint8_t *data, size_t size) {
  if (size < 4)
    return false;
  size_t data_len = size - 4;
  uint32_t crc_calc = esp_crc32_le(0, data, data_len);
  uint32_t crc_recv = (uint32_t)(data[data_len] << 24) | (data[data_len + 1] << 16) | (data[data_len + 2] << 8) |
                      data[data_len + 3];
  if (crc_recv == crc_calc)
    return true;
  ESP_LOGW(TAG, "CRC mismatch recv=%08" PRIX32 " calc=%08" PRIX32, crc_recv, crc_calc);
  return false;
}

}  // namespace esphome::veteran
