//--- K.I.LIB ---
// kl_wcmn.h : windows-common-interface operation

#ifndef AFX_KIWINCOMMON_H__0686721C_CAFB_4C2C_9FE5_0F482EA6A60B__INCLUDED_
#define AFX_KIWINCOMMON_H__0686721C_CAFB_4C2C_9FE5_0F482EA6A60B__INCLUDED_

/*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*/
// Shell utility class

class kiSUtil
{
public:
	// Save/restore current directory
	static void switchCurDirToExeDir();

	// "Select Folder" dialog
	static bool getFolderDlg( wchar_t* buf, HWND par, const wchar_t* title, const wchar_t* def );
	static void getFolderDlgOfEditBox( HWND wnd, HWND par, const wchar_t* title );

	// Display last error
	static void msgLastError( const wchar_t* msg = NULL );

	// Does the file exist?
	static bool exist( const wchar_t* fname );
	static bool isdir( const wchar_t* fname );

	// Check if system locale is Japanese
	static bool isJapaneseLocale();

	// Get localized string (Japanese or English)
	static const wchar_t* getLocalizedString( int id );
};

#endif
