# Fixtures

| path | content |
|---|---|
| `compiler/{valid,quirks,errors,fuzz-regressions}/` | the compiler compatibility corpus: `<case>.txt` + `<case>.expected.json` (see [../docs/compatibility.md](../docs/compatibility.md)) |
| `compiler/plans/` | sibling plans read by `CHANGE_OF_PLAN` cases |
| `vocab/` | the corpus's symbol files in the 1996 layout: `enumIDs.h`, `AnimData/Animassm.txt`, `Sndfiles.lst`, `Infobtxt.lst` (a small made-up vocabulary) |
| `MANIFEST.json` | the executable the expectations came from (hash), the vocabulary files' hashes, every fixture's hash and outcome |

Every expectation was produced by the original 1996 ADLIB compiler, run with the files in
`vocab/` as its symbol files (`"expected_from": "oracle"`), except where the original crashes or
emits uninitialised memory (`"expected_from": "adlibc (...)"`). Do not edit them by hand; see
[../CONTRIBUTING.md](../CONTRIBUTING.md). The positions of the symbols in `vocab/` are part of the
expectations: append, do not reorder.
