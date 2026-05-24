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

//-------------------- Startup code ------------------------//

void kilib_startUp()
{
	// For English locale testing
	//::SetThreadUILanguage(0x0409);

	//-- K.I.LIB initialization
	kiStr::init();
	kiWindow::init();

	//-- Clear keyboard state
	::GetAsyncKeyState( VK_SHIFT );

	//-- Create application instance
	kilib_create_new_app();
	if( app() )
	{
		// Command line splitting
		kiCmdParser cmd( ::GetCommandLine(), true );

		// Execute
		app()->run( cmd );
	}

	//-- K.I.LIB termination
	kiWindow::finish();

	delete app();
	::ExitProcess( 0 );
}

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
