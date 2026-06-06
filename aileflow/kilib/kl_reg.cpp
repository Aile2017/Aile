//--- K.I.LIB ---
// kl_reg.h : registry and ini-file operation

#include "stdafx.h"
#include "kilib.h"


//--------------------------- ini: initialize ----------------------------//


void kiIniFile::setFileName( const char* ini, bool exepath )
{
	if( !exepath )
		m_FileName = "";
	else
	{
		m_FileName.beSpecialPath( kiPath::Exe );
		m_FileName.beBackSlash( true );
	}
	m_FileName += ini;
}


//--------------------------- ini: read functions ----------------------------//


int kiIniFile::getInt( const char* key, int defval )
{
	return ::GetPrivateProfileInt( m_CurSec, key, defval, m_FileName );
}

bool kiIniFile::getBool( const char* key, bool defval )
{
	return (0 != ::GetPrivateProfileInt( m_CurSec,
						key, defval?1:0, m_FileName ) );
}

const char* kiIniFile::getStr( const char* key, const char* defval )
{
	::GetPrivateProfileString( m_CurSec, key, defval,
					m_StrBuf, sizeof(m_StrBuf), m_FileName );
	return m_StrBuf;
}


//--------------------------- ini: write functions ----------------------------//


bool kiIniFile::putStr( const char* key, const char* val )
{
	return (FALSE != ::WritePrivateProfileString(
					m_CurSec, key, val, m_FileName ) );
}

bool kiIniFile::putInt( const char* key, int val )
{
	::wsprintf( m_StrBuf, "%d", val );
	return putStr( key, m_StrBuf );
}

bool kiIniFile::putBool( const char* key, bool val )
{
	return putStr( key, val ? "1" : "0" );
}
