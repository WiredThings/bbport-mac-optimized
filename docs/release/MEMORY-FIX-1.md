# Memory fix 1 (0.1.4-mem1)

This release ports existing bmy/bbport-mac work to the Chinese Optimized launcher and
retains performance port 1, the game-language region fix and the original CPU runtime.

## Upstream provenance

- `d6704c65486bf7389ed329a1731f33b6834eea59`: every 64 command-buffer commits,
  free GPU-completed command buffers beyond a 32-idle-buffer working set. Pending
  GPU work is never freed. `BB_CMDBUF_TRIM=0` restores the old behavior. The exact
  compaction was extracted into `bbport_pool_trim.h` for an in-flight safety test.
- `4de5fd1340c8184dd88687fc07631ee2425cb587`: unified-memory budgeting uses the
  macOS graphics footprint ledger (including compressed graphics), rather than
  treating driver-owned Metal allocations as non-graphics memory and shrinking the
  texture budget continuously. This corrects accounting; it is not a RAM hard cap.
- PR #8, commit `827e27c540eece5b0fe2d478b9435d1cb042c72f` (unmerged upstream):
  KosmicKrisp wrapper `a82cadb14cb225eaeadf190d5c9d20ca8ec91744`, Mesa
  `8b06c1fef1ed316217b805d1068fc74afaa40438`, the four matching Mesa patches,
  graphics-pressure diagnostics and `kk-command-memory-check.c`.
  The driver revision predates geometry-shader emulation. This is a pinned upgrade,
  not a switch to latest Mesa.

The entry point enables `BB_KK_POOL_FIX=1` to initialize/reset the spare upload-block
counter included in the upstream patch. `BB_KK_POOL_FIX=0` and `BB_KK_STATS=1` are
available for isolated comparisons and driver-object diagnostics. Existing
`BB_GC_BUDGET_MB` support is retained; no fixed larger texture budget was introduced.
The experimental whole-game shared-memory import remains disabled.

## Build and safety

- Independent driver checkout/build; old installed driver is kept in the rollback app.
- Renderer build uses at most two jobs; all driver/helper builds also use two jobs.
- The dependency script accepts `BB_BUILD_JOBS` and stamps revision plus patches.
  Changed patch series gets its own source directory, preserving old checkouts.
- This host had LLVM 23 and a translator for LLVM 22. A matching translator from
  `KhronosGroup/SPIRV-LLVM-Translator` branch `llvm_release_230`, commit
  `8b89e5dece5af0fc492ec7c03fa8359e8a5070d2`, was built in /private/tmp. No Homebrew
  tools were replaced. Build tools are not shipped with the app.
- Command-memory checker: 128 Vulkan command buffers, 512 buffer copies each,
  20 rounds, verifies copied bytes, exits above a 2 GiB footprint. No game needed.
- No game launch, input replay, save loading or FPS claim is part of offline validation.
  Full-game memory stability, rendering correctness and frame rate remain unverified.

## Packaging

`tools/macos/package_performance_preview.py --memory-driver <driver-output-dir>`
requires a verified 0.1.3 baseline and produces a separate 0.1.4-mem1 app/archive.
It overlays only the native renderer, pinned driver and entry-point scripts; the
localized Swift launcher, CPU runtime and tested dependency binaries are retained.
Existing perf1 shader caches remain in place; user data and game files are untouched.

On this M4 (2026-10-17), the identical 20-round standalone test finished at
310 MiB footprint with the old driver and 87 MiB with the pinned driver plus pool
fix; both verified copied bytes. This is a bounded process-footprint comparison,
not GPU texture usage or a whole-game memory/FPS result.

Validation: in-flight pool trimming test; 6 real SPIR-V modules; 3 launcher,
21 patch, 8 resolution/restart and 11 packaging Python tests passed. Bash process
substitution tests require normal host permissions; packaging path tests used
`TMPDIR=/private/tmp` to avoid the macOS `/var` alias. Initial sandbox-only failures
were rerun in that environment and passed.

Check `outputs/memory-fix-1-verification.json` for artifact hashes and the
measured standalone results. These are not a full-game benchmark or a guarantee
that the Mac cannot run out of memory.
