add_executable(dmc.system.wasm ${CMAKE_CURRENT_SOURCE_DIR}/src/dmc.system.cpp)
target_include_directories(dmc.system.wasm
   PUBLIC 
   ${CMAKE_CURRENT_SOURCE_DIR}/include
   ${CMAKE_CURRENT_SOURCE_DIR}/../dmc.token/include)

set_target_properties(dmc.system.wasm
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin/dmc.system")

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/abi/dmc.system.abi" "${CMAKE_CURRENT_SOURCE_DIR}/bin/dmc.system" COPYONLY)
#install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/ DESTINATION ${WASM_ROOT}/dmc.wasmsdk/include)