#include "CompressHelper.h"
#include "RarProcess.h"
#include "WorkerThread.h"
#include "resource.h"

HRESULT RunRarCompressSync(HWND parent,
                           const CompressDlg::Params& p,
                           const wchar_t* rarExePath,
                           ProgressDlg& progDlg,
                           ProgressPostSink* sink)
{
    RarAdvancedParams adv;
    adv.dictSize    = p.rarDictSize;
    adv.solid       = p.rarSolid;
    adv.threads     = p.rarThreads;
    adv.recoveryPct = p.rarRecoveryPct;
    adv.splitVolume = p.rarSplitVolume;
    adv.extra       = p.rarExtra;

    progDlg.SetSink(sink);
    RarProcess rar;
    const wchar_t* pw = p.password.empty() ? nullptr : p.password.c_str();
    if (!rar.Compress(p.inputFiles, p.outputPath.c_str(), p.method.c_str(),
                      rarExePath, pw, p.encryptHeaders,
                      parent, WM_APP_PROGRESS, WM_APP_DONE, &adv)) {
        progDlg.Dismiss();
        return E_FAIL;
    }
    return progDlg.RunMessageLoop([&]{ rar.Cancel(); });
}
