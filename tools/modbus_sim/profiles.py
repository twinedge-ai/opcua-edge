from __future__ import annotations

from edge_template import ModbusNode


PROFILES = {
    "normal",
    "high_vibration",
    "low_suction_pressure",
    "overload_current",
    "ro_quality_drift",
}


def _engineering_to_raw(node: ModbusNode, value: float) -> int:
    if node.scale == 0.0:
        return int(value) & 0xFFFF
    return int(round(value / node.scale)) & 0xFFFF


def profile_override(profile: str, node: ModbusNode) -> int | None:
    if profile == "normal":
        return None

    if profile == "high_vibration":
        if node.asset == "hp_pump_1" and node.node_id == "vibration":
            return _engineering_to_raw(node, 9.2)
        return None

    if profile == "low_suction_pressure":
        if node.asset == "hp_pump_1" and node.node_id == "suction_pressure":
            return _engineering_to_raw(node, 0.7)
        return None

    if profile == "overload_current":
        if node.asset == "hp_pump_1" and node.node_id == "motor_current":
            return _engineering_to_raw(node, 900.0)
        return None

    if profile == "ro_quality_drift":
        if node.asset == "ro_train_1" and node.node_id == "permeate_conductivity":
            return _engineering_to_raw(node, 620.0)
        return None

    raise ValueError(f"unknown profile: {profile}")
