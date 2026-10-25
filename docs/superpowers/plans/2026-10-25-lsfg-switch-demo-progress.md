# Execution ledger — plan: 2026-10-25-lsfg-switch-demo.md

- User approved design and implementation; additionally requested an exact game integration guide.
- Execution: native inline. Files remain in the user-selected repository where their DLL was placed; no worktree, commits, publishing or console title changes are needed for this prototype.
- Baseline: only README/LICENSE plus design/.gitignore; no existing build or tests.
- Pre-flight: host cache ABI v2 is consumed by target runtime; the same vendored headers and cache validation sources will be used on both platforms.
- Task 1: complete — real DLL prepared; upstream validation passed100native module compiles and4800bindings; malformed/corrupt inputs checked.
- Task 2: local implementation complete — native1280x720ARM64NRO built; recording test24frames passed; hardware test explicitly deferred by user.
- Task 3: integration guide, netloader and build scripts written; 7local checks passed; final code review found no Critical/Important issues. Nullable initialization handling in documentation corrected.
- User extension: build and launch a desktop app to see LSFG. Confirmed actual AMD Radeon GPU with OpenGL4.6. Reuse the exact translated GLSL and graph/binding metadata; execute through OpenGL, not Maxwell DKSH. Native720p and same scene/modes. No Switch deployment.
- Desktop extension complete: all25GLSLprograms compiled and ran; smoke360frames, gl_error0, three desktop capture/log checks passed. Renderer orientation fixed after fresh review; internal row0 now matches Switch logical top, display is flipped only during blit.
- Mode statistics reset on each transition, with smoke long enough to report all modes. Scene hit/jump added after a failing compile test; green scene test passed in full build.
- Final verification: build.py --test exit0; seven local artifact/runtime tests passed; upstream100module/4800binding checks passed. Desktop remains running interactively for user; latest PID52432. No Switch network actions performed.
