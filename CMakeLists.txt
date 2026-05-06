cmake_minimum_required(VERSION 3.20)

project(opcua_edge C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(EDGE_ENABLE_ASAN "Build edge_core and tests with AddressSanitizer + UBSan" OFF)
if(EDGE_ENABLE_ASAN)
  add_compile_options(-fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=address -fsanitize=undefined)
endif()

set(UA_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(UA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(UA_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(UA_LOGLEVEL 600 CACHE STRING "" FORCE)
set(UA_NAMESPACE_ZERO REDUCED CACHE STRING "" FORCE)
set(UA_ENABLE_AUDITING OFF CACHE BOOL "" FORCE)
set(UA_ENABLE_DA OFF CACHE BOOL "" FORCE)
set(UA_ENABLE_DIAGNOSTICS OFF CACHE BOOL "" FORCE)
set(UA_ENABLE_DISCOVERY OFF CACHE BOOL "" FORCE)
set(UA_ENABLE_HISTORIZING OFF CACHE BOOL "" FORCE)
set(UA_ENABLE_JSON_ENCODING ON CACHE BOOL "" FORCE)
set(UA_ENABLE_METHODCALLS ON CACHE BOOL "" FORCE)
set(UA_ENABLE_PUBSUB OFF CACHE BOOL "" FORCE)
set(UA_ENABLE_SUBSCRIPTIONS ON CACHE BOOL "" FORCE)
set(UA_ENABLE_SUBSCRIPTIONS_EVENTS ON CACHE BOOL "" FORCE)
set(UA_ENABLE_XML_ENCODING ON CACHE BOOL "" FORCE)

add_subdirectory(external/open62541)

find_package(SQLite3 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBMODBUS REQUIRED IMPORTED_TARGET libmodbus)

add_library(edge_core
  src/edge_address_space.c
  src/edge_config.c
  src/edge_events.c
  src/edge_modbus.c
  src/edge_runtime.c
  src/edge_server.c
  src/edge_store.c
  src/edge_time.c
)

target_include_directories(edge_core PUBLIC include)
target_compile_definitions(edge_core PUBLIC _GNU_SOURCE)
target_link_libraries(edge_core PUBLIC open62541::open62541 SQLite::SQLite3 PkgConfig::LIBMODBUS)

add_executable(opcua-edge src/main.c)
target_link_libraries(opcua-edge PRIVATE edge_core)

add_library(edge_benchmark_core
  benchmarks/benchmark_pump.c
  benchmarks/benchmark_scenarios.c
  benchmarks/benchmark_report.c
)
target_link_libraries(edge_benchmark_core PUBLIC edge_core)

add_executable(edge-benchmark benchmarks/benchmark_main.c)
target_link_libraries(edge-benchmark PRIVATE edge_benchmark_core)

enable_testing()

add_executable(test_config tests/unit/test_config.c)
target_link_libraries(test_config PRIVATE edge_core)
add_test(NAME test_config COMMAND test_config ${CMAKE_SOURCE_DIR}/templates/desalination_plant.edge)

add_executable(test_value tests/unit/test_value.c)
target_link_libraries(test_value PRIVATE edge_core m)
add_test(NAME test_value COMMAND test_value)

add_executable(test_address_space tests/integration/test_address_space.c)
target_link_libraries(test_address_space PRIVATE edge_core)
add_test(NAME test_address_space COMMAND test_address_space ${CMAKE_SOURCE_DIR}/templates/desalination_plant.edge)

add_executable(test_store tests/integration/test_store.c)
target_link_libraries(test_store PRIVATE edge_core)
add_test(NAME test_store COMMAND test_store ${CMAKE_SOURCE_DIR}/templates/desalination_plant.edge)

add_executable(test_benchmark_persist tests/integration/test_benchmark_persist.c)
target_link_libraries(test_benchmark_persist PRIVATE edge_benchmark_core)
add_test(NAME test_benchmark_persist COMMAND test_benchmark_persist)

add_executable(test_events tests/integration/test_events.c)
target_link_libraries(test_events PRIVATE edge_core)
add_test(NAME test_events COMMAND test_events ${CMAKE_SOURCE_DIR}/templates/desalination_plant.edge)

add_executable(test_modbus tests/integration/test_modbus.c)
target_link_libraries(test_modbus PRIVATE edge_core)
add_test(NAME test_modbus COMMAND test_modbus
  ${CMAKE_SOURCE_DIR}/templates/desalination_plant.edge
  ${CMAKE_SOURCE_DIR}/tools/modbus_sim/modbus_sim.py)

include(GNUInstallDirs)
install(TARGETS opcua-edge RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES templates/desalination_plant.edge
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/opcua-edge/templates)
install(FILES packaging/systemd/opcua-edge.service
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/systemd/system)
