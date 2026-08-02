# Vendored schemas

Kept in-tree so validation runs offline and reproducibly in CI (no network in tests).

| File | Source | Retrieved | Notes |
|------|--------|-----------|-------|
| `sarif-2.1.0.json` | <https://json.schemastore.org/sarif-2.1.0.json> (`$id`: <https://raw.githubusercontent.com/oasis-tcs/sarif-spec/master/Schemata/sarif-schema-2.1.0.json>) | 2026-08-03 | Official OASIS SARIF 2.1.0 schema, as required by `docs/06_TESTING_QA.md` §6. Used only to validate our own output in tests; not redistributed as part of any binary. |

Refresh by re-downloading from the source URL and re-running
`python tests/format/validate_sarif.py`.
