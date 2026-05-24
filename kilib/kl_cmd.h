//--- K.I.LIB ---
// kl_cmd.h : commandline parser

#ifndef AFX_KICMDPARSER_H__843A27E0_5DBF_48AF_A748_FA7F111F699A__INCLUDED_
#define AFX_KICMDPARSER_H__843A27E0_5DBF_48AF_A748_FA7F111F699A__INCLUDED_

/*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*/
// kiCmdParser : splits a command string into a char* array

class kiCmdParser
{
public: //-- Public interface --------------------------

	// Initialize with string
	kiCmdParser( char* cmd, bool ignoreFirst=false );
	kiCmdParser( const char* cmd, bool ignoreFirst=false );

	// Array of switch strings
	cCharArray& option()
		{ return m_Switch; }

	// Array of non-switch strings
	cCharArray& param()
		{ return m_Param; }

private: //-- Internal processing -----------------------------------

	void doit( char* start, bool ignoreFirst );
	cCharArray m_Param;
	cCharArray m_Switch;
	char* m_Buffer;

public:

	virtual ~kiCmdParser();
};

#endif
