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
set(BOOSTER_MOONSHINE_SOURCE_ROOT "" CACHE PATH "Path to the Moonshine source checkout")
set(BOOSTER_LLAMA_CPP_ROOT "" CACHE PATH "Path to the llama.cpp checkout")
option(BOOSTER_ENABLE_MOONSHINE_NATIVE "Build the native Moonshine ASR helper when Moonshine source is available" ON)
option(BOOSTER_ENABLE_SIMPLECHAT_SERVICE "Build the simplechat-service binary when llama.cpp is available" ON)

if(NOT BOOSTER_MOONSHINE_SOURCE_ROOT)
  foreach(_moonshine_candidate
      "${CMAKE_CURRENT_SOURCE_DIR}/../moonshine"
      "${CMAKE_CURRENT_SOURCE_DIR}/../moonshine_check"
      "/home/booster/Workspace/moonshine_check")
    if(EXISTS "${_moonshine_candidate}")
      set(BOOSTER_MOONSHINE_SOURCE_ROOT "${_moonshine_candidate}" CACHE PATH "Path to the Moonshine source checkout" FORCE)
      break()
    endif()
  endforeach()
endif()

if(NOT BOOSTER_LLAMA_CPP_ROOT)
  foreach(_llama_candidate
      "${CMAKE_CURRENT_SOURCE_DIR}/../llama.cpp"
      "/home/booster/Workspace/llama.cpp-buildtest")
    if(EXISTS "${_llama_candidate}")
      set(BOOSTER_LLAMA_CPP_ROOT "${_llama_candidate}" CACHE PATH "Path to the llama.cpp checkout" FORCE)
      break()
    endif()
  endforeach()
endif()

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

set(BOOSTER_MOONSHINE_CORE_DIR "")
if(BOOSTER_MOONSHINE_SOURCE_ROOT)
  if(EXISTS "${BOOSTER_MOONSHINE_SOURCE_ROOT}/core/CMakeLists.txt")
    set(BOOSTER_MOONSHINE_CORE_DIR "${BOOSTER_MOONSHINE_SOURCE_ROOT}/core")
  elseif(EXISTS "${BOOSTER_MOONSHINE_SOURCE_ROOT}/CMakeLists.txt")
    set(BOOSTER_MOONSHINE_CORE_DIR "${BOOSTER_MOONSHINE_SOURCE_ROOT}")
  endif()
endif()

