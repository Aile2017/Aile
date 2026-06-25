//--- K.I.LIB ---
// kl_wcmn.h : windows-common-interface operatin (UTF-16)

#include "stdafx.h"
#include "kilib.h"

void kiSUtil::switchCurDirToExeDir()
{
	wchar_t exepath[MAX_PATH+50];
	GetModuleFileNameW( NULL, exepath, MAX_PATH );
	wchar_t* lastslash = 0;
	for( wchar_t* p=exepath; *p; p=kiStr::next(p) )
		if( *p==L'\\' || *p==L'/' )
			lastslash = p;
	if(lastslash)
		*lastslash = L'\0';
	SetCurrentDirectoryW(exepath);
}

static int CALLBACK __ki__ofp( HWND w, UINT m, LPARAM l, LPARAM d )
{
	if( m==BFFM_INITIALIZED && d )
		::SendMessage( w, BFFM_SETSELECTION, TRUE, d );
	return 0;
}

bool kiSUtil::getFolderDlg( wchar_t* buf, HWND par, const wchar_t* title, const wchar_t* def )
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
			pfd->SetTitle( title );

		if( def && *def )
		{
			IShellItem* psi = NULL;
			if( SUCCEEDED(::SHCreateItemFromParsingName( def, NULL, IID_IShellItem, (void**)&psi )) )
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
					ki_strcpy( buf, pszPath );
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
	BROWSEINFOW bi;
	ki_memzero( &bi, sizeof(bi) );
	bi.hwndOwner      = par;
	bi.pszDisplayName = buf;
	bi.lpszTitle      = title;
	bi.ulFlags        = BIF_RETURNONLYFSDIRS | BIF_DONTGOBELOWDOMAIN;
	bi.lpfn           = __ki__ofp;
	bi.lParam         = reinterpret_cast<LPARAM>(def);

	LPITEMIDLIST id = ::SHBrowseForFolderW( &bi );
	if( id==NULL )
		return false;
	::SHGetPathFromIDListW( id, buf );
	app()->shellFree( id );
	return true;
}

void kiSUtil::getFolderDlgOfEditBox( HWND wnd, HWND par, const wchar_t* title )
{
	wchar_t str[MAX_PATH];
	::SendMessageW( wnd, WM_GETTEXT, MAX_PATH, (LPARAM)str );
	wchar_t* l = str;
	for( wchar_t* x=str; *x; x=kiStr::next(x) )
		l=x;
	if( *l==L'\\' || *l==L'/' )
		*l=L'\0';
	if( getFolderDlg( str, par, title, str ) )
		::SendMessageW( wnd, WM_SETTEXT, 0, (LPARAM)str );
}

void kiSUtil::msgLastError( const wchar_t* msg )
{
	wchar_t* pMsg;
	::FormatMessageW(
		FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,::GetLastError(),MAKELANGID(LANG_NEUTRAL,SUBLANG_DEFAULT),(LPWSTR)&pMsg,0,NULL );
	if( msg )
		app()->msgBox( kiStr(msg) + L"\r\n\r\n" + pMsg );
	else
		app()->msgBox( pMsg );
	::LocalFree( pMsg );
}

bool kiSUtil::exist( const wchar_t* fname )
{
	return 0xffffffff != ::GetFileAttributesW( fname );
}

bool kiSUtil::isdir( const wchar_t* fname )
{
	DWORD attr = ::GetFileAttributesW( fname );
	return attr!=0xffffffff && (attr&FILE_ATTRIBUTE_DIRECTORY);
}

bool kiSUtil::isJapaneseLocale()
{
	LANGID lid = ::GetUserDefaultLangID();
	return PRIMARYLANGID(lid) == LANG_JAPANESE;
}

const wchar_t* kiSUtil::getLocalizedString( int id )
{
	static wchar_t buf[256];
	kiStr s;
	s.loadRsrc( id );
	wcscpy_s( buf, _countof(buf), (const wchar_t*)s );
	return buf;
}
