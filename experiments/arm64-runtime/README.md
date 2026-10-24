# Isolated ARM64 runtime development snapshot

This is an **unstable development snapshot**, not the production runtime or a Release.
The goal is to replace the game CPU's Rosetta dependency with FEX while reusing
the existing launcher, Metal renderer, localization, saves, input, F3 and network bridges.
The production application and its data are not changed by this source export.

The snapshot contains source, authored regression tests and explicitly selected
non-secret build inputs. It intentionally contains **no game assets, dumped modules,
saves, quick saves, credentials, runtime caches, QA preferences or captured game frames**.
Vendored code retains its original licenses. SOURCE.json records upstream provenance.

## Current status

See CURRENT-PLAN.zh-CN.md and docs/. Exit-code error/restart separation and DIALOG
macro bridge coverage have headless regressions. White models and exploration stutters
remain under investigation; no performance improvement or complete compatibility is claimed.
Death/respawn, online and menu live retests are deferred for the owner's acceptance.
Existing AvPlayer implementations still need a proper guest callback/handle bridge.

## Development build limitations

This is a source checkpoint of an active local prototype, **not yet a clean-checkout
standalone build target**. The current configure/Metal candidate scripts require the
private ARM64 dependency/object stage described in their manifests and several local
paths. Do not run them against production state. Making the prototype build independent
of that stage is pending; the repository's normal production build is unchanged.

On the prepared isolated development host:

```sh
python3 tools/generate_native.py
ninja -C build/fex -j1
python3 tests/run_prototype.py --packed-veneers --fast-hle-lookup
python3 tests/test_runtime_exit_contract.py
```

All live test helpers are QA tools. Review their explicit private-root checks and
memory/lock policy before using them; never substitute a formal save or cache directory.
Do not publish a Release from this snapshot until its remaining acceptance gates pass.
