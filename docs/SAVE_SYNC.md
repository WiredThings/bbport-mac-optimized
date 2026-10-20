# Save retention and iCloud Drive

## Local history

The exclusive `.quicksave-session.lock` protects cleanup and synchronization, just as it protects mode transitions and manual restores. No new runtime helper, renderer hook, timer or in-game scanner is added. Cleanup runs after successful launcher save operations, on startup and after the game process exits. F2/F3 create their existing verified snapshots; retention waits until play finishes.

Per game title, retain the active checkpoint, 3 additional unpinned checkpoints and 3 unpinned `before-load-*` copies. Retain the 5 latest unpinned automatic archives across mode changes and automatic pre-restore backups. Manual backups, imports, migration backups, iCloud conflict backups and unknown labels are not eligible. A `.keep` marker protects an individual checkpoint or archive. Archiving a pinned checkpoint pins the containing archive; explicitly unpinning that archive makes it eligible again.

Before deletion, verify complete current normal saves, active pointers, checkpoint manifests and automatic archive inventories. A pending `restore.json`, invalid active pointer or damaged candidate stops cleanup. Unknown or unpublished staging directories are not deleted. An archived interrupted restore is also preserved. Save browsing and Refresh remain read-only.

Equivalent automatic archives are reused after verification and have their date refreshed. The key includes normal file hashes, the selected checkpoint's contents and the set of historical checkpoint/pre-restore contents. Incidental checkpoint UUIDs, creation timestamps and duplicate copies of the same historical contents do not cause another archive. Explicit keep markers and unknown metadata remain significant. Manual backups are never deduplicated implicitly.

## Optional file-based sync

Choose a directory **inside iCloud Drive** in Save Manager. The launcher owns only the new `Blood Echoes Saves` child, or an existing directory carrying its exact `format.txt` marker. It refuses local-save/backup destinations and unmarked existing sync roots. Disable sync to stop launcher access; disabling does not delete local or cloud progress. Existing settings retain their values and sync defaults off.

The format is:

```text
Blood Echoes Saves/
  format.txt
  <device UUID>.bbechoessave/
    manifest.json
    savedata/                  # entire latest normal-save tree
```

Each publishing installation replaces only its own latest package. Devices that merely download matching progress need not publish a duplicate. Quick checkpoints, archive history, mods, game files, preferences, caches and credentials are never included. A device label, timestamp, file hash inventory and causal version vector are the only additional manifest data. Local sync identity/baseline lives beside that save root's backup records, not in the game saves or shared QA state.

`NSFileCoordinator` coordinates reads and atomic directory replacement. The whole normal-save tree is staged, streamed through SHA-256 with 1 MiB read chunks and verified before publication or import. Download placeholders, unresolved native iCloud file versions, symlinks, damaged/incomplete inventories and unfinished save writes pause synchronization. They never trigger a partial merge of character files. Different normal-save formats/regions are not converted.

The launcher checks on opening, before launching the game and after the game exits. If quitting the launcher also terminates play, the next launch catches up. No launcher cloud work runs during gameplay; macOS may independently continue queued transfers. Disconnected/unavailable cloud storage or unresolved progress conflicts do not block local play. Successful write handoff and macOS upload confirmation are distinct; cross-device delivery cannot be inferred from a local write alone.

## Concurrent progress

Packages carry vector clocks. A causal descendant may be imported automatically only when this Mac has no independent newer progress. Equal contents do not create acknowledgement-only publications, preventing repeated uploads between idle clients. Modification time is for display, never conflict resolution.

Different progress on first connection or concurrent offline edits require an explicit selection. Neither payload is overwritten while the decision is pending. Selecting a stale version is rejected if its content has changed. Choosing local progress first preserves losing remote versions as indefinite local conflict backups, then publishes a new causal head without directly changing other devices' packages. Choosing remote progress creates an indefinite local conflict backup of displaced local progress first. Automatic nonconflicting imports also back up local normal saves/checkpoints, using the bounded automatic policy. All imports restore whole normal saves, rebuild checkpoints and reuse offline-restore protection; online must be explicitly enabled again.

When another Mac accepts the chosen descendant, it first backs up its own displaced progress. There is no remote-delete propagation and no cloud history for every F2/F3 action. Keep valuable manual backups or export them independently.

## Validation boundary

Synthetic filesystem tests simulate two installations, first connection, independent offline edits, both conflict choices, causal imports, repeated idle sync, latest-only replacement, full metadata preservation, corrupted/symlinked packages, stale selections and unsafe destinations. Real iCloud Drive integration uses only a new uniquely named QA directory and tiny synthetic saves, confirms macOS upload flags with uncached resource reads, verifies readback and removes that QA directory. No actual game save is uploaded by QA.

Two physical Macs accepting the same save and real iCloud conflict/download recovery are not claimed tested by the synthetic two-client cases. Use one machine at a time until the other Mac has confirmed the intended progress. The standard Swift suite skips the optional live integration case unless explicitly enabled:

```bash
swift test --package-path launcher-macos --jobs 2
BLOOD_ECHOES_ICLOUD_QA="$HOME/Library/Mobile Documents/com~apple~CloudDocs" \
  swift test --package-path launcher-macos --jobs 2 \
  --filter CloudSaveTests/testSelectedICloudDriveWithSyntheticPayloadOnly
```

Apple API references: [NSFileCoordinator](https://developer.apple.com/documentation/foundation/nsfilecoordinator), [iCloud URL resource keys](https://developer.apple.com/documentation/foundation/urlresourcekey), [document design and conflicts](https://developer.apple.com/library/archive/documentation/General/Conceptual/iCloudDesignGuide/Chapters/DesigningForDocumentsIniCloud.html). This build uses a user-selected Drive folder; it does not claim a provisioned private iCloud container. Apple's [container lookup API](https://developer.apple.com/documentation/foundation/filemanager/url(forubiquitycontaineridentifier:)) requires the corresponding container entitlements.
