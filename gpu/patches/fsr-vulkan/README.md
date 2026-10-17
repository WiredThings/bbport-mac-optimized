# Local changes to gpu/third_party/fsr-vulkan

The submodule points at upstream FireBurn/FSR-Vulkan (`c64f093`). `build.sh` applies the
patches here to its working tree when they are not applied yet:

- `0001-...`: `BB_FSR4_PROFILE` (GPU time per FSR 4 pass) and `BB_FSR4_STATS` (driver
  statistics of each pass) in the FSR 4 v07 provider.

- `0002-...`: narrow stderr diagnostics, preserving host logging after FSR messages.
- `0003-...`: report the source location of FidelityFX Vulkan backend failures.
- `0004-...`: unified-memory fallback when choosing a device-local Vulkan memory type.

Both the root and `engines/metal/gpu/third_party/fsr-vulkan` checkouts must have the complete
ordered series. The MetalFX path does not dispatch FSR; these remain compatibility-provider
changes. `tools/macos/apply_metal_patches.py` checks the patch prefix before applying a suffix.
