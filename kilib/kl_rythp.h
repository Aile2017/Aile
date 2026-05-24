//--- K.I.LIB ---
// kl_rythp.h : interpretor for simple script langauage 'Rythp'

#ifndef AFX_KIRYTHPVM_H__4F3C28A9_7EFE_4605_A149_2C0B9A9236E5__INCLUDED_
#define AFX_KIRYTHPVM_H__4F3C28A9_7EFE_4605_A149_2C0B9A9236E5__INCLUDED_

/*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*/
// kiVar : Variant type for Rythp. Mostly just a kiStr.

class kiVar : public kiStr
{
public:
	kiVar() : kiStr(20) {}
	explicit kiVar( const char* s ) : kiStr( s, 20 ){}
	explicit kiVar( const kiStr& s ) : kiStr( s, 20 ){}
	explicit kiVar( const kiVar& s ) : kiStr( s, 20 ){}
	void operator = ( const char* s ){ kiStr::operator =(s); }

	int getInt();
	kiVar& quote();
	kiVar& unquote();
};

/*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*/
// kiRythpVM : Minimal Rythp. Derive and implement exec_function for practical use.

class kiRythpVM
{
public:
	kiRythpVM();
	virtual ~kiRythpVM() {}

public:
	// eval
	void eval( char* str, kiVar* ans=NULL );

protected:
	// Get arguments with eval and variable substitution applied
	void getarg( char* a, bool b, kiVar* arg );

	// Execute function. [ bool=handled?, name=function name, a,b,c=args, r=return value ]
	virtual bool exec_function( const kiVar& name,
		const CharArray& a, const BoolArray& b,int c, kiVar* r );

private:
	// Variables
	kiVar ele[256];

	// Parameter splitting
	static char* split_tonext( char* p );
	static char* split_toend( char* p );
	static bool split( char* buf, CharArray& argv, BoolArray& argb, int& argc );
};

#endif
