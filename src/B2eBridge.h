// B2eBridge.h
// UNICODE/ANSI bridge between SevenZipB2e.cpp (UNICODE UI layer) and the
// B2E engine (kilib ANSI layer compiled without UNICODE).
// This header exposes only Win32 / std types — no kilib types leak through.

#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include "ArchiveItem.h"
#include "WorkerThread.h"

// One entry from the (type ...) line of a .b2e load: section.
// The output extension is intentionally NOT modeled here: AileFlow does not predict
// the produced file name — the .b2e script's (arc.XXX) commands decide it. See the
// compress flow, which passes an extension-less base path plus the format hint.
struct B2eMethodInfo {
    std::wstring name;       // e.g. L"LZMA2", L"gzip"
    bool         isDefault;  // marked with * in the type list
};

// Format descriptor returned by B2e_GetWritableFormats().
struct B2eFormatInfo {
    std::wstring label;                  // e.g. L"7-Zip (.7z)"
    std::wstring ext;                    // e.g. L"7z"
    std::vector<B2eMethodInfo> methods;  // ordered by type-list position (index = level for B2e_Compress)
    bool         canSfx = false;         // true if the .b2e has an sfx: or sfxd: section
};

// Set the parent HWND for input/inputpw dialogs shown during B2E script execution.
// Call from the worker thread lambda before invoking B2e_Compress / B2e_Extract,
// then clear (pass NULL) after the operation completes.
void B2e_SetDialogParent(HWND hwnd);

// Dynamically scans the b2e/ directory next to the EXE.
// Returns every .b2e file that has an encode: section, with its method list parsed
// from the (type ...) line.  Used by the compression dialog and SevenZip::Load().
std::vector<B2eFormatInfo> B2e_GetWritableFormats();

// Returns one line per external tool used by B2E scripts, formatted as
// "toolname.exe   version" (name left-padded to 12 chars).  Duplicates removed.
std::vector<std::wstring> B2e_GetComponentVersions();

// Returns true if ext (no dot, case-insensitive, e.g. L"7z") is handled
// by a .b2e script (for listing or extraction).
bool B2e_IsArchiveExt(const wchar_t* ext);

// Returns "*.7z;*.zip;..." built from every extension handled by a loaded .b2e
// script. Used as the open dialog's archive filter so it tracks the scripts.
// Empty when no scripts are present.
std::wstring B2e_GetExtensionFilterPattern();

// List archive contents.
// Returns S_OK and fills items on success.
// Returns E_NOTIMPL if the extension has no .b2e handler.
// Returns E_FAIL if the archive could not be opened or listed.
// columnHeader (optional): receives the header line that appears before the
// first separator in the listing output (e.g. "   Date      Time ...  Name").
// toolName (optional): receives the tool executable name from the .b2e load: (name X)
// directive (e.g. L"7zG.exe", L"WinRAR.exe").
// canTest (optional): set to true if the .b2e has a test: section.
// canDelete (optional): set to true if the .b2e has a delete: section.
// canAdd (optional): set to true if the .b2e has an encode: section (format is writable).
HRESULT B2e_List(const wchar_t* archivePath,
                 std::vector<ArchiveItem>& items,
                 std::wstring* columnHeader = nullptr,
                 std::wstring* toolName = nullptr,
                 bool* canTest = nullptr,
                 bool* canDelete = nullptr,
                 bool* canAdd = nullptr);

// Extract entries from an archive.
// indices: positions into allItems (as returned by B2e_List). Empty = extract all.
// allItems: the vector returned by the matching B2e_List call (needed for
//           building the per-file list when doing selective extraction).
// sink: optional progress callback; may be nullptr.
HRESULT B2e_Extract(const wchar_t* archivePath,
                    const std::vector<UINT32>& indices,
                    const std::vector<ArchiveItem>& allItems,
                    const wchar_t* destDir,
                    IExtractProgressSink* sink);

// Compress srcPaths into outPath.
// The output format is inferred from the outPath extension.
// level: 0 = store (method 1 in the .b2e encode: section);
//        1 = default compression (the method marked * in load: type);
//        2+ = successive methods in the type list.
// sfx: if true and the .b2e has an sfx:/sfxd: section, creates a self-extracting archive.
// fmtExt: when non-null, overrides the outPath extension for .b2e script lookup
//         (needed when sfx=true and outPath ends in .exe instead of the format extension).
// sink: optional progress callback; may be nullptr.
HRESULT B2e_Compress(const std::vector<std::wstring>& srcPaths,
                     const wchar_t* outPath,
                     int level,
                     IExtractProgressSink* sink,
                     bool sfx = false,
                     const wchar_t* fmtExt = nullptr);

// Run the test: section for the archive and capture stdout output.
// output receives the full stdout/stderr text from the test tool.
// Returns S_OK if the tool exits with code 0, E_FAIL on non-zero exit.
HRESULT B2e_Test(const wchar_t* archivePath, std::wstring* output);

// Delete the entries at deleteIndices (positions into allItems) from the archive.
HRESULT B2e_Delete(const wchar_t* archivePath,
                   const std::vector<UINT32>& deleteIndices,
                   const std::vector<ArchiveItem>& allItems);
