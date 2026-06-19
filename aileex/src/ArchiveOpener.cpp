#include "ArchiveOpener.h"
#include "SevenZip.h"
#include "UnrarDll.h"
#include "RarBackend.h"
#include "SevenZipBackend.h"
#include <cwchar>

ArchiveOpener::Result ArchiveOpener::Open(const wchar_t* path,
                                          const std::wstring& knownPassword,
                                          const PasswordPrompt& promptPassword) {
    Result result;

    const wchar_t* dot = path ? wcsrchr(path, L'.') : nullptr;
    bool isRar = dot && _wcsicmp(dot + 1, L"rar") == 0;

    // Build candidates in priority order, including only loaded engines.
    std::vector<std::unique_ptr<IArchiveBackend>> candidates;
    if (isRar && m_unrar.IsLoaded())
        candidates.push_back(std::make_unique<RarBackend>(m_unrar, m_rarExePath));
    if (m_sz.IsLoaded())
        candidates.push_back(std::make_unique<SevenZipBackend>(m_sz));

    result.anyCandidate = !candidates.empty();
    if (candidates.empty()) return result;  // caller surfaces the "no engine" error

    // Try every candidate with the given password; on the first S_OK take that
    // backend and its listing. S_FALSE (format mismatch) or a hard failure falls
    // through to the next candidate. Candidates are only consumed on success, so a
    // failed phase leaves them intact for the next password attempt.
    auto tryAll = [&](const std::wstring& pw) -> bool {
        for (auto& cand : candidates) {
            std::vector<ArchiveItem> items;
            std::wstring eff = path ? path : L"";
            HRESULT hr = cand->Open(path, items,
                                    pw.empty() ? nullptr : pw.c_str(), &eff);
            if (hr == S_OK) {
                result.backend       = std::move(cand);
                result.items         = std::move(items);
                result.effectivePath = std::move(eff);
                result.password      = pw;
                return true;
            }
        }
        return false;
    };

    if (tryAll(L"")) return result;
    // Failure may be an encrypted header: retry with the known password, then a prompt.
    if (!knownPassword.empty() && tryAll(knownPassword)) return result;
    if (promptPassword) {
        std::wstring pw = promptPassword();
        if (!pw.empty() && tryAll(pw)) return result;
    }
    return result;  // backend stays null → failure
}
