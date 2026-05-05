#pragma once
#include <windows.h>
#include "CompressDlg.h"
#include "ProgressDlg.h"

class ProgressPostSink;

// RAR (rar.exe / WinRAR.exe) を起動して圧縮し、ProgressDlg のメッセージループを
// 駆動して完了を待つ。known-issues.md「RAR 圧縮ルーティングは 2 経路」を
// 1 経路に集約するための共通エントリ。
//
// 戻り値:
//   起動失敗時は E_FAIL（progDlg は内部で Dismiss 済み）。
//   起動成功時は ProgressDlg::RunMessageLoop の戻り値（S_OK / E_ABORT / 失敗 HRESULT）。
//
// sink の所有権は呼出側。完了後に呼出側で delete すること。
HRESULT RunRarCompressSync(HWND parent,
                           const CompressDlg::Params& p,
                           const wchar_t* rarExePath,
                           ProgressDlg& progDlg,
                           ProgressPostSink* sink);
