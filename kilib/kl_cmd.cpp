//--- K.I.LIB ---
// kl_cmd.h : commandline parser

#include "stdafx.h"
#include "kilib.h"


//------------------------ String memory processing etc. -----------------------//


kiCmdParser::kiCmdParser( char* cmd, bool ignoreFirst )
{
	m_Buffer = NULL;
	if( cmd )
		doit( cmd, ignoreFirst );
}

kiCmdParser::kiCmdParser( const char* cmd, bool ignoreFirst )
{
	m_Buffer=NULL;
	if( cmd )
	{
		m_Buffer = new char[ ki_strlen(cmd)+1 ];
		ki_strcpy( m_Buffer, cmd );
		doit( m_Buffer, ignoreFirst );
	}
}

kiCmdParser::~kiCmdParser()
{
	delete [] m_Buffer;
}


//---------------------------- Splitting processing -----------------------------//


void kiCmdParser::doit( char* start, bool ignoreFirst )
{
	char* p=start;
	char endc;
	bool first = true;

	while( *p!='\0' )
	{
		// Skip extra whitespace
		while( *p==' ' ) //|| *p=='\t' || *p=='\r' || *p=='\n' )
			p++;

		// If '"', record it and advance one more
		if( *p=='"' )
			endc='"', p++;
		else
			endc=' ';

		// If end-of-text, finish
		if( *p=='\0' )
			break;

		if( first && ignoreFirst )
			first = false;
		else
		{
			// Save argument
			if( *p=='-' )
				m_Switch.add( p );
			else
				m_Param.add( p );
		}

		// Move toward end of argument...
		while( *p!=endc && *p!='\0' )
			p++;

		// Terminate with '\0' to delimit argument
		if( *p!='\0' )
			*(p++) = '\0';
	}
}
