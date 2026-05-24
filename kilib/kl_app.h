//--- K.I.LIB ---
// kl_app.h : application class for K.I.LIB

#ifndef AFX_KIAPP_H__AC24C8AF_2187_4873_83E8_AB4F2325017B__INCLUDED_
#define AFX_KIAPP_H__AC24C8AF_2187_4873_83E8_AB4F2325017B__INCLUDED_

/*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*/
// General-purpose application class

class kiApp
{
friend kiApp* app();
friend void kilib_startUp();

public: //-- Public interface --------------------------

	// Instance
	HINSTANCE inst() const
		{
			return m_hInst;
		}

	// Main window
	HWND mainhwnd() const
		{
			return m_pMainWnd ? m_pMainWnd->hwnd() : NULL;
		}
	kiWindow* mainwnd() const
		{
			return m_pMainWnd;
		}
	void setMainWnd( kiWindow* wnd )
		{
			m_pMainWnd = wnd;
		}

	// OS version
	const OSVERSIONINFO& osver() const
		{
			return m_OsVer;
		}

	// Message box
	int msgBox( const char* msg, const char* caption=NULL, UINT type=MB_OK )
		{
			return ::MessageBox( mainhwnd(), msg, caption, type );
		}

	// Free memory using the shell allocator
	void shellFree( void* ptr ) const
		{
			m_pShellAlloc->Free( ptr );
		}

	// Is the key with virtual code vKey pressed?
	static bool keyPushed( int vKey )
		{
			return( 0!=(::GetAsyncKeyState( vKey )>>15) );
		}

	// CommonControl / OLE initialization
	void shellInit()
		{
			if( !m_bShellInit )
			{
				::InitCommonControls();
				::OleInitialize( NULL );
				m_bShellInit = true;
			}
		}

#ifdef KILIB_LOG
	void log( const char* str )
		{
			if( !m_log.isOpened() )
			{
				kiPath logtxt( kiPath::Exe ); logtxt += "log.txt";
				m_log.open( logtxt, false );
			}
			m_log.write( str, ki_strlen(str) );
			m_log.write( "\r\n", 2 );
		}
#endif

protected: //-- For derived classes -----------------------------

	// Function called at startup. Required.
	virtual void run( kiCmdParser& cmd ) = 0;

protected: //-- Internal processing -----------------------------------

	kiApp()
		{
			st_pApp = this;
			m_hInst = ::GetModuleHandle( NULL );
			m_pMainWnd = NULL;
			m_bShellInit = false;
			m_OsVer.dwOSVersionInfoSize = sizeof( m_OsVer );
#pragma warning(push)
#pragma warning(disable:4996)
			::GetVersionEx( &m_OsVer );
#pragma warning(pop)
			::SHGetMalloc( &m_pShellAlloc );
		}

protected:

	virtual ~kiApp()
		{
			m_pShellAlloc->Release();
			if( m_bShellInit )
				::OleUninitialize();
		}

private:

	HINSTANCE      m_hInst;
	IMalloc*       m_pShellAlloc;
	bool           m_bShellInit;
	OSVERSIONINFO  m_OsVer;
	kiWindow*      m_pMainWnd;
	static kiApp* st_pApp;
#ifdef KILIB_LOG
	kiFile         m_log;
#endif
};

#endif
