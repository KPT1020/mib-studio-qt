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

mib_add_backend_test_executable(emodulus_lut_catalog_test backend/emodulus_lut_catalog_test.cpp)
add_test(NAME backend.emodulus_lut_catalog COMMAND $<TARGET_FILE:emodulus_lut_catalog_test>)
set_tests_properties(backend.emodulus_lut_catalog PROPERTIES LABELS "backend;processing")

mib_add_backend_test_executable(backend_lifecycle_smoke_test backend/backend_lifecycle_smoke_test.cpp)
add_test(NAME backend.lifecycle_smoke COMMAND $<TARGET_FILE:backend_lifecycle_smoke_test>)
set_tests_properties(backend.lifecycle_smoke PROPERTIES LABELS "backend")

mib_add_backend_test_executable(frame_store_concurrency_test backend/frame_store_concurrency_test.cpp)
find_package(Threads REQUIRED)
target_link_libraries(frame_store_concurrency_test PRIVATE Threads::Threads)
add_test(NAME backend.frame_store_concurrency COMMAND $<TARGET_FILE:frame_store_concurrency_test>)
set_tests_properties(backend.frame_store_concurrency PROPERTIES LABELS "backend")

mib_add_backend_test_executable(frame_store_bounds_test backend/frame_store_bounds_test.cpp)
add_test(NAME backend.frame_store_bounds COMMAND $<TARGET_FILE:frame_store_bounds_test>)
set_tests_properties(backend.frame_store_bounds PROPERTIES LABELS "backend" TIMEOUT 30)

mib_add_backend_test_executable(pipeline_timing_benchmark performance/pipeline_timing_benchmark.cpp)
target_link_libraries(pipeline_timing_benchmark PRIVATE Threads::Threads)
add_test(NAME performance.pipeline_timing COMMAND $<TARGET_FILE:pipeline_timing_benchmark>)
set_tests_properties(performance.pipeline_timing PROPERTIES LABELS "performance" TIMEOUT 180)

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

mib_add_backend_test_executable(e2e_storage_destinations_test integration/e2e_storage_destinations_test.cpp)
add_test(NAME integration.e2e_storage_destinations COMMAND $<TARGET_FILE:e2e_storage_destinations_test>)
set_tests_properties(integration.e2e_storage_destinations PROPERTIES LABELS "integration;e2e;recording" TIMEOUT 60)

mib_add_backend_test_executable(e2e_trigger_timing_test integration/e2e_trigger_timing_test.cpp)
target_link_libraries(e2e_trigger_timing_test PRIVATE Threads::Threads)
add_test(NAME integration.e2e_trigger_timing COMMAND $<TARGET_FILE:e2e_trigger_timing_test>)
set_tests_properties(integration.e2e_trigger_timing PROPERTIES LABELS "integration;e2e;trigger" TIMEOUT 120)

mib_add_backend_test_executable(e2e_pipeline_stress_test integration/e2e_pipeline_stress_test.cpp)
target_link_libraries(e2e_pipeline_stress_test PRIVATE Threads::Threads)
# Labeled "stress": a non-deterministic lifecycle start/stop race test. It is
# excluded from the gating Linux CI preset (linux-backend-only-test) because it
# can intermittently hang on slower runners — a real concurrency signal worth
# investigating separately, not a per-commit gate. Run on demand:
#   ctest -R integration.e2e_pipeline_stress   (or the exe with a [cycles] arg)
add_test(NAME integration.e2e_pipeline_stress COMMAND $<TARGET_FILE:e2e_pipeline_stress_test>)
set_tests_properties(integration.e2e_pipeline_stress PROPERTIES LABELS "integration;e2e;stress" TIMEOUT 180)

mib_add_backend_test_executable(e2e_live_view_latency_test integration/e2e_live_view_latency_test.cpp)
target_link_libraries(e2e_live_view_latency_test PRIVATE Threads::Threads)
add_test(NAME integration.e2e_live_view_latency COMMAND $<TARGET_FILE:e2e_live_view_latency_test>)
set_tests_properties(integration.e2e_live_view_latency PROPERTIES LABELS "integration;e2e;processing" TIMEOUT 120)

mib_add_backend_test_executable(realtime_drop_frames_default_test processing/realtime_drop_frames_default_test.cpp)
add_test(NAME processing.realtime_drop_frames_default COMMAND $<TARGET_FILE:realtime_drop_frames_default_test>)
set_tests_properties(processing.realtime_drop_frames_default PROPERTIES LABELS "processing;backend" TIMEOUT 30)

mib_add_backend_test_executable(mib_processing_multi_object_test processing/processing_multi_object_test.cpp)
add_test(NAME backend.processing_multi_object COMMAND $<TARGET_FILE:mib_processing_multi_object_test>)
set_tests_properties(backend.processing_multi_object PROPERTIES LABELS "processing;backend")

mib_add_backend_test_executable(mib_processing_object_tracking_test processing/processing_object_tracking_test.cpp)
add_test(NAME backend.processing_object_tracking COMMAND $<TARGET_FILE:mib_processing_object_tracking_test>)
set_tests_properties(backend.processing_object_tracking PROPERTIES LABELS "processing;backend")

add_executable(json_flatten_roundtrip_test
    ${PROJECT_SOURCE_DIR}/tests/frontend/json_flatten_roundtrip_test.cpp
    ${PROJECT_SOURCE_DIR}/src/frontend/utils/JsonFlatten.cpp
)
target_include_directories(json_flatten_roundtrip_test PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_compile_definitions(json_flatten_roundtrip_test PRIVATE
    MIB_PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
target_link_libraries(json_flatten_roundtrip_test PRIVATE Qt6::Core)
set_target_properties(json_flatten_roundtrip_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
)
add_test(NAME frontend.json_flatten_roundtrip COMMAND $<TARGET_FILE:json_flatten_roundtrip_test>)
set_tests_properties(frontend.json_flatten_roundtrip PROPERTIES LABELS "frontend;utility")
