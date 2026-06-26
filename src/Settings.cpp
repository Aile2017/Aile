#include "Settings.h"
#include <shlwapi.h>
#include <algorithm>

void Settings::BuildIniPath() const {
    GetModuleFileNameW(nullptr, m_iniPath, MAX_PATH);
    PathRenameExtensionW(m_iniPath, L".ini");
}

void Settings::Load() {
    BuildIniPath();
    m_defaultOutputDir = ReadStr(L"General", L"DefaultOutputDir", L"");
    {
        std::wstring mode = ReadStr(L"General", L"OutputDirMode", L"source");
        m_outputDirModeFixed = (mode == L"fixed");
    }
    m_defaultFormat    = ReadStr(L"General", L"DefaultFormat",    L"7z");
    m_7zDllPath        = ReadStr(L"General", L"7zDllPath",        L"");
    m_fontName         = ReadStr(L"General", L"FontName",         L"Segoe UI");

    wchar_t buf[16] = {};
    GetPrivateProfileStringW(L"General", L"OpenFolderAfterExtract", L"0", buf, 16, m_iniPath);
    m_openFolderAfterExtract = _wtoi(buf) != 0;
    m_openFolderCommand = ReadStr(L"General", L"OpenFolderCommand", L"");
    GetPrivateProfileStringW(L"General", L"CompressionLevel", L"5", buf, 16, m_iniPath);
    m_compressionLevel = _wtoi(buf);
    if (m_compressionLevel < 0 || m_compressionLevel > 9) m_compressionLevel = 5;

    GetPrivateProfileStringW(L"General", L"MkDir", L"2", buf, 16, m_iniPath);
    m_mkDir = _wtoi(buf);
    if (m_mkDir < 0 || m_mkDir > 3) m_mkDir = 2;

    // Extraction output-folder naming / structure
    GetPrivateProfileStringW(L"General", L"ExtStripMode", L"0", buf, 16, m_iniPath);
    m_extStripMode = _wtoi(buf);
    if (m_extStripMode < 0 || m_extStripMode > 2) m_extStripMode = 0;

    GetPrivateProfileStringW(L"General", L"StripTrailingNumber", L"0", buf, 16, m_iniPath);
    m_stripTrailingNumber = _wtoi(buf) != 0;

    GetPrivateProfileStringW(L"General", L"BreakDDir", L"0", buf, 16, m_iniPath);
    m_breakDDir = _wtoi(buf) != 0;

    // Advanced compress options
    m_advDictSize   = ReadStr(L"AdvancedCompress", L"DictSize",   L"");
    m_advWordSize   = ReadStr(L"AdvancedCompress", L"WordSize",   L"");
    m_advSolidBlock = ReadStr(L"AdvancedCompress", L"SolidBlock", L"");
    m_advThreads    = ReadStr(L"AdvancedCompress", L"Threads",    L"");
    m_advExtra      = ReadStr(L"AdvancedCompress", L"Extra",      L"");
    m_advVolume     = ReadStr(L"AdvancedCompress", L"Volume",     L"");

    // Window placement
    GetPrivateProfileStringW(L"Window", L"X",         L"-1",    buf, 16, m_iniPath); m_windowX        = _wtoi(buf);
    GetPrivateProfileStringW(L"Window", L"Y",         L"-1",    buf, 16, m_iniPath); m_windowY        = _wtoi(buf);
    GetPrivateProfileStringW(L"Window", L"W",         L"900",   buf, 16, m_iniPath); m_windowW        = _wtoi(buf);
    GetPrivateProfileStringW(L"Window", L"H",         L"600",   buf, 16, m_iniPath); m_windowH        = _wtoi(buf);
    GetPrivateProfileStringW(L"Window", L"Maximized", L"0",     buf, 16, m_iniPath); m_windowMaximized = _wtoi(buf) != 0;
    GetPrivateProfileStringW(L"Window", L"Splitter",  L"220",   buf, 16, m_iniPath); m_splitterPos    = _wtoi(buf);
    GetPrivateProfileStringW(L"Window", L"TreeVisible", L"1",   buf, 16, m_iniPath); m_treeVisible    = _wtoi(buf) != 0;
    GetPrivateProfileStringW(L"Window", L"ToolbarVisible", L"1", buf, 16, m_iniPath); m_toolbarVisible = _wtoi(buf) != 0;
    GetPrivateProfileStringW(L"Window", L"IconsVisible", L"1",  buf, 16, m_iniPath); m_iconsVisible   = _wtoi(buf) != 0;
    GetPrivateProfileStringW(L"Window", L"MenubarVisible", L"1", buf, 16, m_iniPath); m_menubarVisible = _wtoi(buf) != 0;
    if (m_windowW < 400) m_windowW = 400;
    if (m_windowH < 300) m_windowH = 300;
    if (m_splitterPos < 80) m_splitterPos = 80;

    // MRU — Path0 is the most recent. Stop as soon as an empty entry is encountered.
    m_mruPaths.clear();
    for (size_t i = 0; i < kMaxMru; ++i) {
        wchar_t key[16];
        swprintf_s(key, L"Path%zu", i);
        std::wstring v = ReadStr(L"Mru", key, L"");
        if (v.empty()) break;
        m_mruPaths.push_back(std::move(v));
    }
}

