#include "I18n.h"

#include <stdarg.h>
#include <stdio.h>

namespace I18n {

static const wchar_t* g_tag = L"en-US";

void Init() {
    // GetUserDefaultUILanguage returns the OS display language.
    // Only the language family is checked via PRIMARYLANGID (SUBLANG region is ignored).
    // Overridable with the AILEEX_LANG=en|ja environment variable (for testing).
    LANGID lang = GetUserDefaultUILanguage();
    const wchar_t* preferred = nullptr;

    wchar_t override_buf[8] = {};
    DWORD ovlen = GetEnvironmentVariableW(L"AILEEX_LANG", override_buf, _countof(override_buf));
    bool forceEn = (ovlen > 0 && _wcsicmp(override_buf, L"en") == 0);
    bool forceJa = (ovlen > 0 && _wcsicmp(override_buf, L"ja") == 0);

    if (forceJa || (!forceEn && PRIMARYLANGID(lang) == LANG_JAPANESE)) {
        preferred = L"ja-JP\0";
        g_tag = L"ja-JP";
    } else {
        preferred = L"en-US\0";
        g_tag = L"en-US";
    }

    // A double-null-terminated list is required: the literal \0 at the end of the string
    // plus the C-string terminator \0 together form the required double NUL.
    ULONG n = 0;
    SetProcessPreferredUILanguages(MUI_LANGUAGE_NAME, preferred, &n);
}

const wchar_t* CurrentTag() {
    return g_tag;
}

std::wstring Tr(UINT id) {
    // Conservatively large buffer; fits even long resource strings such as description text.
    wchar_t buf[1024];
    int len = LoadStringW(GetModuleHandleW(nullptr), id, buf, _countof(buf));
    if (len <= 0) return std::wstring();
    return std::wstring(buf, (size_t)len);
}

std::wstring TrFmt(UINT id, ...) {
    std::wstring fmt = Tr(id);
    if (fmt.empty()) return std::wstring();

    wchar_t buf[2048];
    va_list ap;
    va_start(ap, id);
    int n = _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt.c_str(), ap);
    va_end(ap);
    if (n <= 0) return std::wstring();
    return std::wstring(buf, (size_t)n);
}

std::wstring TrFilter(UINT id) {
    std::wstring s = Tr(id);
    for (auto& c : s) {
        if (c == L'|') c = L'\0';
    }
    // OFN requires "label\0pattern\0...\0\0" but STRINGTABLE cannot store embedded NULs,
    // so resources use '|' as the separator and we replace it here.
    // The trailing \0\0 is formed by the wstring's auto NUL plus the '|'-turned-'\0' at the end.
    return s;
}

} // namespace I18n
