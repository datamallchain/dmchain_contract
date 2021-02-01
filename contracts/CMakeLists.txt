cmake_minimum_required(VERSION 3.5)

project(contracts)

set(EOSIO_WASM_OLD_BEHAVIOR "Off")
find_package(eosio.cdt)

# add_subdirectory(dmc.bios)
# add_subdirectory(dmc.msig)
# add_subdirectory(dmc.system)
add_subdirectory(dmc.token)

# add_subdirectory(dmc.wrap)