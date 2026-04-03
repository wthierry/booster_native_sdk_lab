cmake_minimum_required(VERSION 3.22)

project(booster_native_sdk_lab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(BOOSTER_DEV_MODE_DEFAULT OFF)
if(APPLE)
  set(BOOSTER_DEV_MODE_DEFAULT ON)
endif()
option(BOOSTER_DEV_MODE "Build without robot SDK and Linux runtime dependencies" ${BOOSTER_DEV_MODE_DEFAULT})

set(BOOSTER_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../booster_robotics_sdk" CACHE PATH "Path to the Booster native SDK repo")

if(APPLE AND EXISTS "/opt/homebrew/opt/boost")
  set(Boost_ROOT "/opt/homebrew/opt/boost")
endif()
if(APPLE AND EXISTS "/opt/homebrew/opt/nlohmann-json")
  set(nlohmann_json_ROOT "/opt/homebrew/opt/nlohmann-json")
endif()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
  set(BOOSTER_SDK_ARCH "aarch64")
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
  set(BOOSTER_SDK_ARCH "x86_64")
else()
  message(FATAL_ERROR "Unsupported architecture '${CMAKE_SYSTEM_PROCESSOR}'")
endif()

find_package(Boost CONFIG QUIET COMPONENTS system)
if(NOT Boost_FOUND)
  find_package(Boost REQUIRED COMPONENTS system)
endif()
find_package(nlohmann_json CONFIG QUIET)

if(NOT BOOSTER_DEV_MODE)
  set(BOOSTER_SDK_INCLUDE_DIR "${BOOSTER_SDK_ROOT}/include")
  set(BOOSTER_SDK_LIBRARY "${BOOSTER_SDK_ROOT}/lib/${BOOSTER_SDK_ARCH}/libbooster_robotics_sdk.a")

  if(NOT EXISTS "${BOOSTER_SDK_INCLUDE_DIR}")
    message(FATAL_ERROR "Booster SDK headers not found at ${BOOSTER_SDK_INCLUDE_DIR}")
  endif()

  if(NOT EXISTS "${BOOSTER_SDK_LIBRARY}")
    message(FATAL_ERROR "Booster SDK library not found at ${BOOSTER_SDK_LIBRARY}")
  endif()

  find_library(FASTRTPS_LIB fastrtps REQUIRED)
  find_library(FASTCDR_LIB fastcdr REQUIRED)
  find_library(FOONATHAN_MEMORY_LIB foonathan_memory-0.7.3 REQUIRED)
endif()

add_executable(booster_sdk_http_wrapper
  src/main.cpp
)

if(Boost_INCLUDE_DIRS)
  target_include_directories(booster_sdk_http_wrapper PRIVATE ${Boost_INCLUDE_DIRS})
endif()
if(TARGET nlohmann_json::nlohmann_json)
  target_link_libraries(booster_sdk_http_wrapper PRIVATE nlohmann_json::nlohmann_json)
elseif(EXISTS "/opt/homebrew/include/nlohmann/json.hpp")
  target_include_directories(booster_sdk_http_wrapper PRIVATE /opt/homebrew/include)
endif()

if(NOT BOOSTER_DEV_MODE)
  target_include_directories(booster_sdk_http_wrapper PRIVATE
    "${BOOSTER_SDK_INCLUDE_DIR}"
  )
endif()

target_compile_definitions(booster_sdk_http_wrapper PRIVATE
  BOOSTER_NATIVE_SDK_LAB_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  BOOSTER_DEV_MODE=$<BOOL:${BOOSTER_DEV_MODE}>
)

if(TARGET Boost::system)
  target_link_libraries(booster_sdk_http_wrapper PRIVATE Boost::system)
elseif(Boost_LIBRARIES)
  target_link_libraries(booster_sdk_http_wrapper PRIVATE ${Boost_LIBRARIES})
endif()

if(NOT BOOSTER_DEV_MODE)
  target_link_libraries(booster_sdk_http_wrapper PRIVATE
    "${BOOSTER_SDK_LIBRARY}"
    ${FASTRTPS_LIB}
    ${FASTCDR_LIB}
    ${FOONATHAN_MEMORY_LIB}
  )
endif()

if(NOT APPLE)
  target_link_libraries(booster_sdk_http_wrapper PRIVATE pthread)
endif()
