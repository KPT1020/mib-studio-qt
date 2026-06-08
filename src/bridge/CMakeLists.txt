add_library(mib_bridge_cpp_api INTERFACE)

target_include_directories(mib_bridge_cpp_api INTERFACE
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(mib_bridge_cpp_api INTERFACE
    mib_backend
)
