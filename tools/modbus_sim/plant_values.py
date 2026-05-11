from __future__ import annotations

from dataclasses import dataclass
import math

from edge_template import ModbusNode, group_by_unit
from profiles import profile_override


@dataclass
class RegisterValue:
    node: ModbusNode
    offset: int
    value: int


def _clamp(value: float, low: float, high: float) -> float:
    return max(low, min(high, value))


def _phase(node: ModbusNode) -> float:
    key = f"{node.unit}:{node.register}:{node.asset}:{node.node_id}"
    return float(sum((index + 1) * ord(char) for index, char in enumerate(key)) % 628) / 100.0


def _wave(node: ModbusNode, tick: int, period: float = 900.0) -> float:
    return math.sin((float(tick) / period) * math.tau + _phase(node))


def _asset_number(asset: str) -> int:
    digits = ""
    for char in reversed(asset):
        if char.isdigit():
            digits = char + digits
        elif digits:
            break
    return int(digits) if digits else 1


def _vary(node: ModbusNode, tick: int, base: float, amplitude: float, period: float = 900.0) -> float:
    return base + amplitude * _wave(node, tick, period)


def _pump_flow(node: ModbusNode, tick: int) -> float:
    asset = node.asset
    pump_index = _asset_number(asset)
    if asset.startswith("intake_pump"):
        return _vary(node, tick, 16200.0 - pump_index * 250.0, 550.0, 1100.0)
    if asset.startswith("hp_pump"):
        return _vary(node, tick, 10800.0 - (pump_index % 4) * 90.0, 320.0, 900.0)
    if asset.startswith("dist_pump"):
        return _vary(node, tick, 6400.0 - pump_index * 180.0, 260.0, 1000.0)
    if asset == "lime_saturator":
        return _vary(node, tick, 240.0, 12.0, 1300.0)
    if asset == "antiscalant_skid":
        return _vary(node, tick, 7.5, 0.4, 1500.0)
    if asset == "brine_outfall":
        return _vary(node, tick, 17600.0, 450.0, 950.0)
    return _vary(node, tick, 1200.0, 60.0, 1200.0)


def _engineering_value(node: ModbusNode, tick: int, seed: int) -> float:
    shifted_tick = tick + seed * 37
    asset = node.asset
    node_id = node.node_id
    train_index = _asset_number(asset)

    if node_id == "tide_level":
        return _vary(node, shifted_tick, 1.75, 0.42, 1800.0)
    if node_id in {"raw_temperature", "temperature"}:
        return _vary(node, shifted_tick, 24.0, 1.2, 2200.0)
    if node_id == "raw_conductivity":
        return _vary(node, shifted_tick, 53000.0, 950.0, 1600.0)
    if node_id == "conductivity":
        return _vary(node, shifted_tick, 76000.0, 1600.0, 1500.0)
    if node_id == "debris_load_pct":
        return _clamp(_vary(node, shifted_tick, 42.0, 9.0, 700.0), 0.0, 100.0)
    if node_id in {"level_pct", "tank_level"}:
        return _clamp(_vary(node, shifted_tick, 68.0, 8.0, 1300.0), 0.0, 100.0)
    if node_id == "level_meters":
        return _vary(node, shifted_tick, 8.4, 0.5, 1300.0)
    if node_id == "sludge_level":
        return _vary(node, shifted_tick, 1.15, 0.12, 1100.0)
    if node_id == "flow_rate":
        return _pump_flow(node, shifted_tick)
    if node_id == "recycle_flow":
        return _vary(node, shifted_tick, 4200.0, 180.0, 950.0)
    if node_id == "permeate_flow":
        return _vary(node, shifted_tick, 4850.0 - train_index * 35.0, 140.0, 1050.0)
    if node_id in {"differential_pressure", "headloss", "stage1_dp", "stage2_dp"}:
        if asset.startswith("cartridge_housing"):
            return _vary(node, shifted_tick, 0.78, 0.08, 800.0)
        if asset.startswith("filter_cell"):
            return _vary(node, shifted_tick, 0.62, 0.08, 850.0)
        if asset.startswith("band_screen"):
            return _vary(node, shifted_tick, 0.16, 0.03, 750.0)
        return _vary(node, shifted_tick, 1.05, 0.10, 900.0)
    if node_id == "sdi":
        return _vary(node, shifted_tick, 2.4, 0.25, 1000.0)
    if node_id == "turbidity_out":
        return _vary(node, shifted_tick, 0.08, 0.02, 650.0)
    if node_id in {"discharge_pressure", "feed_pressure", "hp_in_pressure"}:
        if asset.startswith("hp_pump") or asset.startswith("ro_train") or asset.startswith("erd_skid"):
            return _vary(node, shifted_tick, 64.5, 1.6, 850.0)
        if asset.startswith("intake_pump"):
            return _vary(node, shifted_tick, 2.6, 0.12, 900.0)
        if asset.startswith("dist_pump"):
            return _vary(node, shifted_tick, 5.4, 0.22, 900.0)
        if asset.startswith("brine_booster"):
            return _vary(node, shifted_tick, 17.5, 0.8, 950.0)
        return _vary(node, shifted_tick, 3.0, 0.2, 900.0)
    if node_id in {"suction_pressure", "lp_out_pressure"}:
        return _vary(node, shifted_tick, 2.35, 0.16, 900.0)
    if node_id == "motor_current":
        if asset.startswith("hp_pump"):
            return _vary(node, shifted_tick, 535.0, 28.0, 780.0)
        if asset.startswith("intake_pump"):
            return _vary(node, shifted_tick, 210.0, 12.0, 850.0)
        if asset.startswith("dist_pump"):
            return _vary(node, shifted_tick, 165.0, 10.0, 870.0)
        if asset.startswith("brine_booster"):
            return _vary(node, shifted_tick, 145.0, 8.0, 820.0)
        return _vary(node, shifted_tick, 80.0, 5.0, 850.0)
    if node_id == "motor_voltage":
        return _vary(node, shifted_tick, 4160.0, 18.0, 1400.0)
    if node_id == "vibration":
        return _vary(node, shifted_tick, 2.4, 0.25, 700.0)
    if node_id == "bearing_temperature":
        return _vary(node, shifted_tick, 58.0, 2.5, 1000.0)
    if node_id == "permeate_conductivity":
        return _vary(node, shifted_tick, 210.0 + train_index * 7.0, 18.0, 1150.0)
    if node_id == "recovery_pct":
        return _clamp(_vary(node, shifted_tick, 45.0, 1.2, 1200.0), 40.5, 48.0)
    if node_id == "salt_rejection_pct":
        return _clamp(_vary(node, shifted_tick, 99.55, 0.08, 1300.0), 98.8, 99.8)
    if node_id == "efficiency_pct":
        return _clamp(_vary(node, shifted_tick, 96.0, 0.5, 1100.0), 90.0, 98.0)
    if node_id == "ph_out":
        return _vary(node, shifted_tick, 8.15, 0.08, 1250.0)
    if node_id == "hardness":
        return _vary(node, shifted_tick, 68.0, 4.0, 1350.0)
    if node_id == "cl2_residual":
        return _vary(node, shifted_tick, 1.15, 0.12, 1000.0)
    if node_id in {"dose_rate", "fecl3_dose_rate"}:
        if asset == "antiscalant_skid":
            return _vary(node, shifted_tick, 18.0, 1.0, 1000.0)
        if asset == "naocl_skid":
            return _vary(node, shifted_tick, 11.5, 0.8, 1000.0)
        if asset == "chlorination_skid":
            return _vary(node, shifted_tick, 12.0, 0.7, 1000.0)
        return _vary(node, shifted_tick, 36.0, 2.0, 900.0)
    if node_id == "total_kw":
        return _vary(node, shifted_tick, 58500.0, 1800.0, 1000.0)
    if node_id == "kwh_per_m3":
        return _vary(node, shifted_tick, 3.75, 0.12, 1000.0)

    return float((node.unit * 1000) + (node.register - 40000))


