//--- K.I.LIB ---
// kl_rythp.cpp : interpretor for simple script langauage 'Rythp' (UTF-16)

#include "stdafx.h"
#include "kilibext.h"

// Byte size of an N-element wchar_t run (the ki_mem* macros are byte-based).
#define WB(n)  ((n) * (int)sizeof(wchar_t))

//-------------------- Variant type variables --------------------------//

int kiVar::getInt()
{
	int n=0;
	bool minus = (*m_pBuf==L'-');
	for( wchar_t* p = minus ? m_pBuf+1 : m_pBuf; *p; p=next(p) )
	{
		if( L'0'>*p || *p>L'9' )
			return 0;
		n = (10*n) + (*p-L'0');
	}
	return minus ? -n : n;
}

kiVar& kiVar::quote()
{
	if( m_pBuf[0]==L'\"' )
		return *this;
	const wchar_t* p = m_pBuf;
	for( ; *p; p=next(p) )
		if( *p==L' ' )
			break;
	if( !(*p) )
		return *this;

	int ln=len()+1;
	if( m_ALen<ln+2 )
	{
		wchar_t* tmp = new wchar_t[m_ALen=ln+2];
		ki_memcpy( tmp+1,m_pBuf,WB(ln) );
		delete [] m_pBuf;
		m_pBuf = tmp;
	}
	else
		ki_memmov( m_pBuf+1,m_pBuf,WB(ln) );
	m_pBuf[0]=m_pBuf[ln]=L'\"', m_pBuf[ln+1]=L'\0';
	return *this;
}

kiVar& kiVar::unquote()
{
	if( *m_pBuf!=L'\"' )
		return *this;
	const wchar_t* last = m_pBuf+1;
	for( const wchar_t* p=m_pBuf+1; *p; p=next(p) )
		last=p;
	if( *last!=L'\"' )
		return *this;

	ki_memmov( m_pBuf,m_pBuf+1,WB((int)((last-m_pBuf)-1)) );
	m_pBuf[(last-m_pBuf)-1]=L'\0';
	return *this;
}

//---------------------- Initialize & destroy ----------------------------//

kiRythpVM::kiRythpVM()
{
	ele[L'%'] = L"%";
	ele[L'('] = L"(";
	ele[L')'] = L")";
	ele[L'"'] = L"\"";
	ele[L'/'] = L"\n";
}

//---------------------- Split by parameter ----------------------//

wchar_t* kiRythpVM::split_tonext( wchar_t* p )
{
	for(;;) {
		while( *p!=L'\0' && ( *p==L'\t' || *p==L' ' || *p==L'\r' || *p==L'\n' ) )
			p++;
		if( *p!=L';' )
			break;
		while( *p && *p!=L'\n' && *p!=L'\r' )
			p++;
	}
	return (*p==L'\0' ? NULL : p);
}

wchar_t* kiRythpVM::split_toend( wchar_t* p )
{
	int kkc=0, dqc=0;
	while( *p!=L'\0' && kkc>=0 )
	{
		if( *p==L'(' && !(dqc&1) )
			kkc++;
		else if( *p==L')' && !(dqc&1) )
			kkc--;
		else if( *p==L'\"' )
			dqc++;
		else if( *p==L'%' )
			p++;
		else if( (*p==L'\t' || *p==L' ' || *p==L'\r' || *p==L'\n') && kkc==0 && !(dqc&1) )
			return p;
		p++;
	}
	return (kkc==0 && !(dqc&1)) ? p : NULL;
}

bool kiRythpVM::split( wchar_t* buf, kiArray<wchar_t*>& argv, kiArray<bool>& argb, int& argc )
{
	argv.empty(), argb.empty(), argc=0;

	for( wchar_t* p=buf; p=split_tonext(p); p++,argc++ )
	{
		argv.add( p );
		argb.add( *p==L'(' );

		if( !(p=split_toend(p)) )
			return false;

		if( argv[argc][0]==L'(' || argv[argc][0]==L'"' )
			argv[argc]++, *(p-1)=L'\0';
		if( *p==L'\0' )
		{
			argc++;
			break;
		}
		*p=L'\0';
	}
	return true;
}

