add_executable(eosio.token.wasm ${CMAKE_CURRENT_SOURCE_DIR}/src/eosio.token.cpp)
target_include_directories(eosio.token.wasm
   PUBLIC 
   ${CMAKE_CURRENT_SOURCE_DIR}/include)

set_target_properties(eosio.token.wasm
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin/eosio.token")

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/abi/eosio.token.abi" "${CMAKE_CURRENT_SOURCE_DIR}/bin/eosio.token" COPYONLY)
