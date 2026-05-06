#!/usr/bin/env sh
set -eu

if ! command -v valgrind >/dev/null 2>&1; then
  echo "valgrind is required for leak validation; install it and rerun scripts/leak-check.sh" >&2
  exit 1
fi

BUILD_DIR=${BUILD_DIR:-build}
DB_PATH=${DB_PATH:-leak-check.db}

cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug
cmake --build "$BUILD_DIR" -j

rm -f "$DB_PATH"

VALGRIND="valgrind --leak-check=full --show-leak-kinds=definite,indirect,possible --errors-for-leak-kinds=definite,indirect,possible --error-exitcode=99"

EDGE_CONFIG_PATH=templates/desalination_plant.edge \
EDGE_DB_PATH="$DB_PATH" \
EDGE_MODBUS_HOST= \
EDGE_MODBUS_PORT=0 \
EDGE_OPCUA_PORT=4860 \
sh -c "$VALGRIND ./$BUILD_DIR/opcua-edge & pid=\$!; sleep 2; kill -INT \$pid; wait \$pid"

$VALGRIND "./$BUILD_DIR/edge-benchmark" --scenario read --pumps 2 --seconds 1 --db "$DB_PATH"
$VALGRIND "./$BUILD_DIR/edge-benchmark" --scenario write --pumps 2 --seconds 1 --db "$DB_PATH"
$VALGRIND "./$BUILD_DIR/edge-benchmark" --scenario events --pumps 2 --seconds 1 --with-events --db "$DB_PATH"

rm -f "$DB_PATH"