void Settings::Save() const {
    // Guard against writing to an empty path if Save() is called before Load()
    if (!m_iniPath[0]) BuildIniPath();
    WriteStr(L"General", L"DefaultOutputDir", m_defaultOutputDir.c_str());
    WriteStr(L"General", L"OutputDirMode",    m_outputDirModeFixed ? L"fixed" : L"source");
    WriteStr(L"General", L"DefaultFormat",    m_defaultFormat.c_str());
    WriteStr(L"General", L"7zDllPath",        m_7zDllPath.c_str());
    WriteStr(L"General", L"FontName",         m_fontName.c_str());
    WriteStr(L"General", L"OpenFolderAfterExtract", m_openFolderAfterExtract ? L"1" : L"0");
    WriteStr(L"General", L"OpenFolderCommand",      m_openFolderCommand.c_str());

    wchar_t buf[16] = {};
    _itow_s(m_compressionLevel, buf, 10);
    WriteStr(L"General", L"CompressionLevel", buf);

    _itow_s(m_mkDir, buf, 10);
    WriteStr(L"General", L"MkDir", buf);

    // Extraction output-folder naming / structure
    _itow_s(m_extStripMode, buf, 10);
    WriteStr(L"General", L"ExtStripMode", buf);
    WriteStr(L"General", L"StripTrailingNumber", m_stripTrailingNumber ? L"1" : L"0");
    WriteStr(L"General", L"BreakDDir",           m_breakDDir           ? L"1" : L"0");

    // Advanced compress options
    WriteStr(L"AdvancedCompress", L"DictSize",   m_advDictSize.c_str());
    WriteStr(L"AdvancedCompress", L"WordSize",   m_advWordSize.c_str());
    WriteStr(L"AdvancedCompress", L"SolidBlock", m_advSolidBlock.c_str());
    WriteStr(L"AdvancedCompress", L"Threads",    m_advThreads.c_str());
    WriteStr(L"AdvancedCompress", L"Extra",      m_advExtra.c_str());
    WriteStr(L"AdvancedCompress", L"Volume",     m_advVolume.c_str());

    // Window placement
    _itow_s(m_windowX,  buf, 10); WriteStr(L"Window", L"X",         buf);
    _itow_s(m_windowY,  buf, 10); WriteStr(L"Window", L"Y",         buf);
    _itow_s(m_windowW,  buf, 10); WriteStr(L"Window", L"W",         buf);
    _itow_s(m_windowH,  buf, 10); WriteStr(L"Window", L"H",         buf);
    WriteStr(L"Window", L"Maximized", m_windowMaximized ? L"1" : L"0");
    _itow_s(m_splitterPos, buf, 10); WriteStr(L"Window", L"Splitter", buf);
    WriteStr(L"Window", L"TreeVisible", m_treeVisible ? L"1" : L"0");
    WriteStr(L"Window", L"ToolbarVisible", m_toolbarVisible ? L"1" : L"0");
    WriteStr(L"Window", L"IconsVisible", m_iconsVisible ? L"1" : L"0");
    WriteStr(L"Window", L"MenubarVisible", m_menubarVisible ? L"1" : L"0");

    // MRU — pass nullptr to WritePrivateProfileStringW to delete obsolete keys from the ini.
    for (size_t i = 0; i < kMaxMru; ++i) {
        wchar_t key[16];
        swprintf_s(key, L"Path%zu", i);
        const wchar_t* val = (i < m_mruPaths.size()) ? m_mruPaths[i].c_str() : nullptr;
        WritePrivateProfileStringW(L"Mru", key, val, m_iniPath);
    }
}

