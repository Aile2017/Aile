//--- K.I.LIB ---
// kl_rythp.cpp : interpretor for simple script langauage 'Rythp'

#include "stdafx.h"
#include "kilibext.h"

//-------------------- Variant type variables --------------------------//

int kiVar::getInt()
{
	int n=0;
	bool minus = (*m_pBuf=='-');
	for( char* p = minus ? m_pBuf+1 : m_pBuf; *p; p=next(p) )
	{
		if( '0'>*p || *p>'9' )
			return 0;
		n = (10*n) + (*p-'0');
	}
	return minus ? -n : n;
}

kiVar& kiVar::quote()
{
	if( m_pBuf[0]=='\"' )
		return *this;
	const char* p = m_pBuf;
	for( ; *p; p=next(p) )
		if( *p==' ' )
			break;
	if( !(*p) )
		return *this;

	int ln=len()+1;
	if( m_ALen<ln+2 )
	{
		char* tmp = new char[m_ALen=ln+2];
		ki_memcpy( tmp+1,m_pBuf,ln );
		delete [] m_pBuf;
		m_pBuf = tmp;
	}
	else
		ki_memmov( m_pBuf+1,m_pBuf,ln );
	m_pBuf[0]=m_pBuf[ln]='\"', m_pBuf[ln+1]='\0';
	return *this;
}

kiVar& kiVar::unquote()
{
	if( *m_pBuf!='\"' )
		return *this;
	const char* last = m_pBuf+1;
	for( const char* p=m_pBuf+1; *p; p=next(p) )
		last=p;
	if( *last!='\"' )
		return *this;

	ki_memmov( m_pBuf,m_pBuf+1,(last-m_pBuf)-1 );
	m_pBuf[(last-m_pBuf)-1]='\0';
	return *this;
}

//---------------------- Initialize & destroy ----------------------------//

kiRythpVM::kiRythpVM()
{
	ele['%'] = "%";
	ele['('] = "(";
	ele[')'] = ")";
	ele['"'] = "\"";
	ele['/'] = "\n";
}

//---------------------- Split by parameter ----------------------//

char* kiRythpVM::split_tonext( char* p )
{
	while( *p!='\0' && ( *p=='\t' || *p==' ' || *p=='\r' || *p=='\n' ) )
		p++;
	return (*p=='\0' ? NULL : p);
}

char* kiRythpVM::split_toend( char* p )
{
	int kkc=0, dqc=0;
	while( *p!='\0' && kkc>=0 )
	{
		if( *p=='(' && !(dqc&1) )
			kkc++;
		else if( *p==')' && !(dqc&1) )
			kkc--;
		else if( *p=='\"' )
			dqc++;
		else if( *p=='%' )
			p++;
		else if( (*p=='\t' || *p==' ' || *p=='\r' || *p=='\n') && kkc==0 && !(dqc&1) )
			return p;
		p++;
	}
	return (kkc==0 && !(dqc&1)) ? p : NULL;
}

bool kiRythpVM::split( char* buf, kiArray<char*>& argv, kiArray<bool>& argb, int& argc )
{
	argv.empty(), argb.empty(), argc=0;

	for( char* p=buf; p=split_tonext(p); p++,argc++ )
	{
		argv.add( p );
		argb.add( *p=='(' );

		if( !(p=split_toend(p)) )
			return false;

		if( argv[argc][0]=='(' || argv[argc][0]=='"' )
			argv[argc]++, *(p-1)='\0';
		if( *p=='\0' )
		{
			argc++;
			break;
		}
		*p='\0';
	}
	return true;
}

//------------------------- Execute -------------------------//

