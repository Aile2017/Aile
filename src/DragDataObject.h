// Minimal IDataObject exposing CF_HDROP only, for dragging files out of the archive
// list to Explorer / another app.
//
// SHCreateDataObject was tried first but doesn't fit here: its `apidl` entries must be
// child PIDLs relative to a single common `pidlFolder`, while a multi-item selection's
// extracted paths (mirroring the archive's internal folder structure under the scratch
// temp dir) can sit at different depths with no single immediate parent. Passing
// `pidlFolder=nullptr` with absolute PIDLs is not a documented/supported combination —
// it produced a data object Explorer couldn't read any format from, showing the
// no-drop cursor for the whole drag. A hand-built CF_HDROP HGLOBAL sidesteps the shell
// namespace entirely and needs no shared parent, matching how CF_HDROP is normally
// produced (e.g. WM_DROPFILES).
#pragma once
#include <windows.h>
#include <ole2.h>
#include <shlobj.h>
#include <shellapi.h>
#include <string>
#include <vector>

class CDragDataObject : public IDataObject {
public:
    explicit CDragDataObject(const std::vector<std::wstring>& paths) {
        m_hGlobal = BuildDropFiles(paths);
    }
    ~CDragDataObject() {
        if (m_hGlobal) GlobalFree(m_hGlobal);
    }

    bool IsValid() const { return m_hGlobal != nullptr; }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (iid == IID_IUnknown || iid == IID_IDataObject) {
            *ppv = static_cast<IDataObject*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return (ULONG)InterlockedIncrement(&m_refCount);
    }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&m_refCount);
        if (r == 0) delete this;
        return (ULONG)r;
    }

    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* pFormatetcIn, STGMEDIUM* pmedium) override {
        if (!pFormatetcIn || !pmedium) return E_INVALIDARG;
        if (!Matches(*pFormatetcIn)) return DV_E_FORMATETC;
        HGLOBAL dup = (HGLOBAL)OleDuplicateData(m_hGlobal, CF_HDROP, 0);
        if (!dup) return E_OUTOFMEMORY;
        pmedium->tymed          = TYMED_HGLOBAL;
        pmedium->hGlobal        = dup;
        pmedium->pUnkForRelease = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* pFormatetc) override {
        if (!pFormatetc) return E_INVALIDARG;
        return Matches(*pFormatetc) ? S_OK : DV_E_FORMATETC;
    }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC* pOut) override {
        if (pOut) pOut->ptd = nullptr;
        return DATA_S_SAMEFORMATETC;
    }
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC** ppEnum) override {
        if (!ppEnum) return E_INVALIDARG;
        *ppEnum = nullptr;
        if (dwDirection != DATADIR_GET) return E_NOTIMPL;
        FORMATETC fmt = Format();
        return SHCreateStdEnumFmtEtc(1, &fmt, ppEnum);
    }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }

private:
    static FORMATETC Format() {
        FORMATETC fmt = {};
        fmt.cfFormat = CF_HDROP;
        fmt.dwAspect = DVASPECT_CONTENT;
        fmt.lindex   = -1;
        fmt.tymed    = TYMED_HGLOBAL;
        return fmt;
    }
    static bool Matches(const FORMATETC& f) {
        return f.cfFormat == CF_HDROP && (f.tymed & TYMED_HGLOBAL) &&
               f.dwAspect == DVASPECT_CONTENT;
    }
    // Builds a DROPFILES HGLOBAL: header + double-null-terminated wide path list,
    // the same layout Explorer produces for WM_DROPFILES / CF_HDROP.
    static HGLOBAL BuildDropFiles(const std::vector<std::wstring>& paths) {
        size_t charCount = 1;  // final extra NUL terminating the whole list
        for (auto& p : paths) charCount += p.size() + 1;
        size_t totalBytes = sizeof(DROPFILES) + charCount * sizeof(wchar_t);

        HGLOBAL hGlobal = GlobalAlloc(GHND, totalBytes);
        if (!hGlobal) return nullptr;
        BYTE* base = (BYTE*)GlobalLock(hGlobal);
        if (!base) { GlobalFree(hGlobal); return nullptr; }

        auto* df   = (DROPFILES*)base;
        df->pFiles = sizeof(DROPFILES);
        df->pt.x = df->pt.y = 0;
        df->fNC   = FALSE;
        df->fWide = TRUE;

        wchar_t* dst = (wchar_t*)(base + sizeof(DROPFILES));
        for (auto& p : paths) {
            wcscpy_s(dst, p.size() + 1, p.c_str());
            dst += p.size() + 1;
        }
        *dst = L'\0';

        GlobalUnlock(hGlobal);
        return hGlobal;
    }

    LONG    m_refCount = 1;
    HGLOBAL m_hGlobal  = nullptr;
};
