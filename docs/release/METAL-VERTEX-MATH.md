# Strict vertex math inlining (0.2.14)

The three hot plain vertex programs b10c4f55, 3deaa573 and adf9e624 have direct buffer
loads in their final cached MSL bodies. The declared page-table binding is optimized out
of those functions: there are no BDA lookups or physical-address helpers in their bodies.
This observation does not exclude page-table costs in other shaders.

Their bodies contain roughly 190-320 calls to SPIRV-Cross's optnone scalar add/subtract/
multiply helpers, along with bone-palette and transform-buffer loads. A bounded offscreen
probe of the same shader bodies, with synthetic inputs/buffers and unchanged transformations,
found no output differences and 24-39% lower compute-dispatch time after helper inlining.
This is not an in-game vertex percentage. Varying attributes/index distribution changed
both data access and arithmetic inputs; it does not isolate memory bandwidth by itself.

Adapter patch 0015 rewrites only the three exact scalar helper definitions in plain vertex
shaders. Every original explicit fma operand and rounding boundary is retained; helpers
become always_inline with function-scoped Safe math and contract(off). Matrix and other
optnone helpers are unchanged. This does not enable fast/relaxed vertex math, reassociate
the transform, use half precision, reduce geometry or change page-table addressing.
The pragma semantics are documented by [Apple's Metal language specification, section 1.6.3](https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf).
The upstream helper implementation is [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross/blob/f11ba9f0b21ba8fc15153d50a2a1ae31ab1cf8f7/spirv_msl.cpp).

Both cache hits and misses apply the rewrite at native-library compilation. Original MSL
cache files, metadata and capacities remain unchanged; disabling the optimization needs
no cache purge. An inline compile rejection retries the original source. Fragment, compute,
tessellation/geometry variants and binding layouts keep their existing rules.

Default on; `BB_MTL_VERTEX_MATH=0` restores original helper calls. Optional diagnostic
`BB_MTL_VERTEX_MATH_FILE=/private/tmp/bbport-vertex-math-ab.control` selects 0=reference,
1=inline for paired main color PSOs of the three hashes. Reference native libraries and
parent PSOs each have a lifetime creation limit of 32; cap/failure messages invalidate
claims of complete pairing. The file selector uses atomic state and polls at most once
a second. Normal launches create no reference libraries/PSOs and poll no file.
Other vertex passes stay optimized during this targeted same-process A/B; it measures
the selected main passes, not a complete 0.2.13-vs-0.2.14 comparison.

Validation covers more than a million scalar boundary/random results, vector chains,
signed zero/NaN/infinity and a known FMA contraction counterexample; all match the original
helpers bit-for-bit. Two-pass raster tests first write depth with color writes disabled,
then compare depth Equal using original/inline transforms; resulting pixels must match.
Ten local hot SPIR-V variants also verify first compilation, cache hits and reference
binding-map equality. Game shader sources/data are not bundled in the release.

No resolution, effect, cache budget, proxy-clear policy, velocity path, blur bypass,
visibility history, resource lifetime or two-level BDA mapping changes. Gameplay benefit
still requires a same-scene measurement with mode acknowledgements and actual paired PSOs.