void Settings::AddMru(const std::wstring& path) {
    auto eq = [&](const std::wstring& s) {
        return _wcsicmp(s.c_str(), path.c_str()) == 0;
    };
    auto it = std::find_if(m_mruPaths.begin(), m_mruPaths.end(), eq);
    if (it != m_mruPaths.end()) m_mruPaths.erase(it);
    m_mruPaths.insert(m_mruPaths.begin(), path);
    if (m_mruPaths.size() > kMaxMru) m_mruPaths.resize(kMaxMru);
}

void Settings::RemoveMru(const std::wstring& path) {
    auto eq = [&](const std::wstring& s) {
        return _wcsicmp(s.c_str(), path.c_str()) == 0;
    };
    auto it = std::find_if(m_mruPaths.begin(), m_mruPaths.end(), eq);
    if (it != m_mruPaths.end()) m_mruPaths.erase(it);
}

std::wstring Settings::ReadStr(const wchar_t* section, const wchar_t* key, const wchar_t* def) const {
    wchar_t buf[MAX_PATH * 2] = {};
    GetPrivateProfileStringW(section, key, def, buf, MAX_PATH * 2, m_iniPath);
    return buf;
}

void Settings::WriteStr(const wchar_t* section, const wchar_t* key, const wchar_t* val) const {
    WritePrivateProfileStringW(section, key, val, m_iniPath);
}

// When auto-detecting the output path, we want to strip the original extension.
// However, if we're wrapping a single file into a stream format (like .gz),
// it's conventional to append the extension rather than replace it.
std::wstring Settings::ComputeDefaultOutputPath(const Settings& s,
                                               const std::vector<std::wstring>& srcFiles,
                                               const std::wstring& overrideDir) {
    // Priority: overrideDir > fixed mode > source file directory
    std::wstring dir;
    if (!overrideDir.empty()) {
        dir = overrideDir;
    } else if (s.GetOutputDirModeFixed()) {
        dir = s.GetDefaultOutputDir();
    } else if (!srcFiles.empty()) {
        auto sl = srcFiles[0].find_last_of(L"\\/");
        dir = (sl != std::wstring::npos) ? srcFiles[0].substr(0, sl) : L"";
    } else {
        dir = s.GetDefaultOutputDir();
    }

    // Extract stem of first input file
    if (srcFiles.empty()) return dir;

    auto sl = srcFiles[0].find_last_of(L"\\/");
    std::wstring name = (sl != std::wstring::npos) ? srcFiles[0].substr(sl + 1) : srcFiles[0];
    
    DWORD attrs = GetFileAttributesW(srcFiles[0].c_str());
    bool isDir = (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY));

    std::wstring stem;
    if (srcFiles.size() == 1 && !isDir) {
        // Single file: keep full name so we get file.txt.gz
        stem = name;
    } else {
        // Directory or multiple files (we use the first item's name as a base)
        auto dot = name.rfind(L'.');
        stem = (dot != std::wstring::npos) ? name.substr(0, dot) : name;
    }

    return dir.empty() ? stem : dir + L"\\" + stem;
}
