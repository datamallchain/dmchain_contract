add_executable(dmc.abo.wasm ${CMAKE_CURRENT_SOURCE_DIR}/src/dmc.abo.cpp)
target_include_directories(dmc.abo.wasm
   PUBLIC 
   ${CMAKE_CURRENT_SOURCE_DIR}/include)

set_target_properties(dmc.abo.wasm
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin/dmc.abo")

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/abi/dmc.abo.abi" "${CMAKE_CURRENT_SOURCE_DIR}/bin/dmc.abo" COPYONLY)

