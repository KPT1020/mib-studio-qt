# SqliteService

> Minimal wrapper over `sqlite3` for any future metadata needs. Currently a
> thin shell.

**Source:** `src/backend/services/SqliteService.cpp`,
`include/backend/services/SqliteService.h`

## API

```cpp
bool initialize(const std::string& dbPath);
bool execute(const std::string& sql);
```

## Threading

Caller-thread only. No internal locking beyond sqlite3's default.

## Gotchas

- `AppBackend::initialize` creates the DB at `<dataDir>/app.sqlite3`.
- No schema migrations yet — code that adds tables should be defensive with
  `CREATE TABLE IF NOT EXISTS`.
