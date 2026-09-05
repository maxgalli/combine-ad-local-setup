# CMSHistSum codegen — plan, findings, and open questions

Working notes for PR [#1252](https://github.com/cms-analysis/HiggsAnalysis-CombinedLimit/pull/1252)
("Add codegen implementations of CMSHist classes (2)"), covering work done
2026-09-03 → 2026-09-05. Written as a handoff: what was decided, what was
built, what was measured, and what is still unresolved.

Scope is **`CMSHistSum` only**. `CMSHistFunc` + `CMSHistErrorPropagator` need
the same treatment (they are what `text2workspace.py` emits *without*
`--use-histsum`) but were deliberately left alone.

---

## 1. Where the work lives

| | |
|---|---|
| Repo | `combine-ad-local-setup/HiggsAnalysis/CombinedLimit` |
| Branch | `ad_dev_maxgalli` (the PR branch) |
| PR head on the fork | `0cbfc3c4` |

Five commits on top of the PR head, none pushed:

```
eef22f40  Extract the Barlow-Beeston solve into a pure kernel
66a22cb9  Apply Barlow-Beeston bin shifts in the generated CMSHistSum code
39f46c59  Implement the CMSHistSum analytical integral in codegen
77e6043f  Emit CMSHistSum constants into xlArr instead of baking bin values
6f4392f8  Extract a pure CMSHistSum kernel and evaluate() through it
```

Uncommitted (the route-(a) data plumbing, section 6):

```
 M interface/CMSHistSum.h      (dataCounts() accessor)
 M src/Combine.cc              (feed per-channel data to CMSHistSum under codegen)
```

---

## 2. The plan we adopted

From guitargeek's plan in PR
[#1162](https://github.com/cms-analysis/HiggsAnalysis-CombinedLimit/pull/1162),
adapted to land **inside Combine first**, upstreaming to RooFit later.

Target shape of generated code:

```cpp
double nll(double *params, const double *obs, const double *xlArr)
// params : live RooRealVar / RooCategory values      — differentiated
// obs    : dataset (bin centres, weights, weights²)
// xlArr  : per-workspace constants                   — frozen
```

with analytical Barlow-Beeston layered on top as two further functions:

```cpp
double analytical_minim(double *params, const double *obs, const double *xlArr);
double nll_with_analytical_bb(double *params, const double *obs,
                              const double *xlArr, bool doMinim);
```

Both of guitargeek's prerequisites were already resolved before we started:
Combine [#1137](https://github.com/cms-analysis/HiggsAnalysis-CombinedLimit/pull/1137)
merged 2025-12-04, clad [#1637](https://github.com/vgvassilev/clad/issues/1637)
closed 2025-11-13. The plan had stalled, not gone stale.

### Drawing the line: constants vs live parameters

The whole correctness question. Everything below is fixed for the lifetime of
the workspace and belongs in `xlArr`; the rest must be read from `params` on
every evaluation.

| Member | Channel |
|---|---|
| `storage_` (template arena) | xlArr |
| `process_fields_`, `vmorph_fields_` | xlArr |
| `binerrors_` | xlArr |
| `bintypes_` (+ offsets, binpar index map) | xlArr |
| `vtype_`, `vsmooth_par_` | xlArr |
| bin edges | xlArr |
| `coeffpars_` | **params** |
| `morphpars_` | **params** |
| `binpars_` | **params** |
| `sentry_`, `binsentry_`, `fast_mode_`, `vertical_prev_vals_` | neither — caching state, no place in generated code |

The original PR put the *morphed bin values* in `xlArr`. Those sit downstream
of `morphpars_`, so freezing them makes the generated likelihood independent of
every shape nuisance.

---

## 3. Phases: what was done

### Phase 1 — pure kernel, `evaluate()` through it (`6f4392f8`)

Moved the arithmetic of `updateMorphs()` / `updateCache()` into
`CombineMathFuncs.h` as pure, allocation-free functions over flat arrays:
`cmsHistSumKahan`, `cmsHistSumSmoothStep`, `cmsHistSumMorph`,
`cmsHistSumAccumulate`. `CMSHistSum` gained a flat arena built once by
`buildFlatArena()`; caches became a memoisation layer around the kernel.

**Verified bit-identical** (17 significant digits) against the branch without
the patch, on both reproducers.

Two traps found only because the criterion was bit-identity rather than a loose
tolerance — a first attempt looked fine at 12 digits and was wrong:

* `FastTemplate::CropUnderflows` floors at **`1e-9`**, not at 0.
* `FastTemplate::Integral` uses **Kahan** summation (`DefaultAccumulator =
  KahanAccumulator`). A naive sum shifted `LogQuadLinear` processes by ~6e-13.

Deliberately excluded from the kernel path: the `FAST_VERTICAL_MORPH` fast path
(`DiffMeld` updates `compcache_` incrementally against `vertical_prev_vals_`,
so it is history-dependent and cannot be pure — it is opt-in and off by
default), and external morphs (not yet in the arena).

### Phase 2 — constants into `xlArr` (`77e6043f`)

`codegenImpl(CMSHistSum&)` no longer calls `evaluate()` and bakes the result.
The generated code now reads:

```cpp
cmsHistSumValues(5, 20, 74, xlArr+0, t2, t3, xlArr+6460, t4, xlArr+6560,
                 t5, t1160, ...)
double t1160[]{params[0], params[2], params[4], ...}   // 74 live morphs
```

The morph and process sum are emitted **once per evaluation**, outside the bin
loop, via `addToCodeBody(..., isScopeIndep=true)`, so the per-bin cost is a
single lookup rather than a full re-morph (otherwise O(nBins²)).

### Phase 3 — analytical integral (`39f46c59`)

Replaced `return "3.0";` with a real sum over the value table weighted by bin
width, mirroring `analyticalIntegral` → `FastHisto::IntegralWidth()`, Kahan
included. `codegenImpl` and `codegenIntegralImpl` must name the same table;
`CodegenContext::makeValidVarName` is **not public** in ROOT 6.41 and
`getTmpVarName()` cannot be shared across the two calls, so the name is derived
deterministically from the node name.

This was a genuine blocker: with a constant garbage normalisation the
likelihood is wrong regardless of the bin values. The 5-channel workspace
converged for the first time after this.

### Phase 4a — Barlow-Beeston bin shifts (`66a22cb9`)

`buildFlatArena()` flattens `bintypes_` into values + per-bin offsets and
records which element of `binpars_` each entry consumes, walking `(j, i)` in
exactly the order `initialize()` uses to fill `vbinpars_`.
`cmsHistSumValues()` applies all three modes (lumped Gaussian, per-process
Poisson, per-process Gaussian) before the trailing crop.

### Phase 4b (started) — BB solve as a pure kernel (`eef22f40`)

`cmsHistSumSolveBarlowBeeston(valsum, toterr, dat, gobs)` — the closed-form
quadratic root — extracted from `runBarlowBeeston()`, which now calls it. No
behaviour change under the `combine` backend (the only one that reaches
analytic BB at all).

---

## 4. Measurements

### Best-fit `r`, three backends

| workspace | `combine` | `cpu` | `codegen` |
|---|---|---|---|
| `ws5` (5 channels × 1 bin) | +1.276 | +1.337 | +1.320 |
| `ws1ch` (1 channel × 5 bins) | +5.563 | +2.820 | +4.885 |

**`cpu` is not a reference.** The backends disagree with each other before
codegen enters the picture. On `ws1ch` they span 2.7 in `r`; the likelihood
there is flat enough that a fit discriminates nothing. Judging codegen by
"does it match cpu" is invalid — this was a mistake made repeatedly during
this work.

### Pointwise, generated function vs cpu evaluation

Via the macro from `writeCodegenDebugMacro`, the only harness that actually
evaluates the generated code:

| point | generated | cpu | rel |
|---|---|---|---|
| nominal | −4802.2555057811478 | −4802.2554117629525 | 2e-8 |
| BB = 1.0, `CMS_btag_hf` = 0.7 | −4795.7745416719708 | −4795.7744949161824 | 1e-8 |

The displaced point matters: at nominal every bin shift is identically zero
(type 1 gives `toterr·0`, type 2 gives `(1−1)·…`), so it exercises nothing.

### Gradient

Clad vs central differences (`eps = 1e-6`), sampled parameters:

```
idx   2  numr=-0.20141897   clad=-0.20141929   rel=3.2e-07
idx  90  numr=-0.049063146  clad=-0.049062916  rel=2.3e-07
idx 150  numr= 0.078975972  clad= 0.078976412  rel=4.4e-07
idx 183  numr= 0.11215752   clad= 0.11215736   rel=1.6e-07
```

i.e. finite-difference noise. **The gradient is not the problem.**

---

## 5. Methodology traps (read this before trusting any measurement)

**`combineCreateNLL(...)` + `getVal()` does not evaluate the generated code.**
Proved by making `cmsHistSumLookup` return a constant `42.0`: the "codegen" NLL
stayed bit-identical to cpu. The JIT genuinely runs (it reports function and
gradient compilation times), but the value comes back from the ordinary
evaluation path. Several hours of "cpu and codegen agree" results earlier in
this work were measuring cpu against cpu.

Valid channels: the `combine` CLI fit, or the macro written by
`writeCodegenDebugMacro`.

**Using the debug macro.** It must be given Combine's header — it only includes
ROOT's `MathFuncs.h`, so it fails with `no member named 'asymPow'` otherwise:

```cpp
#include <HiggsAnalysis/CombinedLimit/interface/CombineMathFuncs.h>
#include "gen_lib.C"     // the macro, with its driver function renamed
void drive(){ printf("%.17g\n",
  roo_codegen_0(parametersVec.data(), observablesVec.data(), auxConstantsVec.data())); }
```

It **cannot be compiled** — `CladDerivator.h` errors with *"This file must not
be included by compiled programs"*. Gradient checks therefore run interpreted
and are slow enough (184 parameters × 2 evaluations) that they must be
subsetted; a full sweep ran >28 minutes before being killed.

**A uniform sabotage is not a valid probe.** Scaling every bin by a constant
largely cancels in a normalised pdf. Use a bin-dependent or constant return.

**`ulimit -s unlimited` is required.** `text2workspace.py` otherwise segfaults
in `TStreamerInfo::WriteBufferAux` → `AsymPow::Streamer` on the full
combination. Reproduces on plain `main`; nothing to do with this PR.

**`dev3/latest` rolled from Thu to Fri mid-work**, so not all measurements share
one ROOT. Pin a dated path (`/cvmfs/sft-nightlies.cern.ch/lcg/views/dev3/Thu/…`)
for anything comparative.

---

## 6. Where it stopped: analytic Barlow-Beeston

Two routes were considered for emitting the solve.

### Route (b) — separate generated functions (guitargeek's design)

**Blocked structurally.** Two facts:

1. **`CMSHistSum` cannot see the data.** The solve needs `valsum`, `toterr`,
   the observed count and the global observable. The last two come from
   `data_`, filled by `CMSHistSum::setData()`, whose only caller is
   `CachingAddNLL::setData` (`CachingNLL.cc:800`). The codegen backend bypasses
   `CachingSimNLL`, so `data_` is empty — and `setAnalyticBarlowBeeston` guards
   on `if (flag && data_.size())`. The data belongs to the NLL above the node,
   not to the node. This is *why* guitargeek's plan puts the analytical
   minimisation in a separate class on top of the likelihood; it is forced, not
   stylistic.
2. **`RooFuncWrapper` is not public in ROOT 6.41.** The public codegen surface
   is only `CodegenContext.h` and `CodegenImpl.h`. Combine cannot subclass it
   or drive its JIT, so "generate, then append our own functions and compile
   them Combine-side" does not work.

What route (b) would need: a Combine class above the NLL holding model *and*
data, collecting hooks from the existing `histSums_` / `histErrorPropagators_`
registry in `CachingAddNLL` (no new `RooAbsArg` virtual required — this is the
one place the Combine-first adaptation is cheaper than the upstream design),
emitting the solve into the generated TU via
`CodegenContext::addToGlobalScope`, and assigning into `params[k]` (which
`ctx.getResult(binParArg)` yields directly, so no index bookkeeping). Clad
#1637 being closed means mutating `params` should differentiate.

### Route (a) — inline the solve (attempted, partially done)

The data problem is fixable here because `combineCreateNLL` *has* the dataset.
The uncommitted change adds `combineFeedDataToCMSHistNodes()`, which splits the
data per channel (mirroring `CachingAddNLL::propagateData()`) and calls
`setData()` on every `CMSHistSum` when the backend is `codegen`/
`codegen_no_grad`. Verified:

```
backend=cpu      CMSHistSum nodes=1  with data=0
backend=codegen  CMSHistSum nodes=1  with data=1
backend=codegen  CMSHistSum nodes=5  with data=5
```

All six fit results unchanged, correctly — nothing consumes `data_` yet.

Note: `RooAbsData::split` returns `std::vector<std::unique_ptr<RooAbsData>>` in
ROOT 6.41, not the `TList*` the older `CachingNLL` idiom expects.

**Still blocked on a design question, which applies to both routes:** the bin
parameters cannot be both constant and assignable.
`setAnalyticBarlowBeeston(true)` sets them constant so the minimiser stops
floating them — but constants get baked into `xlArr`, leaving no `params[k]`
slot to write the solution into. The design needs them present in `params` yet
not minimised over, which the current wiring does not express. This is also
exactly the CAT-hackathon requirement that analytic minimisation be switchable
*per parameter after the likelihood is built*, so it belongs in a driver rather
than in `codegenImpl`.

Secondary: the global observables come from walking
`vbinpars_[j][0]->valueClients()` inside `setAnalyticBarlowBeeston`; that walk
has to happen at codegen time and the values go into `xlArr`, which means
calling that function during code generation — with the side effect above.

---

## 7. Other findings worth keeping

* **The old `codegenImpl` had a heap-corruption bug.** `double free or
  corruption (!prev)` inside `createNLL` on the 5-bin workspace,
  nondeterministic. Superseded by the rewrite, but any measurement taken from
  the pre-phase-2 code is untrustworthy.
* **ROOT's fallback for unsupported classes is not fatal**: it logs
  *"Translate function for class … has not yet been implemented"* and
  substitutes the literal `1.0`, so the fit runs on a wrong likelihood. Check
  values, never "did it run". (Known/expected, per Massimiliano.)
* **`commit 6ac692ba`** (in the PR, from petro.zarytskyi) switches
  `lower_bound` → `upper_bound` in **seven** places across `FastHisto`,
  `FastHisto2D`, `FastHisto3D`. It changes bin-edge semantics for *every*
  existing user of these templates, and **neither reproducer exercises it** —
  the pruned card's channels are single-bin. It needs its own unit test.
* **JIT time dominates on the real combination**: 38 min for the 48-channel
  workspace versus 1m54s for the cpu backend, essentially all compilation.
  If that does not improve, AD is only viable for repeated-minimisation
  workflows (profile scans), which turns the final benchmark into a go/no-go.
* **Non-smooth points**: `CropUnderflows` is a clamp at `1e-9` and
  `smoothStepFunc` is piecewise. Differentiable almost everywhere, but a bin
  pinned at the clamp has zero derivative.

---

## 8. Reproducers

Inputs live in `combine-ad-local-setup/inputs/` (gitignored), from
CMS-HIG-24-011 (off-shell Higgs width, 48 channels, 19 processes, 244
nuisances).

| name | build | shape | fit time |
|---|---|---|---|
| full combination | `source t2w.sh` | 48 ch, 5.9 MB ws | cpu 1m54s / codegen 38m |
| `ws1ch` | single card `2016_HIPM_0j/SR_ggH_OFF_0j` | 1 ch × **5 bins**, 74 morphs | ~10 s |
| `ws5` | `inputs/pruned_top5.txt` | 5 ch × **1 bin** | ~15 s |

`pruned_top5.txt` is the five highest-yield channels, built with
`combineCards.py`, all systematics and `autoMCStats` preserved (163 nuisances
in, 163 out). Caveat: ranking by yield selects the top control regions, which
are all **single-bin**, so it does not exercise multi-bin behaviour, the
bin-lookup path, or the `upper_bound` change. `ws1ch` is the multi-bin case but
its likelihood is too flat in `r` for fit comparisons.

Helper scripts in `inputs/`: `dump_codegen.cxx` (build NLL + write debug
macro), `scan_param.cxx`, `random_check.cxx`, `rank_channels.py`.

---

## 9. Suggested next steps

1. **Settle the parameter-state question** — how bin parameters stay in
   `params` while being excluded from minimisation. Everything else in phase 4b
   depends on it.
2. **Build the pointwise harness properly** as a committed test, wrapping the
   debug-macro comparison, so future changes have a criterion that can fail.
   Fit-to-fit comparison is not usable given the backend spread.
3. **A better reproducer**: multi-bin *and* well-conditioned. Candidates are the
   highest-yield signal regions — `2018_1j/SR_ggH_OFF_1j` (4514 events),
   `2018_2j/SR_ggH_OFF_2j` (3732), `2018_0j/SR_ggH_OFF_0j` (3404).
4. **Understand the backend spread** (`combine` vs `cpu` 0.06 on `ws5`, 2.7 on
   `ws1ch`). Pre-existing and independent of this PR, but it makes validation
   much harder and may indicate something worth fixing on its own.
5. **A unit test for the `upper_bound` change**, independent of codegen.
6. Then `CMSHistFunc` + `CMSHistErrorPropagator`, which need the same
   treatment before codegen covers analyses built without `--use-histsum`.
