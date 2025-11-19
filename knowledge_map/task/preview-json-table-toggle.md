Title: Preview JSON table toggle
Date: 2025-11-18
Owner: frontend

Context
- Add a toggle in Preview tab lower bottom (ConfigTabs → App config) to switch between formatted JSON and a table.
- The table flattens arbitrary JSON using dot-path keys and joins arrays.

Implementation
- UI: Added a checkable “Table” QToolButton. A QStackedWidget switches between `QPlainTextEdit` and a `QTableView`.
- Model: New `frontend::JsonTableModel` (`QAbstractTableModel`) and `frontend::jsonutil::flattenJsonForTable` utility.
- Persistence: Mode is saved in QSettings key `Preview/ShowTable`.
- Logging: spdlog used for JSON parse errors.

Usage
- Open Preview → bottom “App config (config.json)” tab.
- Click “Table” to show a table view of the current editor JSON.
- Edits update the table with a short debounce while the table is visible.
- Top-level arrays of objects become rows; nested fields use dot paths; arrays become joined strings.

Files
- `include/frontend/JsonFlatten.h`, `src/frontend/JsonFlatten.cpp`
- `include/frontend/JsonTableModel.h`, `src/frontend/JsonTableModel.cpp`
- `include/frontend/ConfigTabs.h`, `src/frontend/ConfigTabs.cpp`
- `CMakeLists.txt`: sources added

Notes
- Keeps existing JSON editor; no changes to file i/o semantics.
- For very large JSON, consider row caps/lazy population in a future iteration.
*** End Patch*** }でした to=functions.apply_patch code```json

