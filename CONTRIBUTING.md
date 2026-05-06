# Contributing

Thank You.

## Development Setup

Install the native dependencies listed in `README.md`, then initialize the
submodule and build:

```sh
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

For Modbus simulator work:

```sh
python3 -m pip install -r tools/modbus_sim/requirements.txt
pytest tests/modbus_sim
```

## Pull Requests

- Keep changes scoped to one behavior or maintenance task.
- Add or update tests for parser, address-space, Modbus, persistence, event, or
  simulator behavior when those areas change.
- Run the relevant validation commands before opening a pull request.
- Do not commit local databases, build directories, credentials, logs, or
  generated cache directories.

## Coding Style

The C runtime targets C11 and favors small, explicit modules with predictable
fixed-size data structures. Follow the existing include, naming, and error
handling style. 
