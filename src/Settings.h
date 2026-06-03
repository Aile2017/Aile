#pragma once
#include <windows.h>
#include <string>
#include <vector>

class Settings {
public:
    static constexpr size_t kMaxMru = 10;

    void Load();
    void Save() const;

    const std::wstring& GetDefaultOutputDir() const { return m_defaultOutputDir; }
    void SetDefaultOutputDir(const wchar_t* v)      { m_defaultOutputDir = v; }

    // Output dir mode: true = use fixed DefaultOutputDir, false = use source file's directory
    bool GetOutputDirModeFixed() const              { return m_outputDirModeFixed; }
    void SetOutputDirModeFixed(bool v)              { m_outputDirModeFixed = v; }

    const std::wstring& GetDefaultFormat() const    { return m_defaultFormat; }
    void SetDefaultFormat(const wchar_t* v)         { m_defaultFormat = v; }

    int  GetCompressionLevel() const                { return m_compressionLevel; }
    void SetCompressionLevel(int v)                 { m_compressionLevel = v; }

    // Subfolder creation policy on extract:
    // 0=never / 1=single file only / 2=multiple entries (default) / 3=always
    int  GetMkDir() const                           { return m_mkDir; }
    void SetMkDir(int v)                            { m_mkDir = v; }

    // Window placement
    int  GetWindowX() const          { return m_windowX; }
    int  GetWindowY() const          { return m_windowY; }
    int  GetWindowW() const          { return m_windowW; }
    int  GetWindowH() const          { return m_windowH; }
    bool GetWindowMaximized() const  { return m_windowMaximized; }
    int  GetSplitterPos() const      { return m_splitterPos; }
    bool GetTreeVisible() const      { return m_treeVisible; }
    bool GetToolbarVisible() const   { return m_toolbarVisible; }
    bool GetIconsVisible() const     { return m_iconsVisible; }
    bool GetMenubarVisible() const   { return m_menubarVisible; }
    void SetWindowPlacement(int x, int y, int w, int h, bool maximized) {
        m_windowX = x; m_windowY = y; m_windowW = w; m_windowH = h;
        m_windowMaximized = maximized;
    }
    void SetSplitterPos(int v)       { m_splitterPos = v; }
    void SetTreeVisible(bool v)      { m_treeVisible = v; }
    void SetToolbarVisible(bool v)   { m_toolbarVisible = v; }
    void SetIconsVisible(bool v)     { m_iconsVisible = v; }
    void SetMenubarVisible(bool v)   { m_menubarVisible = v; }

    const std::wstring& GetFontName() const         { return m_fontName; }
    void SetFontName(const wchar_t* v)              { m_fontName = v; }

    // Phase 1+2: Extraction behavior
    // ExtStripMode: 0=strip all known exts (default), 1=strip one ext, 2=keep all
    int  GetExtStripMode() const                    { return m_extStripMode; }
    void SetExtStripMode(int v)                     { m_extStripMode = v; }

    bool GetStripTrailingNumber() const             { return m_stripTrailingNumber; }
    void SetStripTrailingNumber(bool v)             { m_stripTrailingNumber = v; }

    // break_ddir: collapse single-subfolder output after extraction
    bool GetBreakDDir() const                       { return m_breakDDir; }
    void SetBreakDDir(bool v)                       { m_breakDDir = v; }

    // Phase 1: General behavior
    bool GetOpenFolderAfterExtract() const          { return m_openFolderAfterExtract; }
    void SetOpenFolderAfterExtract(bool v)          { m_openFolderAfterExtract = v; }

    // OpenFolderCommand: INI-only. Empty = use Explorer (ShellExecuteW).
    const std::wstring& GetOpenFolderCommand() const { return m_openFolderCommand; }
    void SetOpenFolderCommand(const wchar_t* v)     { m_openFolderCommand = v; }

    // ConcurrentLimit: INI-only. 0 = no limit. Default 4.
    int  GetConcurrentLimit() const                 { return m_concurrentLimit; }

    // MRU (recently used archives) — head is most recent; duplicates removed case-insensitively.
    const std::vector<std::wstring>& GetMruPaths() const { return m_mruPaths; }
    void AddMru(const std::wstring& path);
    void RemoveMru(const std::wstring& path);

private:
    mutable std::wstring m_iniPath;

    // All members are initialized by Load() before use
    std::wstring m_defaultOutputDir;
    bool         m_outputDirModeFixed;
    std::wstring m_defaultFormat;
    int          m_compressionLevel;
    int          m_mkDir;
    int          m_windowX;
    int          m_windowY;
    int          m_windowW;
    int          m_windowH;
    bool         m_windowMaximized;
    int          m_splitterPos;
    bool         m_treeVisible;
    bool         m_toolbarVisible;
    bool         m_iconsVisible;
    bool         m_menubarVisible;
    std::wstring m_fontName;
    std::vector<std::wstring> m_mruPaths;
    // Phase 1+2
    int          m_extStripMode;
    bool         m_stripTrailingNumber;
    bool         m_breakDDir;
    bool         m_openFolderAfterExtract;
    std::wstring m_openFolderCommand;
    int          m_concurrentLimit;

    std::wstring ReadStr(const wchar_t* section, const wchar_t* key, const wchar_t* def) const;
    void         WriteStr(const wchar_t* section, const wchar_t* key, const wchar_t* val) const;
    void         BuildIniPath() const;
};
