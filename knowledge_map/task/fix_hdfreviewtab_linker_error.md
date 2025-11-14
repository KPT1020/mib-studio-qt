Title: Fix LNK2019 unresolved external symbol for frontend::HdfReviewTab constructor

Date: 2025-11-14

Context:
- Build failure for target `mock_studio_qt` with LNK2019 unresolved external symbol:
  `frontend::HdfReviewTab::HdfReviewTab(backend::AppBackend&, QWidget*)` referenced from `MainWindow`.

Root cause:
- `src/frontend/HdfReviewTab.cpp` (and header) existed but were not included in the `FRONTEND_COMMON_SOURCES` list in `CMakeLists.txt`, so the object file was never compiled/linked.

Fix:
- Add `src/frontend/HdfReviewTab.cpp` and `include/frontend/HdfReviewTab.h` to `FRONTEND_COMMON_SOURCES` in `CMakeLists.txt` so both `mib_studio_qt` and `mock_studio_qt` link against it.

Files changed:
- `CMakeLists.txt` (added two entries under `FRONTEND_COMMON_SOURCES`)

Verification:
- Reconfigured and rebuilt `Release`:
  - `mock_studio_qt.exe` and `mib_studio_qt.exe` built successfully.
  - No more unresolved externals.
  - Only warnings from `windeployqt` regarding translations/ICU DLLs (non-blocking).

Notes:
- Followed workspace rules: used existing code and build system; no new utilities introduced.


