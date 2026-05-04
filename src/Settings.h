#pragma once
#include <windows.h>
#include <string>

class Settings {
public:
    void Load();
    void Save() const;

    const std::wstring& GetRarExtractor() const    { return m_rarExtractor; }
    void SetRarExtractor(const wchar_t* v)          { m_rarExtractor = v; }

    const std::wstring& GetRarExePath() const       { return m_rarExePath; }
    void SetRarExePath(const wchar_t* v)            { m_rarExePath = v; }

    const std::wstring& GetDefaultOutputDir() const { return m_defaultOutputDir; }
    void SetDefaultOutputDir(const wchar_t* v)      { m_defaultOutputDir = v; }

    const std::wstring& GetDefaultFormat() const    { return m_defaultFormat; }
    void SetDefaultFormat(const wchar_t* v)         { m_defaultFormat = v; }

    int  GetCompressionLevel() const                { return m_compressionLevel; }
    void SetCompressionLevel(int v)                 { m_compressionLevel = v; }

    int  GetRarLevel() const                        { return m_rarLevel; }
    void SetRarLevel(int v)                         { m_rarLevel = v; }

    // Advanced compress options (last-used values)
    const std::wstring& GetAdvDictSize() const      { return m_advDictSize; }
    const std::wstring& GetAdvWordSize() const      { return m_advWordSize; }
    const std::wstring& GetAdvSolidBlock() const    { return m_advSolidBlock; }
    const std::wstring& GetAdvThreads() const       { return m_advThreads; }
    const std::wstring& GetAdvExtra() const         { return m_advExtra; }
    void SetAdvDictSize(const wchar_t* v)           { m_advDictSize   = v; }
    void SetAdvWordSize(const wchar_t* v)           { m_advWordSize   = v; }
    void SetAdvSolidBlock(const wchar_t* v)         { m_advSolidBlock = v; }
    void SetAdvThreads(const wchar_t* v)            { m_advThreads    = v; }
    void SetAdvExtra(const wchar_t* v)              { m_advExtra      = v; }

    // Window placement
    int  GetWindowX() const          { return m_windowX; }
    int  GetWindowY() const          { return m_windowY; }
    int  GetWindowW() const          { return m_windowW; }
    int  GetWindowH() const          { return m_windowH; }
    bool GetWindowMaximized() const  { return m_windowMaximized; }
    int  GetSplitterPos() const      { return m_splitterPos; }
    void SetWindowPlacement(int x, int y, int w, int h, bool maximized) {
        m_windowX = x; m_windowY = y; m_windowW = w; m_windowH = h;
        m_windowMaximized = maximized;
    }
    void SetSplitterPos(int v)       { m_splitterPos = v; }

    const std::wstring& Get7zDllPath() const        { return m_7zDllPath; }
    void Set7zDllPath(const wchar_t* v)             { m_7zDllPath = v; }

    const std::wstring& GetUnrarDllPath() const     { return m_unrarDllPath; }
    void SetUnrarDllPath(const wchar_t* v)          { m_unrarDllPath = v; }

private:
    wchar_t m_iniPath[MAX_PATH] = {};

    std::wstring m_rarExtractor    = L"7z";
    std::wstring m_rarExePath;
    std::wstring m_defaultOutputDir;
    std::wstring m_defaultFormat   = L"7z";
    int          m_compressionLevel = 5;
    int          m_rarLevel         = 3;
    std::wstring m_advDictSize;
    std::wstring m_advWordSize;
    std::wstring m_advSolidBlock;
    std::wstring m_advThreads;
    std::wstring m_advExtra;
    int          m_windowX          = -1;   // -1 = use CW_USEDEFAULT
    int          m_windowY          = -1;
    int          m_windowW          = 900;
    int          m_windowH          = 600;
    bool         m_windowMaximized  = false;
    int          m_splitterPos      = 220;
    std::wstring m_7zDllPath;
    std::wstring m_unrarDllPath;

    std::wstring ReadStr(const wchar_t* section, const wchar_t* key, const wchar_t* def) const;
    void         WriteStr(const wchar_t* section, const wchar_t* key, const wchar_t* val) const;
    void         BuildIniPath();
};
