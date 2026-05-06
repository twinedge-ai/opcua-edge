from __future__ import annotations

from dataclasses import dataclass

from edge_template import ModbusNode, group_by_unit
from profiles import profile_override


@dataclass
class RegisterValue:
    node: ModbusNode
    offset: int
    value: int


def base_register_value(node: ModbusNode, tick: int, seed: int) -> int:
    if node.data_type == "bool":
        return (tick + node.unit + node.register + seed) & 1

    raw = node.unit * 100000 + node.register + tick + seed
    if node.data_type in {"int32", "uint32"}:
        return raw & 0xFFFFFFFF
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
