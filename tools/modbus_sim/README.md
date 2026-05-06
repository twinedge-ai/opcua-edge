# Modbus Plant Simulator

Python test tooling for simulating the desalination plant PLC/controller layer from `templates/desalination_plant.edge`.

This simulator is not part of the C edge runtime and should not ship on the edge device.

## Install

```bash
python3 -m pip install -r tools/modbus_sim/requirements.txt
```

## Run

```bash
python3 tools/modbus_sim/modbus_sim.py \
  --template templates/desalination_plant.edge \
  --host 127.0.0.1 \
  --port 1502 \
  --profile normal \
  --tick-ms 100
```

## Inspect Register Map

```bash
python3 tools/modbus_sim/modbus_sim.py \
  --template templates/desalination_plant.edge \
  --dump-map
```

## Profiles

Supported profiles:

```text
normal
high_vibration
low_suction_pressure
overload_current
ro_quality_drift
```

The simulator serves holding registers only. Template register `40001` maps to Modbus offset `0`.

