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
	static bool getFolderDlg( char* buf, HWND par, const char* title, const char* def );
	static void getFolderDlgOfEditBox( HWND wnd, HWND par, const char* title );

	// Display last error
	static void msgLastError( const char* msg = NULL );

	// Does the file exist?
	static bool exist( const char* fname );
	static bool isdir( const char* fname );

	// Check if system locale is Japanese
	static bool isJapaneseLocale();

	// Get localized string (Japanese or English)
	static const char* getLocalizedString( int id );
};

#endif
