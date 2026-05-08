include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.4
)

FetchContent_MakeAvailable(Catch2)

add_executable(mib_backend_tests
    backend/test_processing_service.cpp
    backend/test_hdf5_service_recording.cpp
    backend/test_frame_store_avi.cpp
)

target_link_libraries(mib_backend_tests PRIVATE
    mib_backend
    Catch2::Catch2WithMain
)

include(Catch)
catch_discover_tests(mib_backend_tests)
