#pragma once

#include <vector>

struct B2eSections {
    char* load = nullptr;
    char* encode = nullptr;
    char* decode = nullptr;
    char* sfx = nullptr;
    char* decode1 = nullptr;
    char* list = nullptr;
    char* test = nullptr;
    char* del = nullptr;
    bool  sfxDirect = false;
    bool  pack1 = false;
};

bool B2e_LoadAndPreprocessScriptFile(const char* path, std::vector<char>* buffer);
void B2e_PreprocessScriptInPlace(char* script);
void B2e_SplitSectionsInPlace(char* script, B2eSections* sections);
bool B2e_IsSectionEmpty(const char* section);
