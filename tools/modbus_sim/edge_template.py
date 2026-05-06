from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class ModbusNode:
    asset: str
    node_id: str
    name: str
    data_type: str
    access: str
    unit: int
    register: int
    scale: float

    @property
    def offset(self) -> int:
        return self.register - 40001

    @property
    def writable(self) -> bool:
        return self.access == "read_write"

    @property
    def width(self) -> int:
        return 2 if self.data_type in {"int32", "uint32"} else 1


def _finish_node(raw: dict[str, str], nodes: list[ModbusNode]) -> None:
    if not raw:
        return
    if raw.get("source") != "modbus":
        return

    required = (
        "asset",
        "id",
        "name",
        "data_type",
        "access",
        "modbus_unit",
        "modbus_register",
        "scale",
    )
    for key in required:
        if key not in raw:
            raise ValueError(f"missing node key: {key}")

    unit = int(raw["modbus_unit"])
    register = int(raw["modbus_register"])
    if unit <= 0:
        raise ValueError("modbus_unit must be positive")
    if register < 40001:
        raise ValueError("modbus_register must be >= 40001")

    nodes.append(
        ModbusNode(
            asset=raw["asset"],
            node_id=raw["id"],
            name=raw["name"],
            data_type=raw["data_type"],
            access=raw["access"],
            unit=unit,
            register=register,
            scale=float(raw["scale"]),
        )
    )


def load_modbus_nodes(path: str | Path) -> list[ModbusNode]:
    section = ""
    current_node: dict[str, str] = {}
    nodes: list[ModbusNode] = []

    with Path(path).open("r", encoding="utf-8") as file:
        for raw_line in file:
            line = raw_line.strip()
            if not line or line.startswith("#"):
                continue

            if line.startswith("[") and line.endswith("]"):
                if section == "node":
                    _finish_node(current_node, nodes)
                    current_node = {}
                section = line[1:-1]
                continue

            if "=" not in line:
                raise ValueError(f"invalid template line: {line}")

            key, value = line.split("=", 1)
            if section == "node":
                current_node[key] = value

    if section == "node":
        _finish_node(current_node, nodes)

    return nodes


def group_by_unit(nodes: list[ModbusNode]) -> dict[int, list[ModbusNode]]:
    units: dict[int, list[ModbusNode]] = {}
    for node in nodes:
        units.setdefault(node.unit, []).append(node)
    return units
