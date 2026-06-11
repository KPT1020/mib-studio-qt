find_package(Python3 COMPONENTS Interpreter QUIET)

function(mib_add_backend_test_executable target source)
    add_executable(${target}
        ${PROJECT_SOURCE_DIR}/tests/${source}
    )
    target_link_libraries(${target} PRIVATE mib_backend)
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
    )
endfunction()

mib_add_backend_test_executable(mib_backend_smoke_test backend/backend_smoke_test.cpp)
add_test(NAME backend.smoke COMMAND $<TARGET_FILE:mib_backend_smoke_test>)
set_tests_properties(backend.smoke PROPERTIES LABELS "backend")

mib_add_backend_test_executable(backend_lifecycle_smoke_test backend/backend_lifecycle_smoke_test.cpp)
add_test(NAME backend.lifecycle_smoke COMMAND $<TARGET_FILE:backend_lifecycle_smoke_test>)
set_tests_properties(backend.lifecycle_smoke PROPERTIES LABELS "backend")

mib_add_backend_test_executable(backend_facade_boundary_test backend/backend_facade_boundary_test.cpp)
add_test(NAME backend.facade_boundary COMMAND $<TARGET_FILE:backend_facade_boundary_test>)
set_tests_properties(backend.facade_boundary PROPERTIES LABELS "backend;bridge")

mib_add_backend_test_executable(mindvision_selection_state_test backend/mindvision_selection_state_test.cpp)
add_test(NAME backend.mindvision_selection_state COMMAND $<TARGET_FILE:mindvision_selection_state_test>)
set_tests_properties(backend.mindvision_selection_state PROPERTIES LABELS "backend;camera")

mib_add_backend_test_executable(mock_camera_smoke_test camera/mock_camera_smoke_test.cpp)
add_test(NAME camera.mock_smoke COMMAND $<TARGET_FILE:mock_camera_smoke_test>)
set_tests_properties(camera.mock_smoke PROPERTIES LABELS "camera;backend")

mib_add_backend_test_executable(processing_pipeline_smoke_test processing/processing_pipeline_smoke_test.cpp)
add_test(NAME processing.pipeline_smoke COMMAND $<TARGET_FILE:processing_pipeline_smoke_test>)
set_tests_properties(processing.pipeline_smoke PROPERTIES LABELS "processing;backend")

mib_add_backend_test_executable(processing_batch_pipeline_test processing/processing_batch_pipeline_test.cpp)
add_test(NAME backend.processing_batch_pipeline COMMAND $<TARGET_FILE:processing_batch_pipeline_test>)
set_tests_properties(backend.processing_batch_pipeline PROPERTIES LABELS "processing;backend")

mib_add_backend_test_executable(recording_lifecycle_test recording/recording_lifecycle_test.cpp)
add_test(NAME recording.lifecycle COMMAND $<TARGET_FILE:recording_lifecycle_test>)
set_tests_properties(recording.lifecycle PROPERTIES LABELS "recording;backend")

mib_add_backend_test_executable(kin6_batch_pipeline_evidence integration/kin6_batch_pipeline_evidence.cpp)

mib_add_backend_test_executable(kin10_hf_dataset_pipeline_test integration/kin10_hf_dataset_pipeline_test.cpp)
if(Python3_Interpreter_FOUND)
    add_test(
        NAME backend.kin10_hf_dataset_pipeline
        COMMAND
            ${Python3_EXECUTABLE}
            ${PROJECT_SOURCE_DIR}/tools/kin10_run_hf_dataset_test.py
            --out-dir ${PROJECT_BINARY_DIR}/test-output/kin10_hf_dataset_pipeline
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
            ${PROJECT_BINARY_DIR}/kin10_hf_dataset_pipeline
            $<TARGET_FILE:kin10_hf_dataset_pipeline_test>
    )
    set_tests_properties(backend.kin10_hf_dataset_pipeline PROPERTIES
        LABELS "backend;integration"
        TIMEOUT 180
    )
else()
    message(WARNING "Python3 interpreter not found; backend.kin10_hf_dataset_pipeline will not be registered")
endif()

mib_add_backend_test_executable(kin6_mib_app_capture_proof integration/kin6_mib_app_capture_proof.cpp)
add_test(NAME backend.kin6_mib_app_capture_proof COMMAND $<TARGET_FILE:kin6_mib_app_capture_proof>)
set_tests_properties(backend.kin6_mib_app_capture_proof PROPERTIES LABELS "backend;integration")

mib_add_backend_test_executable(mib_processing_multi_object_test processing/processing_multi_object_test.cpp)
add_test(NAME backend.processing_multi_object COMMAND $<TARGET_FILE:mib_processing_multi_object_test>)
set_tests_properties(backend.processing_multi_object PROPERTIES LABELS "processing;backend")

mib_add_backend_test_executable(mib_processing_object_tracking_test processing/processing_object_tracking_test.cpp)
add_test(NAME backend.processing_object_tracking COMMAND $<TARGET_FILE:mib_processing_object_tracking_test>)
set_tests_properties(backend.processing_object_tracking PROPERTIES LABELS "processing;backend")