//------------------------- Execute -------------------------//

void kiRythpVM::eval( wchar_t* str, kiVar* ans )
{
	// Clear return value
	kiVar tmp,*aaa=&tmp;
	if(ans)
		*ans=L"",aaa=ans;

	// Split string in "function param1 param2 ..." format into parameters
	kiArray<wchar_t*> av;
	kiArray<bool> ab;
	int ac;
	if( split( str,av,ab,ac ) && ac )
	{
		// Get function name
		kiVar name;
		getarg( av[0],ab[0],&name );

		// Execute function!
		exec_function( name, av, ab, ac, aaa );
	}
}

void kiRythpVM::getarg( wchar_t* a, bool b, kiVar* arg )
{
	kiVar t;
	const wchar_t* p;

	// If (...), call eval.
	if( b )
	{
		eval( a, &t ), *arg = t;
	}
	else
	{
		p = a;

		// Variable substitution
		*arg=L"";
		for( ; *p; *p && p++ )
			if( *p!=L'%' )
			{
				*arg += *p;
			}
			else
			{
				p++, *arg+=ele[(*p)&0xff];
			}
	}
}

//------------------------- Minimum-Rythp environment -------------------------//

namespace {
	static bool isIntStr( const wchar_t* str ) {
		for(;*str;++str)
			if( !(L'0'<=*str && *str<=L'9' || *str==L',' || *str==L'-') )
				return false;
		return true;
	}
}

