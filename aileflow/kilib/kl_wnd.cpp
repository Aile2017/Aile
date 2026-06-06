//--- K.I.LIB ---
// kl_wnd.cpp : window information manager

#include "stdafx.h"
#include "kilib.h"


//-------- Processing to store kiWindow* in HWND at window creation -------//


kiWindow* kiWindow::st_pCurInit = NULL;
    HHOOK kiWindow::st_hHook    = NULL;

void kiWindow::init()
{
	// Set hook for CreateWindow
	st_hHook = ::SetWindowsHookEx( WH_CBT, &CBTProc, NULL, ::GetCurrentThreadId() );
}

void kiWindow::finish()
{
	// Remove hook for CreateWindow
	::UnhookWindowsHookEx( st_hHook );
}

LRESULT CALLBACK kiWindow::CBTProc( int code, WPARAM wp, LPARAM lp )
{
	if( code == HCBT_CREATEWND )
	{
		if( st_pCurInit )
		{
			// When a k.i.lib window is created via CreateWindow
			st_pCurInit->setHwnd( (HWND)wp );
			::SetWindowLongPtr( (HWND)wp, GWLP_USERDATA, (LONG_PTR)st_pCurInit );
			st_pCurInit = NULL;
		}
		else
			::SetWindowLongPtr( (HWND)wp, GWLP_USERDATA, 0 );
	}

	return ::CallNextHookEx( st_hHook, code, wp, lp );
}

void kiWindow::detachHwnd()
{
	::SetWindowLongPtr( hwnd(), GWLP_USERDATA, 0 );
	if( this == app()->mainwnd() )
		app()->setMainWnd( NULL );
	setHwnd( NULL );
}


//------------ Miscellaneous static window utility processing ---------------//


bool kiWindow::loopbreaker = false;

void kiWindow::msg()
{
	for( MSG msg; ::PeekMessage( &msg,NULL,0,0,PM_REMOVE ); )
		::TranslateMessage( &msg ), ::DispatchMessage( &msg );
}

void kiWindow::msgLoop()
{
	kiWindow* wnd;
	MSG msg;
	while( !loopbreaker && ::GetMessage( &msg,NULL,0,0 ) )
	{
		if( wnd = app()->mainwnd() )
		{
			// Translate accelerators before IsDialogMessage so modeless dialogs
			// can receive Ctrl+O / Ctrl+F / F1.
			if( wnd->m_hAccel )
				if( ::TranslateAccelerator( wnd->hwnd(), wnd->m_hAccel, &msg ) )
					continue;
			if( msg.message!=WM_CHAR && wnd->isDlgMsg( &msg ) )
				continue;
		}
		::TranslateMessage( &msg ), ::DispatchMessage( &msg );
	}
	loopbreaker = false;
}

void kiWindow::setFront( HWND wnd )
{
	// Special Thanks To kazubon !! ( the author of TClock )
	DWORD pid;
	DWORD th1 = ::GetWindowThreadProcessId( ::GetForegroundWindow(), &pid );
	DWORD th2 = ::GetCurrentThreadId();
	::AttachThreadInput( th2, th1, TRUE );
	::BringWindowToTop( wnd );
	::SetForegroundWindow( wnd );
	::AttachThreadInput( th2, th1, FALSE );
}

void kiWindow::setCenter( HWND wnd, HWND rel )
{
	RECT rc,pr;
	::GetWindowRect( wnd, &rc );

	if( rel )
		::GetWindowRect( rel, &pr );
	else
		::SystemParametersInfo( SPI_GETWORKAREA, 0, &pr, 0 );

	::SetWindowPos( wnd, 0,
		pr.left + ( (pr.right-pr.left)-(rc.right-rc.left) )/2,
		pr.top  + ( (pr.bottom-pr.top)-(rc.bottom-rc.top) )/2,
		0, 0, SWP_NOSIZE|SWP_NOZORDER );
}


//------------------ Processing as Window base class ----------------------//


kiWindow::kiWindow()
{
	m_hWnd = NULL;
	m_hAccel = NULL;
	app()->shellInit();
}

kiWindow::~kiWindow()
{
	if( m_hWnd && ::IsWindow( m_hWnd ) )
	{
		::SetWindowLongPtr( m_hWnd, GWLP_USERDATA, 0 );
		::DestroyWindow( m_hWnd );
	}
}

void kiWindow::loadAccel( UINT id )
{
	m_hAccel = ::LoadAccelerators( app()->inst(), MAKEINTRESOURCE(id) );
}


//---------------- Standalone window processing ---------------------//

