# Testing

Use CTest as the entry point for C++ test executables. The Windows build keeps
test `.exe` files beside the app and DLLs under `build/Debug` or
`build/Release`; that is expected for runtime DLL lookup. Test inputs and
generated outputs should be supplied by CTest commands or tool runners, not by
manual double-clicking in the build folder.

## Windows

Fast Debug backend tests:

```powershell
cmake --preset windows-default
cmake --build --preset windows-default-build
ctest --preset windows-debug-test
```

Run a single registered test:

```powershell
ctest --test-dir build -C Debug -R backend.processing_object_tracking --output-on-failure
```

List tests without running them:

```powershell
ctest --test-dir build -C Debug -N
```

Network-backed integration tests are labeled `integration` and excluded from
the fast Windows presets. Run them explicitly:

```powershell
ctest --preset windows-debug-integration-test --output-on-failure
```

## Input-Driven Tests

Do not run input-driven binaries directly unless you also provide their
required arguments. Prefer these wrappers:

```powershell
python tools/kin10_run_hf_dataset_test.py `
  --out-dir build/test-output/kin10_hf_dataset_pipeline `
  --binary build/Debug/kin10_hf_dataset_pipeline_test.exe

python tools/kin6_generate_hf_evidence.py `
  --out-dir review_artifacts/KIN-6 `
  --binary build/Debug/kin6_batch_pipeline_evidence.exe `
  --app-proof-binary build/Debug/kin6_mib_app_capture_proof.exe
```

Keep committed fixtures under `tests/fixtures/...` when a test only needs small
static input. Keep generated artifacts under `build/test-output/...` or
`review_artifacts/...`; both are ignored by git.
