# Third-party source provenance

## Vendored code

`third_party/drastic/` is a selective copy of
[NaGaa95/DrasticDS_nx](https://github.com/NaGaa95/DrasticDS_nx) at
`23bd547251b5ab2fafacfb600867b99562bb88aa`:

- `third_party/lsfg-deko3d/`: portable extraction, translation, DKSH cache and compute graph, GPL-3.0-or-later unless a file states otherwise.
- `source/deko_compiler/`: UAM bridge and build integration, GPL-2.0-or-later.
- `source/lsfg/drastic_deko_lsfg.*`: reference native execution adapter, GPL-3.0-or-later.
- `tools/tests/deko_lsfg_*.cpp`: upstream validation and recording checks, GPL-3.0-or-later.

Copyright notices remain in every file. The native library descends from
Dolphin-NX and Cemu/NetherSX2; see its README and individual notices.

Changes to vendored code: Windows conditional build definitions in Uam.cmake;
temporary-file diagnostic capture in UamBridge.cpp for MinGW; Windows mkdir/commit
calls in cache_store.cpp. No algorithm changes.

`source/generator.cpp` derives from that native adapter and replaces on-console
DLL preparation with verified, cache-only loading; it accepts the demo's 2x,
high-precision, Performance, quarter-flow configuration. The recording test derives
from the upstream native recording test. Both retain GPL-3.0-or-later notices.

## Downloaded build dependencies

Downloaded only to ignored `build/deps/`, never fetched from an unpinned branch:

| Component | Repository | Commit | License |
| --- | --- | --- | --- |
| UAM fork | PalindromicBreadLoaf/uam | c0d76aa9107d67edcd35a1b717ace87ee7939c68 | Individual Mesa/UAM notices, predominantly MIT |
| SPIRV-Cross | KhronosGroup/SPIRV-Cross | ebe2aa0cd80f5eb5cd8a605da604cacf72205f3b | Apache-2.0 or MIT |

Archive hashes are recorded locally in `build/deps/*-provenance.json`.
Mako 1.4.3 (MIT), MarkupSafe 3.0.4 (BSD-3-Clause) and Ninja 1.13.0
(Apache-2.0) are host build tools, installed under ignored `build/` if missing.
devkitPro/devkitA64/libnx/deko3d must be installed separately and retain their licenses.

## Project license

The original [MIT LICENSE](LICENSE) is preserved. The LSFG-linked adapter, demo,
and preparation utility are GPL-3.0-or-later; see [GPL text](LICENSE.GPL-3.0.txt).
Do not interpret the original MIT file as relicensing vendored GPL code.

The user's Lossless.dll and generated shaders are local inputs/artifacts,
excluded from source control. No Lossless Scaling proprietary files are supplied
by this source repository.
