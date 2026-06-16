//--- K.I.LIB ---
// kl_app.h : application class for K.I.LIB

#include "stdafx.h"
#include "kilib.h"

//------------ Management of the single application object ------------//

kiApp* kiApp::st_pApp = NULL;

kiApp* app()
{
	return kiApp::st_pApp;
}

//-------------------- Runtime scaffolding ------------------------------------//
// AileFlow enters through wWinMain, not kilib's kilib_startUp(), so the original
// startup routine (which drove kiWindow and app()->run()) has been removed along
// with the kilib windowing framework.
//
// The legacy global operator new/delete overrides (which routed every allocation
// in the process through GlobalAlloc/GlobalFree) have been removed: the standard
// CRT operator new/delete are used everywhere now.  This is safe because nothing
// pairs operator new with GlobalFree/GlobalLock (the only Global*/Local* uses are
// self-contained HGLOBAL handles for drag-drop and Win32 API buffers), and kilib
// never null-checks a new[] result (so the only behavioral difference — throwing
// std::bad_alloc instead of returning NULL on OOM — is moot; both are fatal).

extern "C" void __cxa_pure_virtual()
{
	::ExitProcess( 1 );
}

int main()
{
	// Dummy main to avoid linker error when building without /ENTRY
	return 0;
}

//--------------------------------------------------------------//
