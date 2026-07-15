# Builds the _mib_processing pybind11 extension module over the Qt-free
# mib_processing static library (see ../../src/backend/CMakeLists.txt).
# Enabled via -DMIB_BUILD_PYTHON_BINDINGS=ON (cmake/MIBOptions.cmake); driven
# normally through `pip install .` / scikit-build-core, see pyproject.toml.

find_package(Python3 COMPONENTS Interpreter Development.Module REQUIRED)
find_package(pybind11 CONFIG REQUIRED)

pybind11_add_module(_mib_processing MODULE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/bindings.cpp
)

target_link_libraries(_mib_processing PRIVATE mib_processing)

set_target_properties(_mib_processing PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

# scikit-build-core packages whatever CMake installs; the compiled extension
# lands inside the mib_processing/ python package directory in the wheel.
install(TARGETS _mib_processing LIBRARY DESTINATION mib_processing)
