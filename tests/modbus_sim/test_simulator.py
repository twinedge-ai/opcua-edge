import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SIM = ROOT / "tools" / "modbus_sim"
sys.path.insert(0, str(SIM))

from edge_template import load_modbus_nodes
from plant_values import PlantRegisters


def test_template_loads_all_modbus_nodes():
    nodes = load_modbus_nodes(ROOT / "templates" / "desalination_plant.edge")
    assert len(nodes) == 116
    assert len({node.unit for node in nodes}) == 7


def test_register_values_are_deterministic():
    nodes = load_modbus_nodes(ROOT / "templates" / "desalination_plant.edge")
    plant = PlantRegisters(nodes, seed=3)
    first = plant.values(tick=10, profile="normal")
    second = plant.values(tick=10, profile="normal")
    assert first[1][0].value == second[1][0].value


def test_fault_profile_changes_named_asset_value():
    nodes = load_modbus_nodes(ROOT / "templates" / "desalination_plant.edge")
    plant = PlantRegisters(nodes)
    values = plant.values(tick=0, profile="high_vibration")
    target = None
    for value in values[3]:
        if value.node.asset == "hp_pump_1" and value.node.node_id == "vibration":
            target = value
            break
    assert target is not None
    assert target.value == 920


def test_writable_register_retains_written_value():
    nodes = load_modbus_nodes(ROOT / "templates" / "desalination_plant.edge")
    plant = PlantRegisters(nodes)
    command = next(node for node in nodes if node.asset == "hp_pump_1" and node.node_id == "command_start")
    plant.note_write(command.unit, command.offset, 1)
    values = plant.values(tick=5, profile="normal")
    found = next(value for value in values[command.unit] if value.offset == command.offset)
    assert found.value == 1


def test_32bit_nodes_use_two_big_endian_registers():
    nodes = load_modbus_nodes(ROOT / "templates" / "desalination_plant.edge")
    speed = next(node for node in nodes if node.asset == "hp_pump_1" and node.node_id == "speed_rpm")
    plant = PlantRegisters(nodes, seed=0)
    values = plant.values(tick=0, profile="normal")
    high = next(value for value in values[speed.unit] if value.offset == speed.offset)
    low = next(value for value in values[speed.unit] if value.offset == speed.offset + 1)
    raw = speed.unit * 100000 + speed.register
    assert speed.width == 2
    assert high.value == ((raw >> 16) & 0xFFFF)
    assert low.value == (raw & 0xFFFF)
