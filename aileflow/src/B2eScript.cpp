#include "stdafx.h"
#include <strsafe.h>
#include "B2eScript.h"

bool B2e_LoadAndPreprocessScriptFile(const char* path, std::vector<char>* buffer)
{
    if (!path || !buffer) return false;

    HANDLE hf = ::CreateFileA(path, GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER size = {};
    if (!::GetFileSizeEx(hf, &size) || size.QuadPart < 0 || size.QuadPart > 0x7ffffffeLL) {
        ::CloseHandle(hf);
        return false;
    }

    DWORD sz = static_cast<DWORD>(size.QuadPart);
    buffer->assign(sz + 1, '\0');

    DWORD rd = 0;
    bool ok = !!::ReadFile(hf, buffer->data(), sz, &rd, nullptr);
    ::CloseHandle(hf);
    if (!ok || rd != sz) {
        buffer->clear();
        return false;
    }

    (*buffer)[rd] = '\0';
    B2e_PreprocessScriptInPlace(buffer->data());
    return true;
}

void B2e_PreprocessScriptInPlace(char* script)
{
    if (!script) return;

    bool inString = false;
    for (char* p = script; *p; ++p) {
        // Match the Rythp tokenizer's escape rule: the char after '%' is
        // literal, so %" must not toggle the string state.
        if (*p == '%' && p[1]) {
            ++p;
            continue;
        }

        // Rythp strings cannot span lines; confine any state drift to one line.
        if (*p == '\n' || *p == '\r') {
            inString = false;
            continue;
        }

        if (*p == '"') {
            inString = !inString;
            continue;
        }

        if (!inString && *p == ';') {
            while (*p && *p != '\n' && *p != '\r') {
                *p = ' ';
                ++p;
            }
            if (!*p) break;
        }
    }
}

bool B2e_IsSectionEmpty(const char* section)
{
    if (!section) return true;
    while (*section == '\t' || *section == ' ' || *section == '\r' || *section == '\n')
        ++section;
    return *section == '\0';
}

static void SetSection(char** slot, char* label, size_t labelLen)
{
    *label = '\0';
    *slot = label + labelLen;
}

static void NormalizeSection(char** slot)
{
    if (*slot && B2e_IsSectionEmpty(*slot))
        *slot = nullptr;
}

void B2e_SplitSectionsInPlace(char* script, B2eSections* sections)
{
    if (!script || !sections) return;
    *sections = {};

    for (char* line = script; *line; ) {
        char* nextLine = line;
        while (*nextLine && *nextLine != '\n' && *nextLine != '\r')
            ++nextLine;
        while (*nextLine == '\n' || *nextLine == '\r')
            ++nextLine;

        char* p = line;
        while (*p == ' ' || *p == '\t')
            ++p;

        switch (*p) {
        case 'c': case 'd': case 'e': case 'l': case 's': case 't':
            if (ki_memcmp(p, "load:", 5))
                SetSection(&sections->load, p, 5);
            else if (ki_memcmp(p, "encode:", 7))
                SetSection(&sections->encode, p, 7), sections->pack1 = false;
            else if (ki_memcmp(p, "encode1:", 8))
                SetSection(&sections->encode, p, 8), sections->pack1 = true;
            else if (ki_memcmp(p, "decode:", 7))
                SetSection(&sections->decode, p, 7);
            else if (ki_memcmp(p, "sfx:", 4))
                SetSection(&sections->sfx, p, 4), sections->sfxDirect = false;
            else if (ki_memcmp(p, "sfxd:", 5))
                SetSection(&sections->sfx, p, 5), sections->sfxDirect = true;
            else if (ki_memcmp(p, "decode1:", 8))
                SetSection(&sections->decode1, p, 8);
            else if (ki_memcmp(p, "list:", 5))
                SetSection(&sections->list, p, 5);
            else if (ki_memcmp(p, "test:", 5))
                SetSection(&sections->test, p, 5);
            else if (ki_memcmp(p, "delete:", 7))
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
