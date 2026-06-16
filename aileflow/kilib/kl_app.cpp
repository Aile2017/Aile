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

//-------------------- Allocator / runtime scaffolding ------------------------//
// AileFlow enters through wWinMain, not kilib's kilib_startUp(), so the original
// startup routine (which drove kiWindow and app()->run()) has been removed along
// with the kilib windowing framework.  The global operator new/delete overrides
// below remain process-wide and load-bearing, so they are intentionally kept.

void* operator new( size_t siz )
{
	return (void*)::GlobalAlloc( GMEM_FIXED, siz );
}

void* operator new[]( size_t siz )
{
	return (void*)::GlobalAlloc( GMEM_FIXED, siz );
}

void operator delete( void* ptr )
{
	::GlobalFree( (HGLOBAL)ptr );
}

void operator delete( void* ptr, size_t )
{
	::GlobalFree( (HGLOBAL)ptr );
}

void operator delete[]( void* ptr )
{
	::GlobalFree( (HGLOBAL)ptr );
}

void operator delete[]( void* ptr, size_t )
{
	::GlobalFree( (HGLOBAL)ptr );
}

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
