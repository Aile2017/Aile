#include "stdafx.h"
#include <strsafe.h>
#include "B2eScript.h"

bool B2e_LoadAndPreprocessScriptFile(const wchar_t* path, std::vector<wchar_t>* buffer)
{
    if (!path || !buffer) return false;

    HANDLE hf = ::CreateFileW(path, GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER size = {};
    if (!::GetFileSizeEx(hf, &size) || size.QuadPart < 0 || size.QuadPart > 0x7ffffffeLL) {
        ::CloseHandle(hf);
        return false;
    }

    DWORD sz = static_cast<DWORD>(size.QuadPart);
    std::vector<char> raw(sz + 1, '\0');

    DWORD rd = 0;
    bool ok = !!::ReadFile(hf, raw.data(), sz, &rd, nullptr);
    ::CloseHandle(hf);
    if (!ok || rd != sz) {
        buffer->clear();
        return false;
    }
    raw[rd] = '\0';

    // .b2e scripts are byte text in the system code page (ASCII in practice).
    // Decode to UTF-16 so the whole Rythp engine can run wide.
    int need = ::MultiByteToWideChar(CP_ACP, 0, raw.data(), (int)rd, nullptr, 0);
    if (need < 0) { buffer->clear(); return false; }
    buffer->assign(need + 1, L'\0');
    if (need > 0)
        ::MultiByteToWideChar(CP_ACP, 0, raw.data(), (int)rd, buffer->data(), need);
    (*buffer)[need] = L'\0';

    B2e_PreprocessScriptInPlace(buffer->data());
    return true;
}

void B2e_PreprocessScriptInPlace(wchar_t* script)
{
    if (!script) return;

    bool inString = false;
    for (wchar_t* p = script; *p; ++p) {
        // Match the Rythp tokenizer's escape rule: the char after '%' is
        // literal, so %" must not toggle the string state.
        if (*p == L'%' && p[1]) {
            ++p;
            continue;
        }

        // Rythp strings cannot span lines; confine any state drift to one line.
        if (*p == L'\n' || *p == L'\r') {
            inString = false;
            continue;
        }

        if (*p == L'"') {
            inString = !inString;
            continue;
        }

        if (!inString && *p == L';') {
            while (*p && *p != L'\n' && *p != L'\r') {
                *p = L' ';
                ++p;
            }
            if (!*p) break;
        }
    }
}

bool B2e_IsSectionEmpty(const wchar_t* section)
{
    if (!section) return true;
    while (*section == L'\t' || *section == L' ' || *section == L'\r' || *section == L'\n')
        ++section;
    return *section == L'\0';
}

static void SetSection(wchar_t** slot, wchar_t* label, size_t labelLen)
{
    *label = L'\0';
    *slot = label + labelLen;
}

static void NormalizeSection(wchar_t** slot)
{
    if (*slot && B2e_IsSectionEmpty(*slot))
        *slot = nullptr;
}

void B2e_SplitSectionsInPlace(wchar_t* script, B2eSections* sections)
{
    if (!script || !sections) return;
    *sections = {};

    for (wchar_t* line = script; *line; ) {
        wchar_t* nextLine = line;
        while (*nextLine && *nextLine != L'\n' && *nextLine != L'\r')
            ++nextLine;
        while (*nextLine == L'\n' || *nextLine == L'\r')
            ++nextLine;

        wchar_t* p = line;
        while (*p == L' ' || *p == L'\t')
            ++p;

        switch (*p) {
        case L'c': case L'd': case L'e': case L'l': case L's': case L't':
            if (ki_memcmp(p, L"load:", 5))
                SetSection(&sections->load, p, 5);
            else if (ki_memcmp(p, L"encode:", 7))
                SetSection(&sections->encode, p, 7), sections->pack1 = false;
            else if (ki_memcmp(p, L"encode1:", 8))
                SetSection(&sections->encode, p, 8), sections->pack1 = true;
            else if (ki_memcmp(p, L"decode:", 7))
                SetSection(&sections->decode, p, 7);
            else if (ki_memcmp(p, L"sfx:", 4))
                SetSection(&sections->sfx, p, 4), sections->sfxDirect = false;
            else if (ki_memcmp(p, L"sfxd:", 5))
                SetSection(&sections->sfx, p, 5), sections->sfxDirect = true;
            else if (ki_memcmp(p, L"decode1:", 8))
                SetSection(&sections->decode1, p, 8);
            else if (ki_memcmp(p, L"list:", 5))
                SetSection(&sections->list, p, 5);
            else if (ki_memcmp(p, L"test:", 5))
                SetSection(&sections->test, p, 5);
            else if (ki_memcmp(p, L"delete:", 7))
                SetSection(&sections->del, p, 7);
            break;
        }
        line = nextLine;
    }

    NormalizeSection(&sections->load);
    NormalizeSection(&sections->encode);
    NormalizeSection(&sections->decode);
    NormalizeSection(&sections->sfx);
    NormalizeSection(&sections->decode1);
    NormalizeSection(&sections->list);
    NormalizeSection(&sections->test);
    NormalizeSection(&sections->del);
}
