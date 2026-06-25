//--- K.I.LIB ---
// kl_str.h : string classes for K.I.LIB (UTF-16 / wchar_t)

#ifndef AFX_KISTR_H__1932CA2C_ACA6_4606_B57A_ACD0B7D1D35B__INCLUDED_
#define AFX_KISTR_H__1932CA2C_ACA6_4606_B57A_ACD0B7D1D35B__INCLUDED_

/*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*/
// kiStr : Simple string (wide / UTF-16)

class kiStr
{
public: //-- Character advance ---------------------------

	// With UTF-16 every code unit is fixed width, so advancing is trivial.
	// (The historical MBCS lead-byte table is gone.)
	static wchar_t* next( wchar_t* p )
		{ return p+1; }
	static const wchar_t* next( const wchar_t* p )
		{ return p+1; }
	static bool isLeadByte( wchar_t )
		{ return false; }

	// Initialize
	kiStr( int start_size = 100 );
	kiStr( const wchar_t* s, int min_size = 100 );
	explicit kiStr( const kiStr& s );

	// Operators
	kiStr& operator = ( const kiStr& );
	kiStr& operator = ( const wchar_t* s );
	kiStr& operator += ( const wchar_t* s );
	kiStr& operator += ( wchar_t c );
	bool operator == ( const wchar_t* s ) const;
	bool equalsIgnoreCase( const wchar_t* s ) const;
	operator const wchar_t*()          const;
	int len()                          const;
	void lower()
		{ ::CharLowerW(m_pBuf); }
	void upper()
		{ ::CharUpperW(m_pBuf); }
	kiStr& setInt( int n, bool cm=false );
	void replaceToSlash() {
		for(wchar_t* p=m_pBuf; *p; p=next(p))
			if(*p==L'\\')
				*p=L'/';
	}
	void replaceToBackslash() { replaceToBackslash(m_pBuf); }
	static void replaceToBackslash(wchar_t* p) {
		for(; *p; p=next(p))
			if(*p==L'/')
				*p=L'\\';
	}

	// Load from resource
	kiStr& loadRsrc( UINT id );

	kiStr& removeTrailWS();

	// Retained as a no-op for call sites (AileFlowKiLib.cpp): the MBCS
	// lead-byte table it used to populate no longer exists under UTF-16.
	static void standalone_init() {}

protected: //-- For derived classes -----------------------------

	wchar_t* m_pBuf;
	int      m_ALen;

public:

	virtual ~kiStr();
};

inline const kiStr operator+(const kiStr& x, const kiStr& y)
	{ return kiStr(x) += y; }
inline const kiStr operator+(const wchar_t* x, const kiStr& y)
	{ return kiStr(x) += y; }
inline const kiStr operator+(const kiStr& x, const wchar_t* y)
	{ return kiStr(x) += y; }

/*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*/
// kiPath : string with path-specific utility functions

class kiPath : public kiStr
{
public: //-- Public interface --------------------------

	// Initialize
	kiPath() : kiStr( MAX_PATH ){}
	explicit kiPath( const wchar_t* s ) : kiStr( s, MAX_PATH ){}
	explicit kiPath( const kiStr& s ) : kiStr( s, MAX_PATH ){}
	explicit kiPath( const kiPath& s ) : kiStr( s, MAX_PATH ){}
	kiPath( int nPATH, bool bs = true ) : kiStr( MAX_PATH )
		{
			beSpecialPath( nPATH );
			if( nPATH != Exe_name )
				beBackSlash( bs );
		}

	// operator
	void operator = ( const wchar_t* s ){ kiStr::operator =(s); }

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
	bool isInSameDir(const wchar_t* r) const;

	// [static] Extract filename only, without directory info
	static const wchar_t* name( const wchar_t* str );
	// [static] Last extension. NULL if none.
	static const wchar_t* ext( const wchar_t* str );
	// [static] All extensions. NULL if none.
	static const wchar_t* ext_all( const wchar_t* str );

	// non-static-ver
	const wchar_t* name() const
		{ return name(m_pBuf); }
	const wchar_t* ext() const
		{ return ext(m_pBuf); }
	const wchar_t* ext_all() const
		{ return ext_all(m_pBuf); }
};

#endif
