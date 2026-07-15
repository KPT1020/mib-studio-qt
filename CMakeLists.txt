cmake_minimum_required(VERSION 3.21)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(MIBVersion)

project(MIB_Studio_Qt VERSION ${PROJECT_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

include(MIBOptions)
include(MIBCompilerSettings)
include(MIBDependencies)
include(CTest)

add_subdirectory(src/backend)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

if(NOT MIB_BUILD_BACKEND_ONLY)
    add_subdirectory(src/frontend/qt)

    include(MIBWindowsPackaging)
    mib_add_windows_installer_targets(mib_studio_qt)
endif()
