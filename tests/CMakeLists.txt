find_package(Python3 COMPONENTS Interpreter QUIET)

function(mib_add_backend_test target test_name)
    add_executable(${target} ${ARGN})
    target_link_libraries(${target} PRIVATE mib_backend)
    target_compile_definitions(${target} PRIVATE
        MIB_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    )
    add_test(NAME ${test_name} COMMAND $<TARGET_FILE:${target}>)
    set_tests_properties(${test_name} PROPERTIES LABELS "backend")
endfunction()

mib_add_backend_test(mib_backend_lifecycle_test
    backend.lifecycle
    backend/backend_lifecycle_test.cpp
)

mib_add_backend_test(mib_mock_camera_test
    backend.camera.mock
    camera/mock_camera_test.cpp
)

mib_add_backend_test(mib_hdf5_smoke_test
    backend.recording_hdf5_smoke
    recording/hdf5_smoke_test.cpp
)

mib_add_backend_test(processing_batch_pipeline_test
    backend.processing_batch_pipeline
    processing/processing_batch_pipeline_test.cpp
)

mib_add_backend_test(mib_processing_multi_object_test
    backend.processing_multi_object
    processing/processing_multi_object_test.cpp
)

mib_add_backend_test(mib_processing_object_tracking_test
    backend.processing_object_tracking
    processing/processing_object_tracking_test.cpp
)

add_executable(kin6_batch_pipeline_evidence
    integration/kin6_batch_pipeline_evidence.cpp
)
target_link_libraries(kin6_batch_pipeline_evidence PRIVATE mib_backend)
target_compile_definitions(kin6_batch_pipeline_evidence PRIVATE
    MIB_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)

add_executable(kin10_hf_dataset_pipeline_test
    integration/kin10_hf_dataset_pipeline_test.cpp
)
target_link_libraries(kin10_hf_dataset_pipeline_test PRIVATE mib_backend)
target_compile_definitions(kin10_hf_dataset_pipeline_test PRIVATE
    MIB_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)

if(Python3_Interpreter_FOUND)
    add_test(
        NAME backend.kin10_hf_dataset_pipeline
        COMMAND
            ${Python3_EXECUTABLE}
            ${PROJECT_SOURCE_DIR}/tools/kin10_run_hf_dataset_test.py
            --out-dir ${CMAKE_BINARY_DIR}/test-output/kin10_hf_dataset_pipeline
            --binary $<TARGET_FILE:kin10_hf_dataset_pipeline_test>
    )
    set_tests_properties(backend.kin10_hf_dataset_pipeline PROPERTIES
        LABELS "backend;integration"
        TIMEOUT 180
    )
elseif(UNIX)
    add_test(
        NAME backend.kin10_hf_dataset_pipeline
        COMMAND
            ${PROJECT_SOURCE_DIR}/tools/kin10_run_hf_dataset_test.sh
            ${CMAKE_BINARY_DIR}/kin10_hf_dataset_pipeline
            $<TARGET_FILE:kin10_hf_dataset_pipeline_test>
    )
    set_tests_properties(backend.kin10_hf_dataset_pipeline PROPERTIES
        LABELS "backend;integration"
        TIMEOUT 180
    )
else()
    message(WARNING "Python3 interpreter not found; backend.kin10_hf_dataset_pipeline will not be registered")
endif()

mib_add_backend_test(kin6_mib_app_capture_proof
    backend.kin6_mib_app_capture_proof
    integration/kin6_mib_app_capture_proof.cpp
)