if(BOOSTER_ENABLE_MOONSHINE_NATIVE AND BOOSTER_MOONSHINE_CORE_DIR)
  if(UNIX AND NOT APPLE)
    set(LINUX TRUE)
  endif()
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)

  add_subdirectory("${BOOSTER_MOONSHINE_CORE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/moonshine_core_build" EXCLUDE_FROM_ALL)

  get_target_property(_moonshine_sources moonshine SOURCES)
  if(_moonshine_sources)
    list(REMOVE_ITEM _moonshine_sources
      "speaker-embedding-model-data.cpp"
      "${BOOSTER_MOONSHINE_CORE_DIR}/speaker-embedding-model-data.cpp"
    )
    set_property(TARGET moonshine PROPERTY SOURCES "${_moonshine_sources}")
  endif()
  target_sources(moonshine PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/moonshine_speaker_embedding_stub.cpp"
  )

  add_executable(booster_moonshine_asr_native
    src/moonshine_asr_daemon.cpp
  )
  set_target_properties(booster_moonshine_asr_native PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
  )
  if(APPLE)
    set_target_properties(booster_moonshine_asr_native PROPERTIES
      BUILD_RPATH "@loader_path/moonshine_core_build"
      INSTALL_RPATH "@loader_path/moonshine_core_build"
    )
  elseif(UNIX)
    set_target_properties(booster_moonshine_asr_native PROPERTIES
      BUILD_RPATH "$ORIGIN/moonshine_core_build"
      INSTALL_RPATH "$ORIGIN/moonshine_core_build"
    )
  endif()
  target_include_directories(booster_moonshine_asr_native PRIVATE
    "${BOOSTER_MOONSHINE_CORE_DIR}"
  )
  if(NOT BOOSTER_DEV_MODE)
    target_include_directories(booster_moonshine_asr_native PRIVATE
      "${BOOSTER_SDK_INCLUDE_DIR}"
    )
  endif()
  if(TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(booster_moonshine_asr_native PRIVATE nlohmann_json::nlohmann_json)
  elseif(EXISTS "/opt/homebrew/include/nlohmann/json.hpp")
    target_include_directories(booster_moonshine_asr_native PRIVATE /opt/homebrew/include)
  endif()
  target_link_libraries(booster_moonshine_asr_native PRIVATE moonshine)
  if(NOT APPLE)
    target_link_libraries(booster_moonshine_asr_native PRIVATE pthread)
  endif()

  target_compile_definitions(booster_sdk_http_wrapper PRIVATE
    BOOSTER_MOONSHINE_NATIVE_HELPER_PATH="$<TARGET_FILE:booster_moonshine_asr_native>"
  )
endif()

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

if(BOOSTER_ENABLE_SIMPLECHAT_SERVICE AND BOOSTER_LLAMA_CPP_ROOT)
  set(BOOSTER_LLAMA_INCLUDE_DIR "${BOOSTER_LLAMA_CPP_ROOT}/include")
  set(BOOSTER_GGML_INCLUDE_DIR "${BOOSTER_LLAMA_CPP_ROOT}/ggml/include")
  set(BOOSTER_LLAMA_LIBRARY "${BOOSTER_LLAMA_CPP_ROOT}/build/bin/libllama.so")
  set(BOOSTER_GGML_LIBRARY "${BOOSTER_LLAMA_CPP_ROOT}/build/bin/libggml.so")
  if(EXISTS "${BOOSTER_LLAMA_INCLUDE_DIR}/llama.h" AND EXISTS "${BOOSTER_GGML_INCLUDE_DIR}/ggml.h" AND EXISTS "${BOOSTER_LLAMA_LIBRARY}" AND EXISTS "${BOOSTER_GGML_LIBRARY}")
    add_executable(booster_simplechat_service
      src/simplechat_service.cpp
    )
    target_include_directories(booster_simplechat_service PRIVATE
      "${BOOSTER_LLAMA_INCLUDE_DIR}"
      "${BOOSTER_GGML_INCLUDE_DIR}"
    )
    if(NOT BOOSTER_DEV_MODE)
      target_include_directories(booster_simplechat_service PRIVATE
        "${BOOSTER_SDK_INCLUDE_DIR}"
      )
    endif()
    if(Boost_INCLUDE_DIRS)
      target_include_directories(booster_simplechat_service PRIVATE ${Boost_INCLUDE_DIRS})
    endif()
    if(TARGET nlohmann_json::nlohmann_json)
      target_link_libraries(booster_simplechat_service PRIVATE nlohmann_json::nlohmann_json)
    elseif(EXISTS "/opt/homebrew/include/nlohmann/json.hpp")
      target_include_directories(booster_simplechat_service PRIVATE /opt/homebrew/include)
    endif()
    target_compile_definitions(booster_simplechat_service PRIVATE
      BOOSTER_NATIVE_SDK_LAB_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    if(TARGET Boost::system)
      target_link_libraries(booster_simplechat_service PRIVATE Boost::system)
    elseif(Boost_LIBRARIES)
      target_link_libraries(booster_simplechat_service PRIVATE ${Boost_LIBRARIES})
    endif()
    target_link_libraries(booster_simplechat_service PRIVATE
      "${BOOSTER_LLAMA_LIBRARY}"
      "${BOOSTER_GGML_LIBRARY}"
    )
    set_target_properties(booster_simplechat_service PROPERTIES
      BUILD_RPATH "${BOOSTER_LLAMA_CPP_ROOT}/build/bin"
      INSTALL_RPATH "${BOOSTER_LLAMA_CPP_ROOT}/build/bin"
    )
    if(NOT APPLE)
      target_link_libraries(booster_simplechat_service PRIVATE pthread)
    endif()
  endif()
endif()
