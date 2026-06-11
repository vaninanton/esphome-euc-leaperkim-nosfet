# Veteran / NOSFET BLE Protocol

Протокол BLE для моноколёс Leaperkim Veteran и NOSFET. Реализован в `my_components/veteran/`.

## Источники

- [Таблица разбора пакетов (Google Sheets)](https://docs.google.com/spreadsheets/d/1AFi8-H1jUv8p0RsQlEnBhRtjO1Obn8OK9MPmcfamgvE/edit?usp=sharing)
- Официальное приложение: `com.laoniao.leaperkim` (тот же пакет у NOSFET). Декомпиляция — разд. 9.2; сверены сборки NOSFET 1.1.3 и последняя `com.laoniao.leaperkim`. Старая версия 1.2.1 (`com.lk.lktech`) — разд. 9.1.
- [EUC Charging Monitor](https://github.com/githuba42r/euc_charging) — декодер Veteran, [PROTOCOL_DIAGRAMS.md](https://github.com/githuba42r/euc_charging/blob/eb8c68ba1e1609204928a286723bd10391499e62/PROTOCOL_DIAGRAMS.md)
- [FreeWheel](https://github.com/nathan234/FreeWheel) — KMP-приложение, декодер Veteran (основной источник по Settings и расширенному payload)
- [Wheellog.Android](https://github.com/Wheellog/Wheellog.Android) — VeteranAdapter
- Живые BLE-дампы: Veteran Lynx (36S, fw 43.2.54) и NOSFET Aero (30S, fw 43.2.54) — подтверждены `tools/ble_dump_veteran.py`, `tools/ble_unknown_bytes.py`

---

## Подключение

- **BLE сервис**: `FFE0`
- **Характеристика**: `FFE1` (notify + write)
- Колесо само шлёт пакеты телеметрии; команды отправляются туда же

---

## Структура входящего пакета

```
[0..2]   Header:      DC 5A 5C
[3]      Length byte: размер оставшейся части (payload + 4 байта CRC)
[4..N-5] Payload
[N-4..N-1] CRC32 LE, big-endian
```

Минимальный размер полного пакета: **36 байт** (3 + 1 + 32).

### CRC32

Алгоритм: `esp_crc32_le(0, data, size - 4)`.  
Результат: 4 байта big-endian в конце пакета.

---

## Common Payload (offset 4–35, 32 байта)

Присутствует во всех пакетах. Offset — абсолютный от начала пакета.

| Offset | Тип    | Поле                | Масштаб          | Статус     |
|--------|--------|---------------------|------------------|------------|
| 4      | u16 BE | voltage             | ÷100 → V         | ✓ парсим   |
| 6      | s16 BE | speed               | ÷10 → km/h       | источники  |
| 8      | u32 mid-LE | mileage_current | ÷1000 → km       | ✓ парсим   |
| 12     | u32 mid-LE | mileage_total   | ÷1000 → km       | ✓ парсим   |
| 16     | s16 BE | phase_current       | raw              | источники  |
| 18     | u16 BE | temperature_motor   | ÷100 → °C        | ✓ парсим   |
| 20     | u16 BE | auto_off            | сек; ≥900=выкл   | ✓ парсим   |
| 22     | u16 BE | charging            | ≠0 = заряжается  | ✓ парсим   |
| 24     | u16 BE | speed_alert         | ÷10 → km/h       | источники  |
| 26     | u16 BE | speed_tiltback      | ÷10 → km/h       | источники  |
| 28     | u16 BE | firmware            | modelVersion=÷1000 | ✓ парсим |
| 30     | u16 BE | pedals_mode         | 0/1/2            | источники  |
| 32     | s16 BE | pitch_angle         | ÷100 → °         | источники  |
| 34     | u16 BE | pwm                 | raw              | источники  |

**u32 mid-LE**: `(b[2]<<24) | (b[3]<<16) | (b[0]<<8) | b[1]` — word-swap порядок.  
**Версия прошивки**: `snprintf("%d.%01d.%02d", fw/1000, (fw/100)%10, fw%100)`.  
**modelVersion** ≥ 5 и size ≥ 47 → присутствует Extended Payload.

### Байты 36–45 (между Common и Extended)

Присутствуют во всех пакетах, большинство = `0x80` (not supported).  
Подтверждено дампами (Aero fw 43.2.54):

| Offset | Значение | Примечание |
|--------|----------|------------|
| 36     | `0x80`   | not supported |
| 37     | `0xC8` (200) | константа, назначение неизвестно |
| 38–39  | `0x00`   | |
| 40–45  | `0x80`   | not supported |

---

## Extended Payload (offset 46+)

Байт `[46]` — sub-type.

### Sub-types

| Sub-type | Константа                        | Размер  | Описание |
|----------|----------------------------------|---------|----------|
| `0x00`   | `SUBTYPE_LIVE`                   | 77 байт | Live: контроллер, BMS токи |
| `0x04`   | `SUBTYPE_LIVE_ALT`               | 77 байт | То же, альтернативный тип |
| `0x01`   | `SUBTYPE_BMS_LEFT_CELLS_1_15`    | 87 байт | BMS left, ячейки 1–15 |
| `0x02`   | `SUBTYPE_BMS_LEFT_CELLS_16_30`   | 87 байт | BMS left, ячейки 16–30 |
| `0x03`   | `SUBTYPE_BMS_LEFT_TEMPS_CELLS_31_36` | 99 байт | BMS left, температуры + ячейки 31–36 |
| `0x05`   | `SUBTYPE_BMS_RIGHT_CELLS_1_15`   | 87 байт | BMS right, ячейки 1–15 |
| `0x06`   | `SUBTYPE_BMS_RIGHT_CELLS_16_30`  | 87 байт | BMS right, ячейки 16–30 |
| `0x07`   | `SUBTYPE_BMS_RIGHT_TEMPS_CELLS_31_36` | 99 байт | BMS right, температуры + ячейки 31–36 |
| `0x08`   | `SUBTYPE_SETTINGS`               | 75 байт | Настройки колеса |

---

### Live (0x00 / 0x04) — 77 байт

| Offset | Тип    | Поле                   | Масштаб      | Статус   |
|--------|--------|------------------------|--------------|----------|
| 59     | u16 BE | temperature_controller | ÷100 → °C    | ✓ парсим |
| 69     | s16 BE | bms.left.current       | ÷ −100 → A   | ✓ парсим |
| 71     | s16 BE | bms.right.current      | ÷ −100 → A   | ✓ парсим |

Байт `[70]` — младший байт `bms.left.current` (s16 BE [69..70]).  
Состояние фары в LIVE-пакете **отсутствует** — приходит только в Settings `[47]`.

**Неизвестные изменяющиеся байты** (дамп Aero, подтверждение нужно):

| Offset | Наблюдаемые значения | Гипотеза |
|--------|----------------------|---------|
| 48–49  | u16be 1768..1797 → 17.7..18.0 | roll/tilt angle? |
| 55     | `0x32` (50) = константа | speed_alert в км/ч? |
| 56     | `0x11` (17) = константа | speed_tiltback в км/ч? |
| 62–63  | u16be 39166..40703 → 391..407 | фазовый ток × 100? |

---

### BMS Cells (0x01 / 0x02 / 0x05 / 0x06) — 87 байт

| Offset       | Тип    | Поле                    | Масштаб     |
|--------------|--------|-------------------------|-------------|
| 53 + i×2     | u16 BE | cells[cell_offset + i]  | ÷1000 → V   |

- `0x01` / `0x05`: i = 0..14, cell_offset = 0 (ячейки 1–15)
- `0x02` / `0x06`: i = 0..14, cell_offset = 15 (ячейки 16–30)

---

### BMS Temps + Cells 31–36 (0x03 / 0x07) — 99 байт

| Offset       | Тип    | Поле               | Масштаб     |
|--------------|--------|--------------------|-------------|
| 47 + i×2     | u16 BE | temps[i], i=0..5   | ÷100 → °C   |
| 59 + i×2     | u16 BE | cells[30+i], i=0..5 | ÷1000 → V  |
| 71 + i×2     | u16 BE | cells[36+i], i=0..5 | ÷1000 → V (42S, size≥83) |

Байт `[94]` меняется (0x03: 34..39, 0x07: 18..22) — возможно ambient temperature (°C), не подтверждено.

---

### Settings (0x08) — 75 байт

Подтверждено живым дампом (Aero fw 43.2.54) и сверкой с FreeWheel.

| Offset | Тип | Поле                    | Значение / Масштаб              | Статус   |
|--------|-----|-------------------------|---------------------------------|----------|
| 47     | u8  | headlight               | `0x01` = вкл, `0x00` = выкл    | ✓ парсим |
| 48     | u8  | —                       | `0x00` константа                | |
| 49     | u8  | —                       | `0x80` = not supported          | |
| 50     | u8  | pedal_hardness          | 0–100%                          | FreeWheel |
| 51     | u8  | —                       | `0x00`                          | |
| 52     | u8  | stop_speed              | км/ч                            | FreeWheel |
| 53     | u8  | pwm_limit               | %                               | FreeWheel |
| 54     | u8  | —                       | константа                       | |
| 55     | u8  | screen_backlight        | 0–100%                          | FreeWheel |
| 56–59  | u8  | —                       | `0x00`                          | |
| 60     | u8  | low_power_mode          | `0x01`=вкл, `0x00`=выкл, `0x80`=не поддерживается | ✓ парсим |
| 61     | u8  | high_speed_mode         | `0x01`=вкл, `0x00`=выкл, `0x80`=не поддерживается | ✓ парсим |
| 62     | u8  | cut_off_angle           | raw                             | ✓ парсим |
| 63     | u16 BE | charging_stop_voltage_raw | см. декодирование ниже       | ✓ парсим |
| 65     | u8  | charge_voltage_base     | base voltage для команды зарядки (Lynx=145, Aero=121) | ✓ дамп |
| 66     | u8  | tho_ra                  | %                               | ✓ парсим |
| 67     | u8  | —                       | `0x80` = not supported          | |
| 68     | u8  | accel_limit             | 0–100%                          | FreeWheel |
| 69–70  | u8  | —                       | `0x80` = not supported          | |

**Декодирование charging_stop_voltage**:
```
charging_stop_voltage = read_u16_be(data + 63) + charge_stop_voltage_offset
```
Финальное значение в V: `charging_stop_voltage / 10.0`.

| Колесо | charge_stop_voltage_offset | charge_voltage_base ([65]) |
|--------|---------------------------|---------------------------|
| Veteran Lynx (36S) | 682 | 145 |
| NOSFET Aero (30S)  | −70 | 121 |

---

## Исходящие команды

Все команды отправляются в `FFE1`.

### Структура чанка

```
[N байт payload] [4 байта CRC32 LE big-endian]
```
CRC: `esp_crc32_le(0, payload, N)`, результат big-endian.

### Фара

Два чанка подряд, итого 26 байт (9+4 + 9+4). Последний байт: `0x01` = вкл, `0x00` = выкл.

**Включить:**
```
4C 6B 41 70 0D 01 80 80 01 + CRC32
4C 64 41 70 0D 01 00 80 01 + CRC32
```

**Выключить:**
```
4C 6B 41 70 0D 01 80 80 00 + CRC32
4C 64 41 70 0D 01 00 80 00 + CRC32
```

Подтверждено: совпадает с FreeWheel `buildVeteranCommand(0x0D)` и живым тестом.

### Максимальное напряжение зарядки

Один чанк 25+4 байта. Подтверждено живым тестом на Lynx и Aero.

```
Payload (25 байт):
  4C 64 41 70 1D 01 02
  80 80 80 80 80 80 80 80 80 80 80
  80 80 80 80 80 80
  [byte24]
+ CRC32 (4 байта)
```

```
byte24 = clamp((voltage - charge_voltage_base) × 10, 0, 255)
```

`charge_voltage_base` = значение из Settings `[65]` конкретного колеса (Lynx=145, Aero=121).

| Колесо | target | byte24 | Результат |
|--------|--------|--------|-----------|
| Lynx (base=145) | 147.0V | 20 | 147.0V ✓ |
| Lynx (base=145) | 151.2V | 62 | 151.2V ✓ |
| Aero (base=121) | 123.0V | 20 | 123.0V ✓ (дамп) |
| Aero (base=121) | 122.0V | 10 | 122.0V ✓ (дамп) |

---

## Расчёт заряда батареи

Линейная интерполяция, подтверждена WheelLog и FreeWheel:

```
v_full  = nominal_voltage
v_empty = nominal_voltage × (3.0 / 4.2)
pct     = clamp((v - v_empty) / (v_full - v_empty) × 100, 0, 100)
```

| Колесо | nominal_voltage | v_empty |
|--------|-----------------|---------|
| Veteran Lynx (36S) | 151.2 V | 108.0 V |
| NOSFET Aero (30S)  | 126.0 V |  90.0 V |

---

## Конфигурация компонента

```yaml
veteran:
  - id: veteran_lynx
    id_prefix: lynx
    device_id: lynx_device
    sorting_group_id: "sorting_group_lynx"
    nominal_voltage: 151.2        # V полного заряда
    cell_count: 36                # количество ячеек
    charge_stop_voltage_offset: 682   # декодирование входящего: Lynx=682, Aero=-70
    charge_voltage_offset: 145        # для исходящей команды: Lynx=145, Aero=121
    max_charging_voltage_id: max_charging_voltage_lynx
    switch_lights_id: euc_lights_lynx
```

Все сенсоры создаются автоматически через `__init__.py`.
