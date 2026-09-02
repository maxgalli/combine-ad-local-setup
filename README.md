# combine-ad-local-setup

A local development environment for working on **Automatic Differentiation (AD) in
[CMS Combine](https://github.com/cms-analysis/HiggsAnalysis-CombinedLimit)** — an
AlmaLinux 9 container that mounts CVMFS and builds Combine against an LCG nightly
carrying ROOT master.

## Why this exists

Combine's RooFit codegen/AD support is compiled in only on recent ROOT:

```cpp
// src/CombineCodegenImpl.cxx
#if ROOT_VERSION_CODE >= ROOT_VERSION(6, 36, 0)
```

The pixi environment ships **ROOT 6.34**, so on pixi that entire file is preprocessed
away — `combine` builds and runs fine, but the AD path silently does not exist. AD
development therefore needs ROOT ≥ 6.36, and in practice ROOT *master*, because the
codegen API is still moving.

ROOT master is available through the LCG **`dev3`** nightly stack. LCG is distributed
**only** via CVMFS — there is no tarball or prebuilt image of the stack — so the
container has to mount CVMFS itself. That is the whole trick here.

## Requirements

- Docker (Desktop, Colima, …). Apple Silicon and x86_64 hosts both work.
- Network access to CERN's CVMFS stratum-1. No CERN account or VPN needed.
- ~10 GB of disk for the CVMFS cache volume.

On Apple Silicon this runs **natively on arm64** — LCG publishes
`aarch64-el9-gcc{13,14,15,16}-opt` for `dev3`, so no Rosetta or x86 emulation is involved.

## Quick start

```bash
./combine-dev.sh /path/to/HiggsAnalysis/CombinedLimit
```

Builds the image on first run, starts a container named `combine-dev`, and drops you into
a shell with the LCG environment sourced:

```
LCG view : /cvmfs/sft.cern.ch/lcg/views/dev3/latest/aarch64-el9-gcc13-opt/setup.sh
ROOT     : 6.41.01
source   : /src   build: /build   install: /install
commands : combine-build
```

Then inside the shell:

```bash
combine-build          # cmake + make install -j$(nproc)
combine --help
```

Edit the source on your host as usual — it is bind-mounted read-write at `/src`. Builds
go to a separate volume at `/build`, so your working tree stays clean.

Re-running `./combine-dev.sh` re-attaches to the existing container.

## Verifying that AD actually works

```bash
./verify-ad.sh
```

This checks the three things that matter, in order:

1. ROOT is new enough,
2. the six `codegenImpl` symbols are **defined** in
   `libHiggsAnalysisCombinedLimit.so` — i.e. the guarded code really compiled rather than
   being stripped by the preprocessor,
3. `scripts/fitRooFitAD.py` produces the same fit with `--backend combine` and
   `--backend codegen`.

Expected output ends with:

```
== codegenImpl symbols (expect 6 defined) ==
  codegenImpl(AsymPow&, RooFit::Experimental::CodegenContext&)
  codegenImpl(FastVerticalInterpHistPdf2&, ...)
  codegenImpl(FastVerticalInterpHistPdf2D2&, ...)
  codegenImpl(ProcessNormalization&, ...)
  codegenImpl(VerticalInterpPdf&, ...)
  codegenImpl(RooParametricHist&, ...)
  total: 6

PASS: codegen backend matches the default backend
```

## What's here

| file | purpose |
|---|---|
| `Dockerfile` | AlmaLinux 9 + CVMFS client + the build/runtime bits LCG does not provide |
| `entrypoint.sh` | mounts `sft.cern.ch` and `sft-nightlies.cern.ch` at container start |
| `combine-dev.sh` | builds the image, starts/re-attaches the container, opens a shell |
| `verify-ad.sh` | end-to-end AD smoke test |

Three named Docker volumes are used: `cvmfs-cache` (**important**, see below),
`combine-build`, `combine-install`.

## Configuration

`combine-dev.sh` reads a few environment variables:

| variable | default | meaning |
|---|---|---|
| `LCG_VIEW` | `dev3/latest` | LCG view; e.g. `LCG_108` for a stable stack |
| `LCG_GCC` | `gcc13` | compiler flavour of the view |
| `COMBINE_DEV_NAME` | `combine-dev` | container name, for running several at once |

```bash
LCG_VIEW=LCG_108 ./combine-dev.sh ~/CombinedLimit    # ROOT 6.36.02, stable
```

## How it works

The container installs the CernVM-FS client and mounts the repositories over FUSE, which
is why it needs `--cap-add SYS_ADMIN --device /dev/fuse`. `CVMFS_HTTP_PROXY=DIRECT` is set
because there is no site squid on a laptop.

Sourcing an LCG view puts a complete, self-consistent toolchain (ROOT, gcc, CMake, boost,
eigen, GSL, …) on `PATH`. The build recipe mirrors the `dev3/latest` job in Combine's own
`.github/workflows/development-tests-ci.yml` ("CI with CVMFS for AD tests"), so failures
here should reproduce there.

## Notes and gotchas

**The CVMFS cache dominates everything.** On a cold cache the first build is painfully
slow — CMake configure took **1m51s** at ~190 KB/s, and `rootcling` can sit at ~0% CPU for
many minutes looking hung while it waits on the network. Warm, the same configure takes
**1 second**. This is why the cache lives in a named volume: do not run the container with
`--rm` and a throwaway cache. If something looks stuck, check `cvmfs_config stat
sft.cern.ch` (columns include hit rate and transfer speed) rather than CPU usage.

**A flaky connection surfaces as I/O errors, not timeouts.** On a poor link you may see:

```
rootcling: error while loading shared libraries: .../libCling.so:
           cannot read file data: Input/output error
```

That is a transient CVMFS fetch failure. Retry on a better connection; nothing is corrupt.

**`dev3` is a nightly.** `dev3/latest` is a symlink into a per-weekday directory
(`.../dev3/Wed/...`) that is overwritten weekly, so a build is only reproducible for about
seven days, and ROOT master can break under you. Pin the dated path, or use `LCG_108`,
when you need stability.

**The base image needs more than you'd expect.** The LCG view provides gcc, binutils and
CMake but *not*:

- `make` → *"CMake was unable to find a build program corresponding to Unix Makefiles"*
- `glibc-devel` → *"cannot find crt1.o / crti.o"*
- ROOT's X11 runtime libs → *"combine: libSM.so.6: cannot open shared object file"*

All are installed by the `Dockerfile`; listed here because the errors are opaque if you
build your own image.

**`RooMultiPdf` warning.** On ROOT master you will see:

```
TClassTable::Add:0: RuntimeWarning: class RooMultiPdf already in TClassTable
```

ROOT master now ships `RooMultiPdf` in `roofitcore` (upstreamed from Combine) while
Combine still defines its own. Harmless today, but two definitions in one process is
fragile — arguably Combine should guard its copy the way it guards the codegen code.

## Reference

- RooFit AD blog post — <https://root.cern/blog/roofit-ad/>
- Extending codegen to new classes — <https://root.cern/doc/master/group__roofit__dev__docs__ad.html>
  (note: the appendices of that page still describe the pre-6.36 `translate()` /
  `CodeSquashContext` API; the `RooPoisson` example shows the current
  `codegenImpl()` / `CodegenContext` form)
- LCG stacks and platforms — <https://lcginfo.cern.ch/>