bool kiRythpVM::exec_function( const kiVar& name,
		const kiArray<wchar_t*>& a, const kiArray<bool>& b,int c, kiVar* r )
{
//	The functions available in Minimum-Rythp are as follows.
//		exec, while, if, let, +, -, *, /, =, !, between, mod, <, >

	kiVar t;
	int i,A,B,C;

//----- ---- --- -- -  -   -
//-- (exec stmt stmt ...) returns last-result
//----- ---- --- -- -  -   -
	if( name==L"exec" )
	{
		for( i=1; i<c; i++ )
			getarg( a[i],b[i],r );
	}
//----- ---- --- -- -  -   -
//-- (while condition body) returns last-result
//----- ---- --- -- -  -   -
	else if( name==L"while" )
	{
		if( c>=3 )
		{
			// (special) Must copy since this code is called multiple times.
			int L1=ki_strlen(a[1]), L2=ki_strlen(a[2]);
			wchar_t* tmp = new wchar_t[ 1 + (L1>L2 ? L1 : L2) ];
			while( getarg( ki_strcpy(tmp,a[1]), b[1], &t ), t.getInt()!=0 )
				getarg( ki_strcpy(tmp,a[2]), b[2], r );
			delete [] tmp;
		}
	}
//----- ---- --- -- -  -   -
//-- (if condition true-branch [false-branch]) returns executed-result
//----- ---- --- -- -  -   -
	else if( name==L"if" )
	{
		if( c>=3 )
		{
			if( getarg( a[1],b[1],&t ), t.getInt()!=0 )
				getarg( a[2],b[2],r );
			else if( c>=4 )
				getarg( a[3],b[3],r );
		}
	}
//----- ---- --- -- -  -   -
//-- (let variable-name value value ...) returns new-value
//----- ---- --- -- -  -   -
	else if( name==L"let" )
	{
		if( c>=2 )
		{
			*r = L"";
			for( i=2; i<c; i++ )
				getarg( a[i],b[i],&t ), *r+=t;
			ele[a[1][0]&0xff] = *r;
		}
	}
//----- ---- --- -- -  -   -
//-- (= valueA valueB) returns A==B ?
//----- ---- --- -- -  -   -
	else if( name==L"=" )
	{
		if( c>=3 )
		{
			kiVar t2;
			getarg(a[1],b[1],&t),  A=t.getInt();
			getarg(a[2],b[2],&t2), B=t2.getInt();
			if( isIntStr(t) && isIntStr(t2) )
				*r = A==B ? L"1" : L"0";
			else
				*r = t==t2 ? L"1" : L"0";
		}
	}
//----- ---- --- -- -  -   -
//-- (between valueA valueB valueC) returns A <= B <= C ?
//----- ---- --- -- -  -   -
	else if( name==L"between" )
	{
		if( c>=4 )
		{
			getarg(a[1],b[1],&t), A=t.getInt();
			getarg(a[2],b[2],&t), B=t.getInt();
			getarg(a[3],b[3],&t), C=t.getInt();
			*r = (A<=B && B<=C) ? L"1" : L"0";
		}
	}
//----- ---- --- -- -  -   -
//-- (< valueA valueB) returns A < B ?
//----- ---- --- -- -  -   -
	else if( name==L"<" )
	{
		if( c>=3 )
		{
			getarg(a[1],b[1],&t), A=t.getInt();
			getarg(a[2],b[2],&t), B=t.getInt();
			*r = (A<B) ? L"1" : L"0";
		}
	}
//----- ---- --- -- -  -   -
//-- (> valueA valueB) returns A > B ?
//----- ---- --- -- -  -   -
	else if( name==L">" )
	{
		if( c>=3 )
		{
			getarg(a[1],b[1],&t), A=t.getInt();
			getarg(a[2],b[2],&t), B=t.getInt();
			*r = (A>B) ? L"1" : L"0";
		}
	}
//----- ---- --- -- -  -   -
//-- (! valueA [valueB]) returns A!=B ? or !A
//----- ---- --- -- -  -   -
	else if( name==L"!" )
	{
		if( c>=2 )
		{
			getarg(a[1],b[1],&t), A=t.getInt();
			if( c==2 )
				*r = A==0 ? L"1" : L"0";
			else
			{
				kiVar t2;
				getarg(a[2],b[2],&t2), B=t2.getInt();
				if( isIntStr(t) && isIntStr(t2) )
					*r = A!=B ? L"1" : L"0";
				else
					*r = t!=t2 ? L"1" : L"0";
			}
		}
	}
//----- ---- --- -- -  -   -
//-- (+ valueA valueB) returns A+B
//----- ---- --- -- -  -   -
	else if( name==L"+" )
	{
		int A = 0;
		for( i=1; i<c; i++ )
			A += (getarg(a[i],b[i],&t), t.getInt());
		r->setInt(A);
	}
//----- ---- --- -- -  -   -
//-- (- valueA valueB) returns A-B
//----- ---- --- -- -  -   -
	else if( name==L"-" )
	{
		if( c >= 2 )
		{
			getarg(a[1],b[1],&t);
			int A = t.getInt();
			if( c==2 )
				A = -A;
			else
				for( i=2; i<c; ++i )
					 A -= (getarg(a[i],b[i],&t), t.getInt());
			r->setInt(A);
		}
		else
			r->setInt(0);
	}
//----- ---- --- -- -  -   -
//-- (* valueA valueB) returns A*B
//----- ---- --- -- -  -   -
	else if( name==L"*" )
	{
		int A = 1;
		for( i=1; i<c; i++ )
			A *= (getarg(a[i],b[i],&t), t.getInt());
		r->setInt(A);
	}
//----- ---- --- -- -  -   -
//-- (/ valueA valueB) returns A/B
//----- ---- --- -- -  -   -
	else if( name==L"/" )
	{
		if( c>=3 )
		{
			getarg(a[1],b[1],&t), A=t.getInt();
			getarg(a[2],b[2],&t), B=t.getInt();
			r->setInt( B ? A/B : A );
		}
	}
//----- ---- --- -- -  -   -
//-- (mod valueA valueB) returns A%B
//----- ---- --- -- -  -   -
	else if( name==L"mod" )
	{
		if( c>=3 )
		{
			getarg(a[1],b[1],&t), A=t.getInt();
			getarg(a[2],b[2],&t), B=t.getInt();
			r->setInt( B ? A%B : 0 );
		}
	}
//----- ---- --- -- -  -   -
//-- (slash valueA) returns A.replaceAll("\\", "/")
//----- ---- --- -- -  -   -
	else if( name==L"slash" )
	{
		if( c>=2 )
		{
			getarg(a[1],b[1],&t);
			*r = (const wchar_t*)t;
			r->replaceToSlash();
		}
	}
	else
		return false;
	return true;
}