void kiRythpVM::eval( char* str, kiVar* ans )
{
	// Clear return value
	kiVar tmp,*aaa=&tmp;
	if(ans)
		*ans="",aaa=ans;

	// Split string in "function param1 param2 ..." format into parameters
	kiArray<char*> av;
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

void kiRythpVM::getarg( char* a, bool b, kiVar* arg )
{
	kiVar t;
	const char* p;

	// If (...), call eval.
	if( b )
	{
		eval( a, &t ), *arg = t;
	}
	else
	{
		p = a;

		// Variable substitution
		*arg="";
		for( ; *p; *p && p++ )
			if( *p!='%' )
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
	static bool isIntStr( const char* str ) {
		for(;*str;++str)
			if( !('0'<=*str && *str<='9' || *str==',' || *str=='-') )
				return false;
		return true;
	}
}

bool kiRythpVM::exec_function( const kiVar& name,
		const kiArray<char*>& a, const kiArray<bool>& b,int c, kiVar* r )
{
//	The functions available in Minimum-Rythp are as follows.
//		exec, while, if, let, +, -, *, /, =, !, between, mod, <, >

	kiVar t;
	int i,A,B,C;

//----- ---- --- -- -  -   -
//-- (exec stmt stmt ...) returns last-result
//----- ---- --- -- -  -   -
	if( name=="exec" )
	{
		for( i=1; i<c; i++ )
			getarg( a[i],b[i],r );
	}
//----- ---- --- -- -  -   -
//-- (while condition body) returns last-result
//----- ---- --- -- -  -   -
	else if( name=="while" )
	{
		if( c>=3 )
		{
			// (special) Must copy since this code is called multiple times.
			int L1=ki_strlen(a[1]), L2=ki_strlen(a[2]);
			char* tmp = new char[ 1 + (L1>L2 ? L1 : L2) ];
			while( getarg( ki_strcpy(tmp,a[1]), b[1], &t ), t.getInt()!=0 )
				getarg( ki_strcpy(tmp,a[2]), b[2], r );
			delete [] tmp;
		}
	}
//----- ---- --- -- -  -   -
//-- (if condition true-branch [false-branch]) returns executed-result
//----- ---- --- -- -  -   -
	else if( name=="if" )
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
	else if( name=="let" )
	{
		if( c>=2 )
		{
			*r = "";
			for( i=2; i<c; i++ )
				getarg( a[i],b[i],&t ), *r+=t;
			ele[a[1][0]&0xff] = *r;
		}
	}
//----- ---- --- -- -  -   -
//-- (= valueA valueB) returns A==B ?
//----- ---- --- -- -  -   -
	else if( name=="=" )
	{
		if( c>=3 )
		{
			kiVar t2;
			getarg(a[1],b[1],&t),  A=t.getInt();
			getarg(a[2],b[2],&t2), B=t2.getInt();
			if( isIntStr(t) && isIntStr(t2) )
				*r = A==B ? "1" : "0";
			else
				*r = t==t2 ? "1" : "0";
		}
	}
//----- ---- --- -- -  -   -
//-- (between valueA valueB valueC) returns A <= B <= C ?
//----- ---- --- -- -  -   -
	else if( name=="between" )
	{
		if( c>=4 )
		{
			getarg(a[1],b[1],&t), A=t.getInt();
			getarg(a[2],b[2],&t), B=t.getInt();
			getarg(a[3],b[3],&t), C=t.getInt();
			*r = (A<=B && B<=C) ? "1" : "0";
		}
	}
//----- ---- --- -- -  -   -
//-- (< valueA valueB) returns A < B ?
//----- ---- --- -- -  -   -
	else if( name=="<" )
	{
		if( c>=3 )
		{
			getarg(a[1],b[1],&t), A=t.getInt();
			getarg(a[2],b[2],&t), B=t.getInt();
			*r = (A<B) ? "1" : "0";
		}
	}
//----- ---- --- -- -  -   -
//-- (> valueA valueB) returns A > B ?
//----- ---- --- -- -  -   -
	else if( name==">" )
	{
		if( c>=3 )
		{
			getarg(a[1],b[1],&t), A=t.getInt();
			getarg(a[2],b[2],&t), B=t.getInt();
			*r = (A>B) ? "1" : "0";
		}
	}
//----- ---- --- -- -  -   -
//-- (! valueA [valueB]) returns A!=B ? or !A
//----- ---- --- -- -  -   -
	else if( name=="!" )
	{
		if( c>=2 )
		{
			getarg(a[1],b[1],&t), A=t.getInt();
			if( c==2 )
				*r = A==0 ? "1" : "0";
			else
			{
				kiVar t2;
				getarg(a[2],b[2],&t2), B=t2.getInt();
				if( isIntStr(t) && isIntStr(t2) )
					*r = A!=B ? "1" : "0";
				else
					*r = t!=t2 ? "1" : "0";
			}
		}
	}
//----- ---- --- -- -  -   -
//-- (+ valueA valueB) returns A+B
//----- ---- --- -- -  -   -
	else if( name=="+" )
	{
		int A = 0;
		for( i=1; i<c; i++ )
			A += (getarg(a[i],b[i],&t), t.getInt());
		r->setInt(A);
	}
//----- ---- --- -- -  -   -
//-- (- valueA valueB) returns A-B
//----- ---- --- -- -  -   -
	else if( name=="-" )
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
	else if( name=="*" )
	{
		int A = 1;
		for( i=1; i<c; i++ )
			A *= (getarg(a[i],b[i],&t), t.getInt());
		r->setInt(A);
	}
//----- ---- --- -- -  -   -
//-- (/ valueA valueB) returns A/B
//----- ---- --- -- -  -   -
	else if( name=="/" )
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
	else if( name=="mod" )
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
	else if( name=="slash" )
	{
		if( c>=2 )
		{
			getarg(a[1],b[1],&t);
			*r = (const char*)t;
			r->replaceToSlash();
		}
	}
	else
		return false;
	return true;
}
