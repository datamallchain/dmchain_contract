add_executable(dmc.bios.wasm ${CMAKE_CURRENT_SOURCE_DIR}/src/dmc.bios.cpp)
target_include_directories(dmc.bios.wasm
   PUBLIC 
   ${CMAKE_CURRENT_SOURCE_DIR}/include
   ${EOS_DIR}/dmc.system/include
   ${CMAKE_CURRENT_SOURCE_DIR}/../dmc.token/include)

set_target_properties(dmc.bios.wasm
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin/dmc.bios")


configure_file("${CMAKE_CURRENT_SOURCE_DIR}/abi/dmc.bios.abi" "${CMAKE_CURRENT_SOURCE_DIR}/bin/dmc.bios" COPYONLY)
#install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/ DESTINATION ${WASM_ROOT}/dmc.wasmsdk/include)
