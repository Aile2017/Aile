#pragma once

#include <vector>

struct B2eSections {
    wchar_t* load = nullptr;
    wchar_t* encode = nullptr;
    wchar_t* decode = nullptr;
    wchar_t* sfx = nullptr;
    wchar_t* decode1 = nullptr;
    wchar_t* list = nullptr;
    wchar_t* test = nullptr;
    wchar_t* del = nullptr;
    bool  sfxDirect = false;
    bool  pack1 = false;
};

bool B2e_LoadAndPreprocessScriptFile(const wchar_t* path, std::vector<wchar_t>* buffer);
void B2e_PreprocessScriptInPlace(wchar_t* script);
void B2e_SplitSectionsInPlace(wchar_t* script, B2eSections* sections);
bool B2e_IsSectionEmpty(const wchar_t* section);
