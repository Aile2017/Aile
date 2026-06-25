#pragma once
// Internal helpers shared between SevenZip's split translation units
// (SevenZip.cpp core / SevenZipRead.cpp / SevenZipWrite.cpp).
// AileEx-only. Kept header-inline so each TU gets its own copy without a
// separate link unit; the functions here are small and leaf-level.
#include <windows.h>
#include "7zip/Archive/IArchive.h"   // PROPVARIANT, VT_* and UInt64 (via compat.h)

// 7-Zip returns integer properties (kpidSize / kpidPackSize) with a width that
// depends on the format: 7z/zip use VT_UI8, but 32-bit formats such as CAB use
// VT_UI4.  A strict `vt == VT_UI8` check silently drops those, showing size 0.
// Coerce any integer PROPVARIANT to UInt64 instead.  (An absent value — e.g. the
// per-file packed size of a solid CAB block — stays VT_EMPTY → 0, as expected.)
inline UInt64 PropToUInt64(const PROPVARIANT& p) {
    switch (p.vt) {
    case VT_UI8: return p.uhVal.QuadPart;
    case VT_UI4: return p.ulVal;
    case VT_UI2: return p.uiVal;
    case VT_UI1: return p.bVal;
    case VT_I8:  return p.hVal.QuadPart >= 0 ? (UInt64)p.hVal.QuadPart : 0;
    case VT_I4:  return p.lVal       >= 0 ? (UInt64)p.lVal       : 0;
    case VT_I2:  return p.iVal       >= 0 ? (UInt64)p.iVal       : 0;
    case VT_I1:  return p.cVal       >= 0 ? (UInt64)p.cVal       : 0;
    default:     return 0;
    }
}
