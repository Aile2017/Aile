#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <commctrl.h>
#include <string>
#include "resource.h"

// CLSIDs from AileEx and AileFlow shell extension configs
const wchar_t* CLSID_AILEEX = L"Software\\Classes\\CLSID\\{A50BB570-A951-4D73-A1B2-CA2B709FFD34}";
const wchar_t* CLSID_AILEFLOW = L"Software\\Classes\\CLSID\\{62EF5960-FE49-490D-BC9B-ADCCE789A7B3}";

struct ShellExtInfo {
    int checkboxId;
    std::wstring dllName;
    const wchar_t* clsidKey;
    bool is32Bit;
};

ShellExtInfo g_exts[] = {
    { IDC_CHK_AILEEX_64, L"AileExShell.dll", CLSID_AILEEX, false },
    { IDC_CHK_AILEFLOW_64, L"AileFlowShell.dll", CLSID_AILEFLOW, false },
    { IDC_CHK_AILEEX_32, L"AileExShell32.dll", CLSID_AILEEX, true },
    { IDC_CHK_AILEFLOW_32, L"AileFlowShell32.dll", CLSID_AILEFLOW, true },
};

std::wstring GetModuleDir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    PathRemoveFileSpecW(path);
    return path;
}

bool FileExists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool IsRegistered(const ShellExtInfo& info) {
    HKEY hKey;
    REGSAM sam = KEY_READ | (info.is32Bit ? KEY_WOW64_32KEY : KEY_WOW64_64KEY);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, info.clsidKey, 0, sam, &hKey) == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return true;
    }
    return false;
}

void RunRegsvr32(const std::wstring& dllPath, bool is32Bit, bool unregister) {
    wchar_t sysDir[MAX_PATH];
    if (is32Bit) {
        GetSystemWow64DirectoryW(sysDir, MAX_PATH);
        if (sysDir[0] == L'\0') { // 32-bit OS
            GetSystemDirectoryW(sysDir, MAX_PATH);
        }
    } else {
        GetSystemDirectoryW(sysDir, MAX_PATH);
    }

    std::wstring regsvr = std::wstring(sysDir) + L"\\regsvr32.exe";
    std::wstring args = unregister ? L" /u /s \"" : L" /s \"";
    args += dllPath + L"\"";

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpFile = regsvr.c_str();
    sei.lpParameters = args.c_str();
    sei.nShow = SW_HIDE;
    
    if (ShellExecuteExW(&sei) && sei.hProcess) {
        WaitForSingleObject(sei.hProcess, INFINITE);
        CloseHandle(sei.hProcess);
    }
}

INT_PTR CALLBACK DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    static std::wstring modDir;

    switch (uMsg) {
        case WM_INITDIALOG: {
            modDir = GetModuleDir();
            for (const auto& ext : g_exts) {
                std::wstring fullPath = modDir + L"\\" + ext.dllName;
                HWND hItem = GetDlgItem(hwndDlg, ext.checkboxId);
                if (!FileExists(fullPath)) {
                    EnableWindow(hItem, FALSE);
                    wchar_t buf[256];
                    GetWindowTextW(hItem, buf, 256);
                    wchar_t notFound[64];
                    LoadStringW(GetModuleHandle(nullptr), IDS_NOT_FOUND, notFound, 64);
                    wcscat_s(buf, notFound);
                    SetWindowTextW(hItem, buf);
                } else {
                    if (IsRegistered(ext)) {
                        SendMessageW(hItem, BM_SETCHECK, BST_CHECKED, 0);
                    }
                }
            }
            return TRUE;
        }

        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_BTN_OK) {
                for (const auto& ext : g_exts) {
                    std::wstring fullPath = modDir + L"\\" + ext.dllName;
                    if (FileExists(fullPath)) {
                        bool isChecked = SendDlgItemMessageW(hwndDlg, ext.checkboxId, BM_GETCHECK, 0, 0) == BST_CHECKED;
                        bool isReg = IsRegistered(ext);
                        if (isChecked && !isReg) {
                            RunRegsvr32(fullPath, ext.is32Bit, false);
                        } else if (!isChecked && isReg) {
                            RunRegsvr32(fullPath, ext.is32Bit, true);
                        }
                    }
                }
                wchar_t msg[256];
                wchar_t title[64];
                LoadStringW(GetModuleHandle(nullptr), IDS_MSG_UPDATED, msg, 256);
                LoadStringW(GetModuleHandle(nullptr), IDS_APP_TITLE, title, 64);
                MessageBoxW(hwndDlg, msg, title, MB_OK | MB_ICONINFORMATION);
                EndDialog(hwndDlg, IDOK);
                return TRUE;
            }
            else if (LOWORD(wParam) == IDC_BTN_CANCEL || LOWORD(wParam) == IDCANCEL) {
                EndDialog(hwndDlg, IDCANCEL);
                return TRUE;
            }
            break;
    }
    return FALSE;
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    InitCommonControls();
    DialogBoxParamW(hInstance, MAKEINTRESOURCEW(IDD_SETUP_DIALOG), nullptr, DialogProc, 0);
    return 0;
}
