// Free helpers backing the COM callbacks in SevenZipCallbacks.h.
// Out-of-line so the definitions are emitted once and shared by the inline
// callback classes (CExtractCallback) and SevenZip.cpp's operations
// (Compress / AddToArchive feed CUpdateCallback / CAddCallback).
#include "SevenZipCallbacks.h"

// Canonicalize a path (resolve "." / ".." / redundant separators) via GetFullPathNameW.
// Returns empty string on failure.
std::wstring CanonicalizePath(const std::wstring& p) {
    DWORD need = GetFullPathNameW(p.c_str(), 0, nullptr, nullptr);
    if (need == 0) return L"";
    std::wstring out(need, L'\0');
    DWORD got = GetFullPathNameW(p.c_str(), need, out.data(), nullptr);
    if (got == 0 || got >= need) return L"";
    out.resize(got);
    return out;
}

void EnumeratePaths(const std::vector<std::wstring>& srcPaths,
                    std::vector<SrcEntry>& entries) {
    for (const auto& src : srcPaths) {
        DWORD attrs = GetFileAttributesW(src.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) continue;

        if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
            // Compute base name (top-level archive folder name)
            std::wstring dir = src;
            while (!dir.empty() && (dir.back() == L'\\' || dir.back() == L'/'))
                dir.pop_back();
            auto slash = dir.rfind(L'\\');
            if (slash == std::wstring::npos) slash = dir.rfind(L'/');
            std::wstring baseName = (slash != std::wstring::npos) ? dir.substr(slash + 1) : dir;

            // Add the directory entry itself
            WIN32_FILE_ATTRIBUTE_DATA fad{};
            GetFileAttributesExW(src.c_str(), GetFileExInfoStandard, &fad);
            entries.push_back({ src, baseName, true, 0, fad.ftLastWriteTime });

            // BFS/DFS over children
            struct Job { std::wstring diskDir, archDir; };
            std::vector<Job> stack{ { src, baseName } };
            while (!stack.empty()) {
                auto job = stack.back(); stack.pop_back();
                std::wstring pat = job.diskDir;
                if (pat.back() != L'\\') pat += L'\\';
                pat += L'*';

                WIN32_FIND_DATAW fd;
                HANDLE hFind = FindFirstFileW(pat.c_str(), &fd);
                if (hFind == INVALID_HANDLE_VALUE) continue;
                do {
                    if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
                        continue;
                    std::wstring childDisk = job.diskDir + L'\\' + fd.cFileName;
                    std::wstring childArch = job.archDir  + L'\\' + fd.cFileName;
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                        entries.push_back({ childDisk, childArch, true, 0, fd.ftLastWriteTime });
                        stack.push_back({ childDisk, childArch });
                    } else {
                        UINT64 sz = ((UINT64)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
                        entries.push_back({ childDisk, childArch, false, sz, fd.ftLastWriteTime });
                    }
                } while (FindNextFileW(hFind, &fd));
                FindClose(hFind);
            }
        } else {
            // Single file: archive path = filename only
            auto slash = src.rfind(L'\\');
            if (slash == std::wstring::npos) slash = src.rfind(L'/');
            std::wstring name = (slash != std::wstring::npos) ? src.substr(slash + 1) : src;
            WIN32_FILE_ATTRIBUTE_DATA fad{};
            GetFileAttributesExW(src.c_str(), GetFileExInfoStandard, &fad);
            UINT64 sz = ((UINT64)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
            entries.push_back({ src, name, false, sz, fad.ftLastWriteTime });
        }
    }
}
