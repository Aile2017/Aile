// stdafx.h — shared prefix header for Noah-origin source files (ArcB2e, Archiver, kilib).
// Included directly by those TUs; it is not configured as a CMake precompiled header.

#ifndef AFX_STDAFX_H__AILEFLOW__INCLUDED_
#define AFX_STDAFX_H__AILEFLOW__INCLUDED_

#undef  WINVER
#define WINVER    0x0601   // Windows 7+
#undef  _WIN32_IE
#define _WIN32_IE 0x0700
#include <windows.h>
#include <shlobj.h>

#include "kilib/kilibext.h"

#endif
