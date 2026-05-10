# Menu Implementation — Complete

All phases completed.

| Phase | Completion Date | Content |
|---|---|---|
| 1 | 2026-05-05 | Menu bar framework (File / Edit / View / Help) |
| B | 2026-05-06 | `WM_INITMENUPOPUP` foundation, tree view toggle, MRU |
| Delete | 2026-05-06 | Delete feature (7z.dll `IOutArchive::UpdateItems` / rar.exe `d`) |

Related files:

- `res/AileEx.rc` `IDR_MAIN_MENU`
- `src/resource.h` `ID_*` / `IDM_*`
- `src/MainWindow.cpp` `OnCommand` / `OnInitMenuPopup` / `OnDelete` / `RebuildMruMenu`
- `src/SevenZip.cpp` `DeleteItems` / `CDeleteCallback`
- `src/RarProcess.cpp` `Delete`
- `src/Settings.cpp` `AddMru` / `RemoveMru`
- `src/App.cpp` `RunBrowseMode` accelerator table

## Known Limitations and Future Extensions

- **Encrypted archive deletion**: Password not retained, so deleting header-encrypted 7z archives fails at `IInArchive::Open` stage. Fix: Retain password from Open in `MainWindow`, pass to `DeleteItems`.
- **Write-unsupported formats**: ISO/CAB/JAR etc. don't provide `IOutArchive`, so `QueryInterface` fails. TODO: Make error message explicit.
- **Cancel during delete**: 7z path can cancel via `CDeleteCallback::SetCompleted` returning `E_ABORT`, but RAR (rar.exe) cancel unimplemented (current `RarProcess::Delete` has no cancel path).