// ...incomplete...

//---------------------------- Dialog -----------------------------//


kiDialog::kiDialog( UINT id )
{
	m_Rsrc = id;
}

void kiDialog::doModal( HWND parent )
{
	setState( true );
	preCreate( this );

	::DialogBoxParam( app()->inst(), MAKEINTRESOURCE(m_Rsrc),
						parent, commonDlg, (LPARAM)this );
}

void kiDialog::createModeless( HWND parent )
{
	setState( false );
	preCreate( this );

	::CreateDialogParam( app()->inst(), MAKEINTRESOURCE(m_Rsrc),
						parent, commonDlg, (LPARAM)this );

	::ShowWindow( hwnd(), SW_SHOW );
	::UpdateWindow( hwnd() );
}

void kiDialog::end( UINT endcode )
{
	setEndCode( endcode );

	if( isModal() )
		::EndDialog( hwnd(), getEndCode() );
	else
		::DestroyWindow( hwnd() );
}

INT_PTR kiDialog::commonDlg( HWND dlg, UINT msg, WPARAM wp, LPARAM lp )
{
	// Get pointer to kiDialog interface
	kiDialog* ptr = (kiDialog*)::GetWindowLongPtr( dlg, GWLP_USERDATA );
	if( !ptr ) return FALSE;

	// Call onInit if WM_INITDIALOG
	if( msg == WM_INITDIALOG )
		return ptr->onInit();

	// OK / Cancel processing
	else if( msg == WM_COMMAND )
	{
		switch( LOWORD(wp) )
		{
		case IDOK:
			if( ptr->onOK() )
				ptr->end( IDOK );
			return TRUE;
		case IDCANCEL:
			if( ptr->onCancel() )
				ptr->end( IDCANCEL );
			return TRUE;
		}
	}

	// Ordinary message
	BOOL ans = ptr->proc( msg, wp, lp );

	// Detach window handle on WM_DESTROY
	if( msg == WM_DESTROY )
		ptr->detachHwnd();

	return ans;
}


//------------------------ PropertySheet -------------------------//


kiPropSheet* kiPropSheet::st_CurInitPS = NULL;

kiPropSheet::kiPropSheet() : kiDialog( 0 )
{
	ki_memzero( &m_Header, sizeof(m_Header) );
	m_Header.dwSize      = sizeof(m_Header);
	m_Header.dwFlags     |=PSH_USECALLBACK | PSH_PROPSHEETPAGE;
	m_Header.pfnCallback = main_initProc;
	m_Header.hInstance   = app()->inst();
	m_Header.nStartPage  = 0;
}

void kiPropSheet::begin()
{
	int l = m_Pages.len();
	PROPSHEETPAGE* ppsp = new PROPSHEETPAGE[ l ];
	ki_memzero( ppsp, sizeof(PROPSHEETPAGE)*l );

	for( int i=0; i<l; i++ )
	{
		ppsp[i].dwSize      = sizeof( PROPSHEETPAGE );
		ppsp[i].hInstance   = app()->inst();
		ppsp[i].pfnCallback = page_initProc;
		ppsp[i].pfnDlgProc  = page_cmmnProc;
		ppsp[i].dwFlags     = PSP_USECALLBACK | PSP_HASHELP;
		m_Pages[i]->setInfo( ppsp+i );
	}

	m_Header.ppsp   = ppsp;
	m_Header.nPages = l;

	st_CurInitPS = this;
	PropertySheet( &m_Header );
	delete [] ppsp;
}

void kiPropSheet::doModal( HWND parent )
{
	m_Header.dwFlags &= (~PSH_MODELESS);
	setState( true );
	begin();
}

void kiPropSheet::createModeless( HWND parent )
{
	m_Header.dwFlags |= PSH_MODELESS;
	setState( false );
	begin();
}

void kiPropSheet::end( UINT endcode )
{
	// Set exit code
	setEndCode( endcode );

	// Remove subclassing
	::SetWindowLongPtr( hwnd(), GWLP_WNDPROC, (LONG_PTR)m_DefProc );

	// End
	if( isModal() ) // Subclassing is removed, so end() should not be called again.
		::PostMessage( hwnd(), WM_COMMAND, IDCANCEL, 0 );
	else
		::DestroyWindow( hwnd() );

	// Behavior equivalent to WM_DESTROY
	detachHwnd();
}

