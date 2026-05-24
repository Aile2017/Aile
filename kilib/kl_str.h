//--- K.I.LIB ---
// kl_str.h : string classes for K.I.LIB

#ifndef AFX_KISTR_H__1932CA2C_ACA6_4606_B57A_ACD0B7D1D35B__INCLUDED_
#define AFX_KISTR_H__1932CA2C_ACA6_4606_B57A_ACD0B7D1D35B__INCLUDED_

/*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*/
// kiStr : Simple string

class kiStr
{
friend void kilib_startUp();

private: //-- Global initialization etc. ---------------------

	static void init();

public: //-- Public interface --------------------------

	// Speed up 2-byte character processing (or at least feel like it)
	static char* next( char* p )
		{ return p+st_lb[(*p)&0xff]; }
	static const char* next( const char* p )
		{ return p+st_lb[(*p)&0xff]; }
	static bool isLeadByte( char c )
		{ return st_lb[c&0xff]==2; }

	// Initialize
	kiStr( int start_size = 100 );
	kiStr( const char* s, int min_size = 100 );
	explicit kiStr( const kiStr& s );

	// Operators
	kiStr& operator = ( const kiStr& );
	kiStr& operator = ( const char* s );
	kiStr& operator += ( const char* s );
	kiStr& operator += ( char c );
	bool operator == ( const char* s ) const;
	bool equalsIgnoreCase( const char* s ) const;
	operator const char*()             const;
	int len()                          const;
	void lower()
		{ ::CharLower(m_pBuf); }
	void upper()
		{ ::CharUpper(m_pBuf); }
	kiStr& setInt( int n, bool cm=false );
	void replaceToSlash() {
		for(char* p=m_pBuf; *p; p=next(p))
			if(*p=='\\')
				*p='/';
	}
	void replaceToBackslash() { replaceToBackslash(m_pBuf); }
	static void replaceToBackslash(char* p) {
		for(; *p; p=next(p))
			if(*p=='/')
				*p='\\';
	}

	// Load from resource
	kiStr& loadRsrc( UINT id );

	kiStr& removeTrailWS();

protected: //-- For derived classes -----------------------------

	char* m_pBuf;
	int   m_ALen;

private: //-- Internal processing -------------------------------------

	static char st_lb[256];

public:

	virtual ~kiStr();
};

inline const kiStr operator+(const kiStr& x, const kiStr& y)
	{ return kiStr(x) += y; }
inline const kiStr operator+(const char* x, const kiStr& y)
	{ return kiStr(x) += y; }
inline const kiStr operator+(const kiStr& x, const char* y)
	{ return kiStr(x) += y; }

/*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*/
// kiPath : string with path-specific utility functions

class kiPath : public kiStr
{
public: //-- Public interface --------------------------

	// Initialize
	kiPath() : kiStr( MAX_PATH ){}
	explicit kiPath( const char* s ) : kiStr( s, MAX_PATH ){}
	explicit kiPath( const kiStr& s ) : kiStr( s, MAX_PATH ){}
	explicit kiPath( const kiPath& s ) : kiStr( s, MAX_PATH ){}
	kiPath( int nPATH, bool bs = true ) : kiStr( MAX_PATH )
		{
			beSpecialPath( nPATH );
			if( nPATH != Exe_name )
				beBackSlash( bs );
		}

	// operator
	void operator = ( const char* s ){ kiStr::operator =(s); }

	// Get special path
	void beSpecialPath( int nPATH );
	enum { Win=0x1787, Sys, Tmp, Prg, Exe, Cur, Exe_name,
			Snd=CSIDL_SENDTO, Dsk=CSIDL_DESKTOP, Doc=CSIDL_PERSONAL };

	// Short path
	void beShortPath();

	// Control trailing backslash
	void beBackSlash( bool add );

	// Directory name only
	bool beDirOnly();
	// Filename excluding all extensions
	void getBody( kiStr& str ) const;
	// Filename excluding one extension
	void getBody_all( kiStr& str ) const;

	// Multi-level mkdir
	void mkdir();
	// Multi-level rmdir
	void remove();

	// Drive type
	UINT getDriveType() const;
	// Whether in the same directory
	bool isInSameDir(const char* r) const;

	// [static] Extract filename only, without directory info
	static const char* name( const char* str );
	// [static] Last extension. NULL if none.
	static const char* ext( const char* str );
	// [static] All extensions. NULL if none.
	static const char* ext_all( const char* str );

	// non-static-ver
	const char* name() const
		{ return name(m_pBuf); }
	const char* ext() const
		{ return ext(m_pBuf); }
	const char* ext_all() const
		{ return ext_all(m_pBuf); }
};

#endif
