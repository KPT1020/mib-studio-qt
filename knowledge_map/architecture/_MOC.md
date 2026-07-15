# Architecture — MOC

> Map of Content for the architecture cluster.

- [[Overview]] — layered design summary
- [[AppBackend]] — composition root, owns + wires all services
- [[Threading-Model]] — main, capture, processing, realtime, autofocus, etc.
- [[Data-Flow]] — camera → FrameStore → processing → HDF5
- [[Rust-Bridge]] — cxx bridge over BackendFacade (React + Tauri, epic #246)

**Up**: [[../README|Vault home]] · **See also**: [[../services/_MOC|Services MOC]]
