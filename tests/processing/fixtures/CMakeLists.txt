cmake_minimum_required(VERSION 3.21)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(mib_processing_abi_fixtures LANGUAGES C CXX)
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_C_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(MIB_PROCESSING_ABI_HEADER_DIR "" CACHE PATH "Directory containing backend/processing/ProcessingCoreAbi.h")
    set(MIB_PROCESSING_CORE_VERSION "0.1.0" CACHE STRING "Fixture processing-core version")
    if(NOT IS_DIRECTORY "${MIB_PROCESSING_ABI_HEADER_DIR}")
        message(FATAL_ERROR "MIB_PROCESSING_ABI_HEADER_DIR must point to the public include directory")
    endif()
else()
    set(MIB_PROCESSING_ABI_HEADER_DIR "${PROJECT_SOURCE_DIR}/include")
    file(READ "${PROJECT_SOURCE_DIR}/bindings/python/pyproject.toml"
        _mib_processing_fixture_pyproject)
    string(REGEX MATCH "version[ \t]*=[ \t]*\"([^\"]+)\""
        _mib_processing_fixture_version_match
        "${_mib_processing_fixture_pyproject}")
    set(MIB_PROCESSING_CORE_VERSION "${CMAKE_MATCH_1}")
    if(MIB_PROCESSING_CORE_VERSION STREQUAL "")
        message(FATAL_ERROR "Could not resolve the processing-core version for ABI fixtures")
    endif()
endif()

function(mib_add_processing_core_fixture target mode)
    add_library(${target} MODULE processing_core_fixture.c)
    target_include_directories(${target} PRIVATE "${MIB_PROCESSING_ABI_HEADER_DIR}")
    target_compile_definitions(${target} PRIVATE
        MIB_PROCESSING_CORE_PLUGIN_EXPORTS=1
        MIB_PROCESSING_CORE_VERSION="${MIB_PROCESSING_CORE_VERSION}"
        MIB_PROCESSING_FIXTURE_MODE=${mode}
    )
    set_target_properties(${target} PROPERTIES
        PREFIX ""
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
endfunction()

mib_add_processing_core_fixture(mib_processing_fixture_good 0)
mib_add_processing_core_fixture(mib_processing_fixture_truncated 1)
mib_add_processing_core_fixture(mib_processing_fixture_incompatible 2)
mib_add_processing_core_fixture(mib_processing_fixture_malformed 3)

add_library(mib_processing_fixture_throwing MODULE processing_core_throwing_fixture.cpp)
target_include_directories(mib_processing_fixture_throwing PRIVATE "${MIB_PROCESSING_ABI_HEADER_DIR}")
target_compile_definitions(mib_processing_fixture_throwing PRIVATE
    MIB_PROCESSING_CORE_PLUGIN_EXPORTS=1
    MIB_PROCESSING_CORE_VERSION="${MIB_PROCESSING_CORE_VERSION}"
)
set_target_properties(mib_processing_fixture_throwing PROPERTIES
    PREFIX ""
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
