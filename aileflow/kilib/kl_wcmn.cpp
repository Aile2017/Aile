//--- K.I.LIB ---
// kl_wcmn.h : windows-common-interface operatin

#include "stdafx.h"
#include "kilib.h"

void kiSUtil::switchCurDirToExeDir()
{
	char exepath[MAX_PATH+50];
	GetModuleFileName( NULL, exepath, MAX_PATH );
	char* lastslash = 0;
	for( char* p=exepath; *p; p=CharNext(p) )
		if( *p=='\\' || *p=='/' )
			lastslash = p;
	if(lastslash)
		*lastslash = '\0';
	SetCurrentDirectory(exepath);
}

static int CALLBACK __ki__ofp( HWND w, UINT m, LPARAM l, LPARAM d )
{
	if( m==BFFM_INITIALIZED && d )
		::SendMessage( w, BFFM_SETSELECTION, TRUE, d );
	return 0;
}

bool kiSUtil::getFolderDlg( char* buf, HWND par, const char* title, const char* def )
{
	// Use IFileOpenDialog (Vista+)
	IFileOpenDialog* pfd = NULL;
	HRESULT hr = ::CoCreateInstance( CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
	                                  IID_IFileOpenDialog, (void**)&pfd );
	if( SUCCEEDED(hr) )
	{
		FILEOPENDIALOGOPTIONS opts = 0;
		pfd->GetOptions( &opts );
		pfd->SetOptions( opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM );

		if( title && *title )
		{
			WCHAR wtitle[256];
			::MultiByteToWideChar( CP_ACP, 0, title, -1, wtitle, 256 );
			pfd->SetTitle( wtitle );
		}

		if( def && *def )
		{
			WCHAR wdef[MAX_PATH];
			::MultiByteToWideChar( CP_ACP, 0, def, -1, wdef, MAX_PATH );
			IShellItem* psi = NULL;
			if( SUCCEEDED(::SHCreateItemFromParsingName( wdef, NULL, IID_IShellItem, (void**)&psi )) )
			{
				pfd->SetFolder( psi );
				psi->Release();
			}
		}

		bool ok = false;
		if( SUCCEEDED(pfd->Show( par )) )
		{
			IShellItem* psi = NULL;
			if( SUCCEEDED(pfd->GetResult( &psi )) )
			{
				LPWSTR pszPath = NULL;
				if( SUCCEEDED(psi->GetDisplayName( SIGDN_FILESYSPATH, &pszPath )) )
				{
					::WideCharToMultiByte( CP_ACP, 0, pszPath, -1, buf, MAX_PATH, NULL, NULL );
					::CoTaskMemFree( pszPath );
					ok = true;
				}
				psi->Release();
			}
		}
		pfd->Release();
		return ok;
	}

	// Fallback: legacy SHBrowseForFolder
	BROWSEINFO bi;
	ki_memzero( &bi, sizeof(bi) );
	bi.hwndOwner      = par;
	bi.pszDisplayName = buf;
	bi.lpszTitle      = title;
	bi.ulFlags        = BIF_RETURNONLYFSDIRS | BIF_DONTGOBELOWDOMAIN;
	bi.lpfn           = __ki__ofp;
	bi.lParam         = reinterpret_cast<LPARAM>(def);

	LPITEMIDLIST id = ::SHBrowseForFolder( &bi );
	if( id==NULL )
		return false;
	::SHGetPathFromIDList( id, buf );
	app()->shellFree( id );
	return true;
}

void kiSUtil::getFolderDlgOfEditBox( HWND wnd, HWND par, const char* title )
{
	char str[MAX_PATH];
	::SendMessage( wnd, WM_GETTEXT, MAX_PATH, (LPARAM)str );
	char* l = str;
	for( char* x=str; *x; x=kiStr::next(x) )
		l=x;
	if( *l=='\\' || *l=='/' )
		*l='\0';
	if( getFolderDlg( str, par, title, str ) )
		::SendMessage( wnd, WM_SETTEXT, 0, (LPARAM)str );
}

void kiSUtil::msgLastError( const char* msg )
{
	char* pMsg;
	::FormatMessage( 
		FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,::GetLastError(),MAKELANGID(LANG_NEUTRAL,SUBLANG_DEFAULT),(LPTSTR)&pMsg,0,NULL );
	if( msg )
		app()->msgBox( kiStr(msg) + "\r\n\r\n" + pMsg );
	else
		app()->msgBox( pMsg );
	::LocalFree( pMsg );
}

bool kiSUtil::exist( const char* fname )
{
	return 0xffffffff != ::GetFileAttributes( fname );
}

bool kiSUtil::isdir( const char* fname )
{
	DWORD attr = ::GetFileAttributes( fname );
	return attr!=0xffffffff && (attr&FILE_ATTRIBUTE_DIRECTORY);
}

bool kiSUtil::isJapaneseLocale()
{
	LANGID lid = ::GetUserDefaultLangID();
	return PRIMARYLANGID(lid) == LANG_JAPANESE;
}

const char* kiSUtil::getLocalizedString( int id )
{
	static char buf[256];
	kiStr s;
	s.loadRsrc( id );
	strcpy_s( buf, sizeof(buf), (const char*)s );
	return buf;
}
