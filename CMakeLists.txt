cmake_minimum_required(VERSION 3.20)
project(btoon_python LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# btoon-core: the git submodule in core/ (or BTOON_CORE_DIR, e.g. ../btoon-core while
# developing both). Built as a static library straight into the extension module.
set(BTOON_CORE_DIR "" CACHE PATH "Path to the btoon-core sources")
if(NOT BTOON_CORE_DIR)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/core/CMakeLists.txt")
        set(BTOON_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/core")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../btoon-core/CMakeLists.txt")
        set(BTOON_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../btoon-core")
    else()
        message(FATAL_ERROR "btoon-core not found. Run `git submodule update --init` or pass -DBTOON_CORE_DIR=...")
    endif()
endif()
message(STATUS "btoon-python: using btoon-core from ${BTOON_CORE_DIR}")

# Windows has no system zlib: compile it in. Elsewhere use the platform's zlib unless told otherwise.
if(WIN32)
    set(BTOON_BUNDLED_ZLIB ON CACHE BOOL "" FORCE)
endif()
set(BTOON_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${BTOON_CORE_DIR}" btoon-core EXCLUDE_FROM_ALL)

find_package(Python COMPONENTS Interpreter Development.Module REQUIRED)
find_package(pybind11 CONFIG REQUIRED)

pybind11_add_module(_btoon MODULE src/btoon_python.cpp)
target_link_libraries(_btoon PRIVATE btoon::btoon_core)
if(NOT MSVC)
    target_compile_options(_btoon PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
endif()

install(TARGETS _btoon LIBRARY DESTINATION btoon)
