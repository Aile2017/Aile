//--- K.I.LIB ---
// kl_reg.h : registry and ini-file operation

#ifndef AFX_KIREGKEY_H__4FD5E1B3_B8FE_45B3_B19E_3D30407C94BA__INCLUDED_
#define AFX_KIREGKEY_H__4FD5E1B3_B8FE_45B3_B19E_3D30407C94BA__INCLUDED_

class kiIniFile
{
public: //-- Public interface --------------------------

	// Set ini filename
	void setFileName( const char* ini, bool exepath=true );
	void setSection( const char* section )
		{ m_CurSec = section; }

	// Read
	// Note: the return value of getStr is an internal buffer;
	// its contents are only valid immediately after the call.
	int getInt( const char* key, int defval );
	bool getBool( const char* key, bool defval );
	const char* getStr( const char* key, const char* defval );

	// Write
	bool putStr( const char* key, const char* val );
	bool putInt( const char* key, int val );
	bool putBool( const char* key, bool val );

private: //-- Internal processing -----------------------------------

	kiPath m_FileName;
	kiStr m_CurSec;
	char m_StrBuf[256];
};

#endif
