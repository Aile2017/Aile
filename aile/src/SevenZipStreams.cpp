// Free helpers backing the COM stream wrappers in SevenZipStreams.h.
// Kept out-of-line here so the definitions are emitted once and shared by both
// the inline stream classes (CMultiVolOutStream) and SevenZip.cpp's operations.
#include "SevenZipStreams.h"
#include <wctype.h>     // iswdigit / towlower (ParseVolumeSize)

// ============================================================
// ConcatFiles — write prefix followed by body sequentially into dst.
// Used to concatenate an SFX module + .7z data.
// ============================================================
HRESULT ConcatFiles(const wchar_t* prefix,
                    const wchar_t* body,
                    const wchar_t* dst) {
    HANDLE hOut = CreateFileW(dst, GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hOut == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());

    auto pump = [&](const wchar_t* src) -> HRESULT {
        HANDLE hIn = CreateFileW(src, GENERIC_READ, FILE_SHARE_READ, nullptr,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hIn == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());
        BYTE  buf[64 * 1024];
        DWORD r = 0;
        HRESULT hr = S_OK;
        while (ReadFile(hIn, buf, sizeof(buf), &r, nullptr) && r > 0) {
            DWORD w = 0;
            if (!WriteFile(hOut, buf, r, &w, nullptr) || w != r) {
                hr = HRESULT_FROM_WIN32(GetLastError());
                break;
            }
        }
        CloseHandle(hIn);
        return hr;
    };

    HRESULT hr = pump(prefix);
    if (SUCCEEDED(hr)) hr = pump(body);
    CloseHandle(hOut);
    if (FAILED(hr)) DeleteFileW(dst);
    return hr;
}

// Convert a string ("100m", "1g", etc.) to byte count. Returns 0 for empty or invalid values.
UInt64 ParseVolumeSize(const std::wstring& s) {
    if (s.empty()) return 0;
    UInt64 num = 0;
    size_t i = 0;
    while (i < s.size() && iswdigit(s[i])) {
        num = num * 10 + (s[i] - L'0');
        ++i;
    }
    if (num == 0) return 0;
    UInt64 mult = 1;
    if (i < s.size()) {
        wchar_t u = (wchar_t)towlower(s[i]);
        switch (u) {
            case L'b': mult = 1; break;
            case L'k': mult = 1024ULL; break;
            case L'm': mult = 1024ULL * 1024; break;
            case L'g': mult = 1024ULL * 1024 * 1024; break;
            default: return 0;
        }
    }
    return num * mult;
}
