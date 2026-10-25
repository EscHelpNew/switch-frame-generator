# LSFG Switch Demo Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans; implement inline in this session. Steps use checkboxes.

**Goal:** Build a netloadable bouncing-ball LSFG demo and document its game integration.
**Architecture:** Pinned open-source extraction/compiler pipeline on Windows produces a private DKSH cache. A reusable deko3d runtime consumes that cache on Switch; a small scene exercises it with controlled frame pacing.
**Tech Stack:** C++20, devkitA64/libnx/deko3d, Python 3, CMake/Ninja, SPIRV-Cross and pinned UAM.
**Spec:** ../specs/2026-10-25-lsfg-switch-demo-design.md (approved by user 2026-10-25).

## Global Constraints

- First Nintendo Switch; no proprietary DLL or derived shader blobs in Git.
- 192 MiB LSFG image pool budget; one generated frame, Performance, flow 1/4, high precision.
- Render 30, LSFG 30→60, native 60; internal and output 1280x720 (later user requirement).
- Netloader first; no reuse of another application's title ID or keys.
- Preserve upstream copyright and license notices.

## Review Focus

- DLL version mismatch must stop preparation with a useful error.
- Corrupt cache must fail validation before GPU use.
- Switching modes must reset history and preserve chronological presentation.
- GPU resource teardown must follow queue completion.
- FPS counters must distinguish generated frames and CPU submission from GPU execution.

### Task 1: Reproducible host preparation

**Files:** tools/fetch_sources.py, tools/prepare.cpp, CMakeLists.txt, third_party/, tests/, build.py.
**Interfaces:** host `lsfg_prepare DLL CACHE_ROOT`; cache uses upstream ABI v2; build script reports actual output paths.
- [x] Obtain pinned sources and licenses; test real-DLL preparation using upstream validation before adapting runtime.
- [x] Build host compiler; require all selected shaders compile and cache round-trips successfully.
- [x] Exercise malformed DLL and corrupted cache using upstream checks.

### Task 2: Reusable Switch runtime and controlled scene

**Files:** include/sfg/generator.hpp, source/generator.cpp, include/sfg/scene.hpp, source/main.cpp, shaders/, Makefile, tests/scene_test.cpp.
**Interfaces:** `Generator::create(device,width,height,cache,error)`, `input()`, `record(cmd,generated,original,error)`, `reset()`; caller owns queue and drains before destruction.
- [x] Write and run failing deterministic physics tests (fall, floor bounds, bounce, fixed-step independence).
- [x] Implement scene, reusable cache-only generator, shaders and presentation loop.
- [x] Prepare private romfs, build ARM64 ELF/NRO and inspect artifacts.
- [x] Check warmup, chronological frame order, resets, fences and shutdown in review. Switch hardware run explicitly deferred by user.

### Task 3: Deployment and integration documentation

**Files:** run.ps1, docs/INTEGRATION_RU.md, README.md, THIRD_PARTY.md, docs/VALIDATION.md.
**Interfaces:** `python build.py`, `./run.ps1 -Address <IP>`; published runtime API from Task 2.
- [x] Document exact build/link inputs, cache preparation, initialization, frame scheduling, barriers, reset and cleanup.
- [x] Explain adapting other renderers, HUD composition and netloader workflow from switch-monitor.
- [x] Run host suite and fresh target build; record limits. User explicitly withheld console deployment.
- [x] Obtain one fresh final code review; address important findings and rerun relevant checks.

### User extension: Desktop GPU demo

- [x] Use the same translated GLSL and graph through OpenGL4.5; keep native720p.
- [x] Build and run on the actual PC GPU; smoke360frames across all modes.
- [x] Correct and verify offscreen top-down orientation and display orientation.
- [x] Add visible on/off button, G shortcut, ball hit detection and jump; test physics.
- [x] Start interactive app visibly and leave it open for the user.
