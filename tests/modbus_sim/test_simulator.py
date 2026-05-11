import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SIM = ROOT / "tools" / "modbus_sim"
sys.path.insert(0, str(SIM))

from edge_template import load_modbus_nodes
from plant_values import PlantRegisters


def _raw_to_engineering(value, node):
    if node.data_type == "double":
        signed = value if value < 0x8000 else value - 0x10000
        scale = node.scale if node.scale != 0 else 1.0
        return signed * scale
    return value


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


def test_normal_profile_generates_physical_positive_process_values():
    nodes = load_modbus_nodes(ROOT / "templates" / "desalination_plant.edge")
    plant = PlantRegisters(nodes, seed=5)

    for tick in (0, 100, 500, 1000, 2500):
        values = plant.values(tick=tick, profile="normal")
        for unit_values in values.values():
            for value in unit_values:
                node = value.node
                if node.data_type != "double":
                    continue
                engineering = _raw_to_engineering(value.value, node)
                assert engineering >= 0.0, f"{node.asset}.{node.node_id} was {engineering}"
                assert value.value <= 0x7FFF, f"{node.asset}.{node.node_id} wrapped signed int16"


def test_normal_profile_respects_core_desalination_physics():
    nodes = load_modbus_nodes(ROOT / "templates" / "desalination_plant.edge")
    plant = PlantRegisters(nodes)
    values = plant.values(tick=0, profile="normal")

    by_node = {
        (value.node.asset, value.node.node_id): _raw_to_engineering(value.value, value.node)
        for unit_values in values.values()
        for value in unit_values
        if value.node.data_type == "double"
    }

    assert 14000.0 <= by_node[("intake_pump_1", "flow_rate")] <= 18000.0
    assert 9000.0 <= by_node[("hp_pump_1", "flow_rate")] <= 12000.0
    assert 1.2 <= by_node[("intake_basin", "tide_level")] <= 2.3
    assert 50000.0 <= by_node[("intake_basin", "raw_conductivity")] <= 55000.0
    assert 1.0 < by_node[("hp_pump_1", "suction_pressure")] < 3.0
    assert by_node[("hp_pump_1", "vibration")] < 8.5
    assert by_node[("hp_pump_1", "motor_current")] < 850.0
    assert 40.0 <= by_node[("ro_train_1", "recovery_pct")] <= 48.0
    assert 98.8 <= by_node[("ro_train_1", "salt_rejection_pct")] <= 99.8
    assert by_node[("ro_train_1", "permeate_conductivity")] < 500.0


def test_salt_rejection_scale_can_represent_realistic_ro_rejection():
    nodes = load_modbus_nodes(ROOT / "templates" / "desalination_plant.edge")
    salt_nodes = [node for node in nodes if node.node_id == "salt_rejection_pct"]
    assert len(salt_nodes) == 3
    assert all(node.scale == 0.01 for node in salt_nodes)


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
    raw = (high.value << 16) | low.value
    assert speed.width == 2
    assert high.value == ((raw >> 16) & 0xFFFF)
    assert low.value == (raw & 0xFFFF)
    assert 2800 <= raw <= 3100
