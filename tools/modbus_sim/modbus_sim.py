#!/usr/bin/env python3
from __future__ import annotations

import argparse
import asyncio
import inspect
from pathlib import Path

from edge_template import load_modbus_nodes
from plant_values import PlantRegisters
from profiles import PROFILES


class TrackingDataBlock:
    def __init__(self, base_block, plant: PlantRegisters, unit: int) -> None:
        self._block = base_block
        self._plant = plant
        self._unit = unit

    def validate(self, address, count=1):
        return self._block.validate(address, count)

    def getValues(self, address, count=1):
        return self._block.getValues(address, count)

    def setValues(self, address, values):
        self._block.setValues(address, values)
        if isinstance(values, int):
            self._plant.note_write(self._unit, int(address), int(values))
            return
        for index, value in enumerate(values):
            self._plant.note_write(self._unit, int(address) + index, int(value))


def _import_pymodbus():
    from pymodbus.datastore import ModbusSequentialDataBlock, ModbusServerContext
    from pymodbus.server import StartAsyncTcpServer

    try:
        from pymodbus.datastore import ModbusDeviceContext
    except ImportError:
        from pymodbus.datastore import ModbusSlaveContext as ModbusDeviceContext

    return ModbusSequentialDataBlock, ModbusServerContext, ModbusDeviceContext, StartAsyncTcpServer


def _create_server_context(plant: PlantRegisters):
    ModbusSequentialDataBlock, ModbusServerContext, ModbusDeviceContext, _ = _import_pymodbus()
    devices = {}

    for unit, max_offset in plant.max_offset_by_unit().items():
        block = ModbusSequentialDataBlock(0, [0] * (max_offset + 1))
        tracking_block = TrackingDataBlock(block, plant, unit)
        devices[unit] = ModbusDeviceContext(hr=tracking_block)

    try:
        return ModbusServerContext(devices=devices, single=False)
    except TypeError:
        return ModbusServerContext(slaves=devices, single=False)


def _set_register(context, unit: int, offset: int, value: int) -> None:
    device = context[unit]
    try:
        device.setValues(3, offset, [value & 0xFFFF])
    except TypeError:
        device.setValues(3, offset, value & 0xFFFF)


async def _updater(context, plant: PlantRegisters, profile: str, tick_ms: int) -> None:
    tick = 0
    delay = tick_ms / 1000.0
    while True:
        for unit, values in plant.values(tick, profile).items():
            for register_value in values:
                _set_register(context, unit, register_value.offset, register_value.value)
        tick += 1
        await asyncio.sleep(delay)


async def run_server(args) -> None:
    nodes = load_modbus_nodes(args.template)
    if not nodes:
        raise SystemExit("no modbus nodes found in template")

    plant = PlantRegisters(nodes, seed=args.seed)
    context = _create_server_context(plant)

    for unit, values in plant.values(0, args.profile).items():
        for register_value in values:
            _set_register(context, unit, register_value.offset, register_value.value)

    if not args.quiet:
        print(
            f"modbus simulator serving {len(nodes)} tags "
            f"across {len(plant.units)} units on {args.host}:{args.port}"
        )

    _, _, _, StartAsyncTcpServer = _import_pymodbus()
    asyncio.create_task(_updater(context, plant, args.profile, args.tick_ms))

    result = StartAsyncTcpServer(context=context, address=(args.host, args.port))
    if inspect.isawaitable(result):
        await result


def dump_map(template: str | Path, profile: str, seed: int) -> None:
    nodes = load_modbus_nodes(template)
    plant = PlantRegisters(nodes, seed=seed)
    for unit, values in sorted(plant.values(0, profile).items()):
        for value in values:
            node = value.node
            print(
                f"unit={unit} register={node.register} offset={node.offset} "
                f"asset={node.asset} node={node.node_id} value={value.value}"
            )


def parse_args():
    parser = argparse.ArgumentParser(description="Desalination plant Modbus TCP simulator")
    parser.add_argument("--template", default="templates/desalination_plant.edge")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=1502)
    parser.add_argument("--profile", choices=sorted(PROFILES), default="normal")
    parser.add_argument("--tick-ms", type=int, default=100)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--quiet", action="store_true")
    parser.add_argument("--dump-map", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.tick_ms <= 0:
        raise SystemExit("--tick-ms must be positive")

    if args.dump_map:
        dump_map(args.template, args.profile, args.seed)
        return 0

    asyncio.run(run_server(args))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

