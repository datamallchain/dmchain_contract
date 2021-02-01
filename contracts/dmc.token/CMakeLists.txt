add_contract(token token
   ${CMAKE_CURRENT_SOURCE_DIR}/src/classic_token.cpp
   ${CMAKE_CURRENT_SOURCE_DIR}/src/lock_account.cpp
   ${CMAKE_CURRENT_SOURCE_DIR}/src/smart_token.cpp
   ${CMAKE_CURRENT_SOURCE_DIR}/src/smart_extend.cpp
   ${CMAKE_CURRENT_SOURCE_DIR}/src/nft.cpp
   ${CMAKE_CURRENT_SOURCE_DIR}/src/uniswap.cpp
   ${CMAKE_CURRENT_SOURCE_DIR}/src/record.cpp
   ${CMAKE_CURRENT_SOURCE_DIR}/src/dmc.cpp
   ${CMAKE_CURRENT_SOURCE_DIR}/src/dmc_deliver.cpp
   ${CMAKE_CURRENT_SOURCE_DIR}/src/dmc_challenge.cpp)

# configure_file(${CMAKE_CURRENT_SOURCE_DIR}/../ricardian/ddc.contracts.md.in ${CMAKE_CURRENT_BINARY_DIR}/ricardian/ddc.contracts.md @ONLY)

# target_compile_options(ddc PUBLIC -R${CMAKE_CURRENT_SOURCE_DIR}/ricardian -R${CMAKE_CURRENT_BINARY_DIR}/ricardian)
target_include_directories(token
   PUBLIC
   ${CMAKE_CURRENT_SOURCE_DIR}/include)

set_target_properties(token
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

target_compile_options(token PUBLIC -R${CMAKE_CURRENT_SOURCE_DIR}/ricardian -R${CMAKE_CURRENT_BINARY_DIR}/ricardian)