def _double_raw_from_engineering(node: ModbusNode, value: float) -> int:
    scale = node.scale if node.scale != 0.0 else 1.0
    raw = int(round(value / scale))
    return int(_clamp(float(raw), -32768.0, 32767.0)) & 0xFFFF


def base_register_value(node: ModbusNode, tick: int, seed: int) -> int:
    if node.data_type == "bool":
        if node.node_id.startswith("command_") or node.node_id == "backwash_running":
            return 0
        if node.node_id in {"rake_running", "mixer_running"}:
            return 1
        return 1

    if node.data_type == "int16":
        if node.node_id == "status":
            return 1
        return int(_clamp(float(node.unit * 100 + node.offset), -32768.0, 32767.0)) & 0xFFFF

    if node.data_type == "double":
        return _double_raw_from_engineering(node, _engineering_value(node, tick, seed))

    if node.data_type in {"int32", "uint32"}:
        if node.node_id == "speed_rpm":
            return int(round(_vary(node, tick + seed * 37, 2980.0, 35.0, 900.0))) & 0xFFFFFFFF
        raw = node.unit * 100000 + node.register + tick + seed
        return raw & 0xFFFFFFFF

    raw = node.unit * 100000 + node.register + tick + seed
    return raw & 0xFFFF


def value_for_node(node: ModbusNode, tick: int, seed: int, profile: str) -> int:
    override = profile_override(profile, node)
    if override is not None:
        return override
    return base_register_value(node, tick, seed)


class PlantRegisters:
    def __init__(self, nodes: list[ModbusNode], seed: int = 0) -> None:
        self.nodes = nodes
        self.seed = seed
        self.units = group_by_unit(nodes)
        self.written: dict[tuple[int, int], int] = {}

    def max_offset_by_unit(self) -> dict[int, int]:
        result: dict[int, int] = {}
        for unit, nodes in self.units.items():
            result[unit] = max(node.offset + node.width - 1 for node in nodes)
        return result

    def _node_written_value(self, node: ModbusNode) -> int | None:
        key = (node.unit, node.offset)
        if not node.writable or key not in self.written:
            return None
        if node.width == 1:
            return self.written[key]
        high = self.written.get(key, 0)
        low = self.written.get((node.unit, node.offset + 1), 0)
        return ((high & 0xFFFF) << 16) | (low & 0xFFFF)

    @staticmethod
    def _register_values_for_raw(node: ModbusNode, raw: int) -> list[tuple[int, int]]:
        if node.width == 1:
            return [(node.offset, raw & 0xFFFF)]
        return [
            (node.offset, (raw >> 16) & 0xFFFF),
            (node.offset + 1, raw & 0xFFFF),
        ]

    def values(self, tick: int, profile: str) -> dict[int, list[RegisterValue]]:
        result: dict[int, list[RegisterValue]] = {}
        for unit, nodes in self.units.items():
            unit_values: list[RegisterValue] = []
            for node in nodes:
                raw = self._node_written_value(node)
                if raw is None:
                    raw = value_for_node(node, tick, self.seed, profile)
                for offset, value in self._register_values_for_raw(node, raw):
                    unit_values.append(RegisterValue(node=node, offset=offset, value=value))
            result[unit] = unit_values
        return result

    def note_write(self, unit: int, offset: int, value: int) -> None:
        self.written[(unit, offset)] = value & 0xFFFF
