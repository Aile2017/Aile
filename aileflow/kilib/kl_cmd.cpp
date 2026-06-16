//--- K.I.LIB ---
// kl_cmd.h : commandline parser (UTF-16)

#include "stdafx.h"
#include "kilib.h"


//------------------------ String memory processing etc. -----------------------//


kiCmdParser::kiCmdParser( wchar_t* cmd, bool ignoreFirst )
{
	m_Buffer = NULL;
	if( cmd )
		doit( cmd, ignoreFirst );
}

kiCmdParser::kiCmdParser( const wchar_t* cmd, bool ignoreFirst )
{
	m_Buffer=NULL;
	if( cmd )
	{
		m_Buffer = new wchar_t[ ki_strlen(cmd)+1 ];
		ki_strcpy( m_Buffer, cmd );
		doit( m_Buffer, ignoreFirst );
	}
}

kiCmdParser::~kiCmdParser()
{
	delete [] m_Buffer;
}


//---------------------------- Splitting processing -----------------------------//


void kiCmdParser::doit( wchar_t* start, bool ignoreFirst )
{
	wchar_t* p=start;
	wchar_t endc;
	bool first = true;

	while( *p!=L'\0' )
	{
		// Skip extra whitespace
		while( *p==L' ' ) //|| *p=='\t' || *p=='\r' || *p=='\n' )
			p++;

		// If '"', record it and advance one more
		if( *p==L'"' )
			endc=L'"', p++;
		else
			endc=L' ';

		// If end-of-text, finish
		if( *p==L'\0' )
			break;

		if( first && ignoreFirst )
			first = false;
		else
		{
			// Save argument
			if( *p==L'-' )
				m_Switch.add( p );
			else
				m_Param.add( p );
		}

		// Move toward end of argument...
		while( *p!=endc && *p!=L'\0' )
			p++;

		// Terminate with '\0' to delimit argument
		if( *p!=L'\0' )
			*(p++) = L'\0';
	}
}