LRESULT CALLBACK kiPropSheet::main_cmmnProc( HWND dlg, UINT msg, WPARAM wp, LPARAM lp )
{
	kiPropSheet* ptr = (kiPropSheet*)::GetWindowLongPtr( dlg, GWLP_USERDATA );
	if( !ptr )
		return 0;

	// First, default processing
	LRESULT result = ::CallWindowProc( ptr->m_DefProc, dlg, msg, wp, lp );

	// Close button treated as Cancel
	if( msg==WM_SYSCOMMAND && wp==SC_CLOSE )
		::PostMessage( dlg, WM_COMMAND, IDCANCEL, 0 );

	// Command processing
	else if( msg==WM_COMMAND )
	{
		switch( LOWORD(wp) )
		{
		case IDOK:
			if( ptr->onOK() )
				ptr->end( IDOK );
			return TRUE;
		case IDCANCEL:
			if( ptr->onCancel() )
				ptr->end( IDCANCEL );
			return TRUE;
		case IDAPPLY:
			ptr->onApply();
			break;
		case ID_KIPS_HELP:
			ptr->onHelp();
			break;
		default:
			ptr->onCommand( LOWORD(wp) );
			break;
		}
	}

	// Drag & drop
	else if( msg==WM_DROPFILES )
		ptr->onDrop( (HDROP)wp );

	return result;
}

struct DLGTEMPLATEEX
{
    WORD dlgVer;
    WORD signature;
    DWORD helpID;
    DWORD exStyle;
    DWORD style;
    WORD cDlgItems;
    short x;
    short y;
    short cx;
    short cy;
};

int CALLBACK kiPropSheet::main_initProc( HWND dlg, UINT msg, LPARAM lp )
{
	if( msg == PSCB_PRECREATE )
	{
		// Get address of the DWORD for window style
		DWORD* pst = ( 0xffff==((DLGTEMPLATEEX*)lp)->signature ) ? 
						&(((DLGTEMPLATEEX*)lp)->style) : &(((DLGTEMPLATE*)lp)->style);
		// Remove help button and add minimize button
		(*pst) &= ~DS_CONTEXTHELP;
		(*pst) |=  WS_MINIMIZEBOX;

		preCreate( st_CurInitPS );
    }
	else if( msg == PSCB_INITIALIZED )
	{
		// Remove extra menu that gets created for some reason
		HMENU sysm = ::GetSystemMenu( dlg, FALSE );
		::DeleteMenu( sysm, SC_SIZE, MF_BYCOMMAND );
		::DeleteMenu( sysm, SC_MAXIMIZE, MF_BYCOMMAND );

		// Always bring window to front at startup
		setFront( dlg );

		// Subclass the window
		st_CurInitPS->m_DefProc = (WNDPROC)::SetWindowLongPtr( dlg, GWLP_WNDPROC, (LONG_PTR)main_cmmnProc );
		st_CurInitPS->onInit();
	}
	return 0;
}

INT_PTR kiPropSheet::page_cmmnProc( HWND dlg, UINT msg, WPARAM wp, LPARAM lp )
{
	kiPropSheetPage* ptr = (kiPropSheetPage*)::GetWindowLongPtr( dlg, GWLP_USERDATA );
	if( !ptr )
		return FALSE;

	// Common processing here
	switch( msg )
	{
	case WM_INITDIALOG:
		return ptr->onInit();

	case WM_NOTIFY:
		switch( ((NMHDR*)lp)->code )
		{
		case PSN_APPLY:
			ptr->onOK();
			return TRUE;
		}
		break;

	case WM_COMMAND:
		if( lp )
			switch( HIWORD(wp) )
			{
			case BN_CLICKED:
			if((HWND)lp==::GetFocus())
			case EN_CHANGE:
			case CBN_SELCHANGE:
				PropSheet_Changed( ptr->parent()->hwnd(), dlg );
			}
		break;

	case WM_DESTROY:
		BOOL ans=ptr->proc( msg, wp, lp );
		ptr->detachHwnd();
		return ans;
	}

	return ptr->proc( msg, wp, lp );
}

UINT CALLBACK kiPropSheet::page_initProc( HWND dlg, UINT msg, LPPROPSHEETPAGE ppsp )
{
	if( msg == PSPCB_CREATE )
		preCreate( (kiWindow*)(ppsp->lParam) );
	return TRUE;
}

void kiPropSheetPage::setInfo( PROPSHEETPAGE* p )
{
	p->pszTemplate = MAKEINTRESOURCE( getRsrcID() );
	p->lParam      = (LPARAM)this;

	if( m_hIcon )
	{
		p->dwFlags|= PSP_USEHICON;
		p->hIcon = m_hIcon;
	}
}
