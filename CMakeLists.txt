cmake_minimum_required(VERSION 3.22)

project(booster_native_sdk_lab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(BOOSTER_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../booster_robotics_sdk" CACHE PATH "Path to the Booster native SDK repo")

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
  set(BOOSTER_SDK_ARCH "aarch64")
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
  set(BOOSTER_SDK_ARCH "x86_64")
else()
  message(FATAL_ERROR "Unsupported architecture '${CMAKE_SYSTEM_PROCESSOR}'")
endif()

set(BOOSTER_SDK_INCLUDE_DIR "${BOOSTER_SDK_ROOT}/include")
set(BOOSTER_SDK_LIBRARY "${BOOSTER_SDK_ROOT}/lib/${BOOSTER_SDK_ARCH}/libbooster_robotics_sdk.a")

if(NOT EXISTS "${BOOSTER_SDK_INCLUDE_DIR}")
  message(FATAL_ERROR "Booster SDK headers not found at ${BOOSTER_SDK_INCLUDE_DIR}")
endif()

if(NOT EXISTS "${BOOSTER_SDK_LIBRARY}")
  message(FATAL_ERROR "Booster SDK library not found at ${BOOSTER_SDK_LIBRARY}")
endif()

find_package(Boost REQUIRED COMPONENTS system)
find_library(FASTRTPS_LIB fastrtps REQUIRED)
find_library(FASTCDR_LIB fastcdr REQUIRED)
find_library(FOONATHAN_MEMORY_LIB foonathan_memory-0.7.3 REQUIRED)

add_executable(booster_sdk_http_wrapper
  src/main.cpp
)

target_include_directories(booster_sdk_http_wrapper PRIVATE
  "${BOOSTER_SDK_INCLUDE_DIR}"
)

target_compile_definitions(booster_sdk_http_wrapper PRIVATE
  BOOSTER_NATIVE_SDK_LAB_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(booster_sdk_http_wrapper PRIVATE
  "${BOOSTER_SDK_LIBRARY}"
  ${FASTRTPS_LIB}
  ${FASTCDR_LIB}
  ${FOONATHAN_MEMORY_LIB}
  Boost::system
  pthread
)
