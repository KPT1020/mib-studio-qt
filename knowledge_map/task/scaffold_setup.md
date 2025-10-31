# Qt scaffold (API-driven, Qt/CMake)

- Scope: Create base structure and document architecture assumptions
- Directories: `src/frontend`, `src/backend`, `include`, `cmake`, `knowledge_map/task`, `data`
- Assumptions:
  - Qt 6 via vcpkg (`qtbase`), CMake build, MSVC x64
  - Logging with spdlog (no std::cout)
  - Separate frontend (Qt Widgets) and backend (services, APIs)
  - DB: SQLite for metadata, HDF5 for structured/bulk data
  - eGrabber headers from `C:/Program Files/Euresys/eGrabber/include`
- Next:
  - Add CMake/vcpkg configs (Qt, spdlog, sqlite3, hdf5)
  - Add backend and frontend stubs
