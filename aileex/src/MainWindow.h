#pragma once
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <vector>
#include <string>
#include <set>
#include <memory>
#include "ArchiveItem.h"
#include "WorkerThread.h"
#include "CompressDlg.h"
#include "RarProcess.h"
#include "IArchiveBackend.h"
#include "ArchiveSession.h"
#include "IArchiveUI.h"
#include "ArchiveController.h"

class MainWindow : public IArchiveUI {
public:
    bool Create(HINSTANCE hInst, int nCmdShow);
    // Returns true if the archive was opened successfully. On failure the previous
    // archive state is restored and an error box is shown.
    bool OpenArchive(const wchar_t* path);
    // Show the extract-destination dialog and perform extraction immediately.
    // Called after OpenArchive when the `x` action is given; skips the list view entirely.
    // presetDest: if non-empty, skip the folder picker and extract directly to this path.
    // Returns false only if the user cancelled the destination folder picker (so a
    // multi-archive batch can stop); true otherwise (extracted, empty, or skipped).
    bool TriggerExtract(const std::wstring& presetDest = L"");
    // Called after OpenArchive when the `t` action is given; fires the integrity test
    // directly. Returns the test result HRESULT (S_OK = passed/cancelled).
    HRESULT TriggerTest();
    HWND Hwnd() const { return m_hwnd; }
    // Call before TranslateAccelerator / IsDialogMessage in the message loop.
    // Returns true if the message was consumed.
    bool PreTranslateMessage(const MSG& msg);
    // Set a session-level destination override (from -d option or [...] browse button).
    // Takes priority over settings and archive parent dir until cleared.
    void SetExtractDestOverride(const std::wstring& path) { m_extractDestOverride = path; }

    static const wchar_t* ClassName() { return L"AileEx_MainWnd"; }
    static bool RegisterClass(HINSTANCE hInst);

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMsg(UINT msg, WPARAM wp, LPARAM lp);

    void OnCreate(HWND hwnd);
    void OnSize(int cx, int cy);
    void OnDropFiles(HDROP hDrop);
    void OnCommand(WORD id);
    void OnTreeSelChanged();
    void OnListDblClick();
    void OnExtract(const std::wstring& presetDest = L"");
    void OnExtractSelected(const std::wstring& presetDest = L"");
    // Toolbar extract: extract selected items if any are selected, otherwise extract all.
    void OnExtractSmart();
    void OnContextMenu(HWND hwndFrom, int x, int y);
    HRESULT OnTest();
    void OnOpenAssoc();
    void OnAddFiles();
    void OnAddFilesToCurrentArchive();
    // Worker-driven file addition to the currently open archive. Shows a file picker if `srcPaths` is empty.
    void AddFilesToCurrentArchive(std::vector<std::wstring> srcPaths);
    void OnInfo();
    void OnArchiveProperties();
    void OnArchiveComment();
    void OnDelete();
    void OnFileOpen();
    void OnAbout();
    void OnToggleTree();
    void OnToggleToolbar();
    void OnToggleIcons();
    void OnToggleMenubar();
    void OnInitMenuPopup(HMENU hMenu);
    void OnMruOpen(int idx);
    void RebuildMruMenu();
    void CloseArchive();  // Close the open archive and clear the view (does not quit the app)
    void OnCompress(CompressDlg::Params& params, bool openAfterCompress = false);

    void OnColumnClick(int col);
    void UpdateSortHeader();
    void CreateControls(HWND hwnd);
    void ResizePanes(int cx, int cy);
    void PopulateTree();
    void PopulateList(const std::wstring& folderPath);
    std::wstring SelectedFolderPath() const;
    // Search the session's folder list for `folderPath` and select it in the tree. Does nothing if not found.
    void SelectTreeFolder(const std::wstring& folderPath);
    // Returns false and shows error if 7z is required but not loaded.
    bool Ensure7zLoaded();
    // Creates m_tempViewDir on first call; shows error and returns false on failure.
    bool EnsureTempViewDir(const wchar_t* errorMsg);
    void ApplyFontToControls();
    // Refresh the "Extract to:" edit box to reflect the current archive + settings state.
    void UpdateExtractDestEdit();

    // --- IArchiveUI implementation (UI services for ArchiveController) ---
    OpResult RunOperation(const wchar_t* title,
                std::function<HRESULT(IExtractProgressSink*)> work) override;
    HRESULT RunRarCompress(const CompressDlg::Params& params) override;
    std::wstring PromptPassword() override;   // entered password, or "" if cancelled
    std::wstring SelectedFolder() override { return SelectedFolderPath(); }
    std::wstring ExtractDestOverride() override { return m_extractDestOverride; }
    void ApplyExtractDest(const std::wstring& dir) override {
        m_extractDestOverride = dir;
        UpdateExtractDestEdit();
    }
    bool BrowseDestFolder(std::wstring& dir) override;
    void ShowError(const wchar_t* msg, HRESULT hr = 0) override;
    void ShowMessage(const std::wstring& text, UINT iconFlags) override;
    int  Confirm(const std::wstring& text, const std::wstring& title) override;
    void OnArchiveOpened() override;
    void SelectFolder(const std::wstring& folder) override;

    HWND        m_hwnd         = nullptr;
    HWND        m_hToolbar     = nullptr;
    HWND        m_hExtractLabel  = nullptr;  // "Extract to:" label in toolbar row
    HWND        m_hExtractEdit   = nullptr;  // path edit box in toolbar row
    HWND        m_hExtractBrowse = nullptr;  // [...] browse button in toolbar row
    HWND        m_hTreeView    = nullptr;
    HWND        m_hListView    = nullptr;
    HWND        m_hStatus      = nullptr;
    HIMAGELIST  m_hSysImageList = nullptr;
    HIMAGELIST  m_hToolbarImages = nullptr;  // down-scaled toolbar icons
    HFONT       m_hFont        = nullptr;

    std::wstring             m_extractDestOverride;  // Set by -d option or [...] browse; overrides settings
    // Archive-domain state (open archive's paths, password, backend, listing).
    // Holds the responsibilities that used to live directly on MainWindow.
    ArchiveSession           m_session;
    // Orchestrates archive operations against m_session, using this window as its
    // UI (IArchiveUI). Declared after m_session so the reference is valid.
    ArchiveController        m_controller{ m_session, *this };
    WorkerThread             m_worker;
    ProgressPostSink*        m_pSink = nullptr;
    std::wstring             m_tempViewDir;   // session temp dir; deleted on exit
    int                      m_sortCol = 0;   // 0=Name, 1=Size, 2=Compressed, 3=Type, 4=Modified
    bool                     m_sortAsc = true;
    int                      m_treeWidth = 220;      // current splitter position
    bool                     m_draggingSplitter = false;
    bool                     m_treeVisible = true;   // Toggled from the View menu
    bool                     m_toolbarVisible = true; // Toggled from the View menu
    bool                     m_iconsVisible = true;  // Toggled from the View menu
    bool                     m_menubarVisible = true; // Toggled from View menu / F10
    HMENU                    m_hMenu = nullptr;       // Saved menu bar handle for show/hide
    int                      m_iconIndexFolder = -1; // cached folder icon index
    HMENU                    m_hMruMenu = nullptr;   // Submenu for recently used archives

    static constexpr int kSplitterW = 3;
    static constexpr int kTreeMinW  = 80;
    static constexpr int kListMinW  = 80;
    static constexpr int kToolbarH  = 38;  // 24px icon + 10 vertical padding + frame
    static constexpr int kStatusH   = 22;
};
