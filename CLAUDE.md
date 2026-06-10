# CLAUDE.md

All agent guidance for this repository lives in [`AGENTS.md`](AGENTS.md).
Read it first — it is the map to the knowledge vault (`knowledge_map/`),
docs (`docs/`), build presets, and verification gates.

Two rules worth repeating:

1. **Vault maintenance is mandatory.** Every code change ships with matching
   vault updates in the same commit/PR. The source-file to vault-note mapping
   is in [`knowledge_map/Vault-Maintenance.md`](knowledge_map/Vault-Maintenance.md).
2. **Run `python3 scripts/check_docs.py` before committing** any change that
   touches markdown, the vault, or moves files.
