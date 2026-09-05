# Reproducer inputs and helpers

Large inputs (the CMS-HIG-24-011 datacards and shape files) are gitignored.
Only the helper scripts below are tracked.

## Valid harnesses

| file | what it does |
|---|---|
| `dump_codegen.cxx` | builds the NLL through `combineCreateNLL` and writes the codegen debug macro. The macro is the **only** way to evaluate the generated function. |
| `rank_channels.py` | ranks datacards by total expected events (used to build `pruned_top5.txt`). |

## Harnesses that DO NOT measure codegen — kept only as a warning

`scan_param.cxx`, `random_check.cxx`, `scan_nll.py`, `dbg_nll.py`,
`dump_codegen.py` all compare backends by calling `combineCreateNLL(...)` and
then `getVal()`.

**That does not evaluate the generated code.** Proved by making
`cmsHistSumLookup` return a constant 42.0: the "codegen" NLL stayed
bit-identical to cpu. The JIT runs, but the value comes back from the ordinary
evaluation path. Any agreement these report is cpu compared against cpu.

Use the `combine` CLI, or the debug macro, instead. See
`../CMSHISTSUM_CODEGEN_NOTES.md` section 5.
