#pragma once
// Shared context-menu handler: IShellExtInit + IContextMenu.
//
// One instance is created per right-click by the shell. IShellExtInit hands us
// the selected file paths; IContextMenu adds our submenu and runs the chosen
// verb by delegating to the app EXE via ShellExecuteW. Behaviour differs per
// app only through g_shellConfig (see ShellConfig.h).

#include <windows.h>
#include <shlobj.h>
#include <vector>
#include <string>

class CShellExt : public IShellExtInit, public IContextMenu {
public:
    CShellExt();

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    // IShellExtInit
    IFACEMETHODIMP Initialize(PCIDLIST_ABSOLUTE pidlFolder, IDataObject* pdo,
                              HKEY hkeyProgID) override;

    // IContextMenu
    IFACEMETHODIMP QueryContextMenu(HMENU hMenu, UINT indexMenu, UINT idCmdFirst,
                                    UINT idCmdLast, UINT uFlags) override;
    IFACEMETHODIMP InvokeCommand(CMINVOKECOMMANDINFO* pici) override;
    IFACEMETHODIMP GetCommandString(UINT_PTR idCmd, UINT uType, UINT* pReserved,
                                    CHAR* pszName, UINT cchMax) override;

private:
    ~CShellExt();

    // Verbs offered in the submenu. Order maps to menu offsets 0..N.
    enum class Verb { Open, Extract, Test, Compress };

    void   LaunchExe(const wchar_t* action, const std::wstring& target) const;
    std::wstring ResolveExePath() const;
    HBITMAP CreateMenuBitmap() const;

    long  m_ref;
    HBITMAP m_hMenuBmp = nullptr;
    std::vector<std::wstring> m_paths;   // selected items
    bool  m_allArchives = false;         // decides which verbs to show
    // Menu-offset → verb map filled in QueryContextMenu, read in InvokeCommand.
    std::vector<Verb> m_verbs;
};
