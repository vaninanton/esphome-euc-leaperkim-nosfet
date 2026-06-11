# Copyright 2025 <Tony V>
"""
Единая модель сущностей: одна структура данных описывает ключ, тип, setter и дефолты.
Схема и to_code генерируются из неё — меньше дублирования и ошибок.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, number, sensor, switch, text_sensor
from esphome.const import CONF_ID

AUTO_LOAD = ["binary_sensor", "number", "sensor", "text_sensor"]

my_ns = cg.esphome_ns.namespace("veteran")
VeteranComponent = my_ns.class_("VeteranComponent", cg.Component)


def _sensor_defaults(name, icon=None, device_class=None, state_class="measurement",
                     unit_of_measurement=None, accuracy_decimals=2):
    d = {"name": name}
    if icon:
        d["icon"] = icon
    if device_class:
        d["device_class"] = device_class
    if state_class:
        d["state_class"] = state_class
    if unit_of_measurement is not None:
        d["unit_of_measurement"] = unit_of_measurement
    if accuracy_decimals is not None:
        d["accuracy_decimals"] = accuracy_decimals
    return d


# Один источник правды: ключ в YAML → тип, setter в C++, дефолты для схемы
ENTITY_REGISTRY = [
    # (yaml_key, entity_type, setter_name, defaults_dict)
    ("charging", "binary", "set_binary_sensor_charging", {"name": "Charging", "icon": "mdi:power-plug-battery", "device_class": "battery_charging"}),
    ("low_power_mode", "binary", "set_binary_sensor_low_power_mode", {"name": "Low power mode"}),
    ("high_speed_mode", "binary", "set_binary_sensor_high_speed_mode", {"name": "High speed mode"}),
    ("firmware_version", "text", "set_text_sensor_firmware_version", {"name": "Firmware Version", "icon": "mdi:cellphone-arrow-down"}),
    ("auto_off", "sensor", "set_sensor_auto_off", _sensor_defaults("Auto Off", "mdi:progress-clock", "duration", "measurement", "sec", 0)),
    ("battery_percentage", "sensor", "set_sensor_battery_percentage", _sensor_defaults("Battery", "mdi:battery", "battery", "measurement", "%", 1)),
    ("bms_left_current", "sensor", "set_sensor_bms_left_current", _sensor_defaults("BMS Left Current", "mdi:current-ac", "current", "measurement", "A", 3)),
    ("bms_right_current", "sensor", "set_sensor_bms_right_current", _sensor_defaults("BMS Right Current", "mdi:current-ac", "current", "measurement", "A", 3)),
    ("power", "sensor", "set_sensor_power", _sensor_defaults("Power", "mdi:lightning-bolt", "power", "measurement", "W", 3)),
    ("temperature_motor", "sensor", "set_sensor_temperature_motor", _sensor_defaults("Temperature motor", "mdi:thermometer", "temperature", "measurement", "°C", 2)),
    ("temperature_controller", "sensor", "set_sensor_temperature_controller", _sensor_defaults("Temperature controller", "mdi:thermometer", "temperature", "measurement", "°C", 2)),
    ("tho_ra", "sensor", "set_sensor_tho_ra", _sensor_defaults("Tho_ra", state_class="measurement", unit_of_measurement="%", accuracy_decimals=0)),
    ("mileage_current", "sensor", "set_sensor_mileage_current", _sensor_defaults("Mileage current", "mdi:map-marker-path", "distance", "measurement", "km", 2)),
    ("mileage_total", "sensor", "set_sensor_mileage_total", _sensor_defaults("Mileage total", "mdi:map-marker-distance", "distance", "measurement", "km", 2)),
    ("voltage", "sensor", "set_sensor_voltage", _sensor_defaults("Voltage", "mdi:flash", "voltage", "measurement", "V", 2)),
    ("headlight", "binary", "set_binary_sensor_headlight", {"name": "Headlight", "icon": "mdi:lightbulb"}),
]
for i in range(1, 7):
    ENTITY_REGISTRY.append((
        f"bms_left_temp_{i}", "sensor", f"set_sensor_bms_left_temp_{i}",
        _sensor_defaults(f"BMS Left Temperature {i}", "mdi:thermometer", "temperature", "measurement", "°C", 2),
    ))
    ENTITY_REGISTRY.append((
        f"bms_right_temp_{i}", "sensor", f"set_sensor_bms_right_temp_{i}",
        _sensor_defaults(f"BMS Right Temperature {i}", "mdi:thermometer", "temperature", "measurement", "°C", 2),
    ))

SENSOR_KEYS = [r[0] for r in ENTITY_REGISTRY if r[1] == "sensor"]
BINARY_KEYS = [r[0] for r in ENTITY_REGISTRY if r[1] == "binary"]
TEXT_KEYS = [r[0] for r in ENTITY_REGISTRY if r[1] == "text"]
ALL_ENTITY_KEYS = [r[0] for r in ENTITY_REGISTRY]

DEFAULTS_BY_KEY = {r[0]: r[3] for r in ENTITY_REGISTRY}
SETTER_BY_KEY = {r[0]: r[2] for r in ENTITY_REGISTRY}
TYPE_BY_KEY = {r[0]: r[1] for r in ENTITY_REGISTRY}


def _merge_entity_config(key, defaults_dict):
    def validator(value):
        out = dict(defaults_dict)
        if value is not None:
            out.update(value)
        if CONF_ID not in out:
            out[CONF_ID] = key
        return out
    return validator


def _default_name(key):
    return DEFAULTS_BY_KEY.get(key, {}).get("name", key)


def _fill_missing_entity_keys(config):
    """Все сущности из реестра по умолчанию создаются; отсутствующие ключи получают {}."""
    result = dict(config)
    for key in ALL_ENTITY_KEYS:
        if key not in result:
            result[key] = {}
    return result


def _expand_id_prefix(config):
    """Подставляет id/name с префиксом и device_id. device_id — привязка к устройству в HA; id_prefix — префикс для id и опционально имени."""
    prefix = config.get("id_prefix")
    device_id = config.get("device_id")
    result = dict(config)
    if not prefix and not device_id:
        return result
    name_prefix = (prefix or "").capitalize() if not device_id else ""
    for key in ALL_ENTITY_KEYS:
        entry = result.get(key)
        if entry is None:
            result[key] = {}
            entry = {}
        if not isinstance(entry, dict):
            continue
        entry = dict(entry)
        if prefix:
            entry[CONF_ID] = entry.get(CONF_ID, f"{prefix}_{key}")
        base_name = entry.get("name", _default_name(key))
        entry["name"] = f"{name_prefix} {base_name}".strip() if name_prefix else base_name
        if device_id is not None:
            entry["device_id"] = device_id
        result[key] = entry
    return result


CONF_NOMINAL_VOLTAGE = "nominal_voltage"
CONF_CELL_COUNT = "cell_count"
CONF_CHARGE_VOLTAGE_OFFSET = "charge_voltage_offset"
CONF_CHARGE_STOP_VOLTAGE_OFFSET = "charge_stop_voltage_offset"
CONF_SORTING_GROUP_ID = "sorting_group_id"
# Дефолты web_server для веб-интерфейса: (ключ конфига группы, sorting_weight). Заполняется при наличии sorting_group_id.
WEB_SERVER_DEFAULTS = {
    "voltage": (CONF_SORTING_GROUP_ID, 10),
    "battery_percentage": (CONF_SORTING_GROUP_ID, 11),
    "charging": (CONF_SORTING_GROUP_ID, 12),
    "power": (CONF_SORTING_GROUP_ID, 13),
    "mileage_current": (CONF_SORTING_GROUP_ID, 20),
    "mileage_total": (CONF_SORTING_GROUP_ID, 21),
    "temperature_motor": (CONF_SORTING_GROUP_ID, 22),
    "temperature_controller": (CONF_SORTING_GROUP_ID, 23),
    "auto_off": (CONF_SORTING_GROUP_ID, 30),
    "tho_ra": (CONF_SORTING_GROUP_ID, 31),
    "low_power_mode": (CONF_SORTING_GROUP_ID, 40),
    "high_speed_mode": (CONF_SORTING_GROUP_ID, 41),
    "firmware_version": (CONF_SORTING_GROUP_ID, 42),
    # 0:  Connected (binary_sensor, package-veteran-device.yaml)
    # 51: Lights switch (package-veteran-device.yaml)
    # 52: Max charging voltage number (package-veteran-device.yaml)
    "headlight": (CONF_SORTING_GROUP_ID, 53),
    "bms_left_current": (CONF_SORTING_GROUP_ID, 60),
    "bms_right_current": (CONF_SORTING_GROUP_ID, 61),
    # 70..75: bms_left_temp_1..6
    # 80..85: bms_right_temp_1..6
}
for i in range(1, 7):
    WEB_SERVER_DEFAULTS[f"bms_left_temp_{i}"] = (CONF_SORTING_GROUP_ID, 69 + i)
    WEB_SERVER_DEFAULTS[f"bms_right_temp_{i}"] = (CONF_SORTING_GROUP_ID, 79 + i)


def _inject_web_server_defaults(config):
    """Если заданы sorting_group_id / _bms — подставить web_server сущностям, у которых его нет."""
    result = dict(config)
    for key in ALL_ENTITY_KEYS:
        ws = WEB_SERVER_DEFAULTS.get(key)
        if not ws:
            continue
        group_attr, weight = ws
        group_id = result.get(group_attr)
        if not group_id:
            continue
        entry = result.get(key)
        if not isinstance(entry, dict) or "web_server" in entry:
            continue
        result[key] = {**entry, "web_server": {"sorting_group_id": group_id, "sorting_weight": weight}}
    return result


SINGLE_VETERAN_SCHEMA = cv.All(
    _fill_missing_entity_keys,
    _expand_id_prefix,
    _inject_web_server_defaults,
    cv.Schema({
        cv.GenerateID(): cv.declare_id(VeteranComponent),
        cv.Optional("id_prefix"): cv.string,
        cv.Optional("device_id"): cv.string,
        cv.Optional(CONF_SORTING_GROUP_ID): cv.string,
        cv.Optional(CONF_NOMINAL_VOLTAGE, default=151.2): cv.float_range(90.0, 200.0),
        cv.Optional(CONF_CELL_COUNT, default=36): cv.int_range(min=1, max=42),
        cv.Optional(CONF_CHARGE_VOLTAGE_OFFSET): cv.float_range(100.0, 180.0),
        cv.Optional(CONF_CHARGE_STOP_VOLTAGE_OFFSET, default=682): cv.float_range(-100.0, 1000.0),
        cv.Optional("max_charging_voltage_id"): cv.use_id(number.Number),
        cv.Optional("switch_lights_id"): cv.use_id(switch.Switch),
        **{
            cv.Optional(k): cv.All(
                _merge_entity_config(k, DEFAULTS_BY_KEY[k]),
                sensor.sensor_schema().extend(cv.COMPONENT_SCHEMA),
            )
            for k in SENSOR_KEYS
        },
        **{
            cv.Optional(k): cv.All(
                _merge_entity_config(k, DEFAULTS_BY_KEY[k]),
                binary_sensor.binary_sensor_schema().extend(cv.COMPONENT_SCHEMA),
            )
            for k in BINARY_KEYS
        },
        **{
            cv.Optional(k): cv.All(
                _merge_entity_config(k, DEFAULTS_BY_KEY[k]),
                text_sensor.text_sensor_schema().extend(cv.COMPONENT_SCHEMA),
            )
            for k in TEXT_KEYS
        },
    }).extend(cv.COMPONENT_SCHEMA),
)

CONFIG_SCHEMA = cv.ensure_list(SINGLE_VETERAN_SCHEMA)


async def _to_code_one(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_nominal_voltage(config[CONF_NOMINAL_VOLTAGE]))
    cg.add(var.set_cell_count(config[CONF_CELL_COUNT]))
    if CONF_CHARGE_VOLTAGE_OFFSET in config:
        cg.add(var.set_charge_voltage_offset(config[CONF_CHARGE_VOLTAGE_OFFSET]))
    cg.add(var.set_charge_stop_voltage_offset(config[CONF_CHARGE_STOP_VOLTAGE_OFFSET]))

    for key in BINARY_KEYS:
        sens = await binary_sensor.new_binary_sensor(config[key])
        cg.add(getattr(var, SETTER_BY_KEY[key])(sens))

    for key in SENSOR_KEYS:
        sens = await sensor.new_sensor(config[key])
        cg.add(getattr(var, SETTER_BY_KEY[key])(sens))

    for key in TEXT_KEYS:
        sens = await text_sensor.new_text_sensor(config[key])
        cg.add(getattr(var, SETTER_BY_KEY[key])(sens))

    if "max_charging_voltage_id" in config:
        num = await cg.get_variable(config["max_charging_voltage_id"])
        cg.add(var.set_max_charging_voltage_number(num))
    if "switch_lights_id" in config:
        sw = await cg.get_variable(config["switch_lights_id"])
        cg.add(var.set_switch_lights(sw))


async def to_code(config):
    for one in config:
        await _to_code_one(one)
