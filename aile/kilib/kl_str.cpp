//--- K.I.LIB ---
// kl_str.cpp : string classes for K.I.LIB (UTF-16 / wchar_t)

#include "stdafx.h"
#include "kilib.h"

// Byte size of an N-element wchar_t run (the ki_mem* macros are byte-based).
#define WB(n)  ((n) * (int)sizeof(wchar_t))


//-------------------------- Various copy operations ------------------------//


kiStr::kiStr( int start_size )
{
	(m_pBuf = new wchar_t[ m_ALen = start_size ])[0] = L'\0';
}

kiStr::kiStr( const wchar_t* s, int min_size )
{
	int slen = ki_strlen(s) + 1;
	m_ALen = ( slen < min_size ) ? min_size : slen;
	ki_memcpy( m_pBuf=new wchar_t[m_ALen], s, WB(slen) );
}

kiStr::kiStr( const kiStr& s )
{
	ki_memcpy( m_pBuf=new wchar_t[m_ALen=s.m_ALen], s.m_pBuf, WB(m_ALen) );
}

kiStr& kiStr::operator = ( const kiStr& s )
{
	if( this != &s )
		*this = (const wchar_t*)s;
	return *this;
}

kiStr& kiStr::operator = ( const wchar_t* s )
{
	int slen = ki_strlen( s ) + 1;

	// Reallocate if buffer too small, or if s points into our own buffer (overlap)
	if( m_ALen < slen || (s >= m_pBuf && s < m_pBuf + m_ALen) )
	{
		wchar_t* tmp = new wchar_t[ m_ALen = ( m_ALen>slen ? m_ALen : slen) ];
		ki_memcpy( tmp, s, WB(slen) );   // copy s before freeing old buffer
		delete [] m_pBuf;
		m_pBuf = tmp;
	}
	else
		ki_memcpy( m_pBuf, s, WB(slen) );
	return *this;
}

kiStr& kiStr::operator += ( const wchar_t* s )
{
	int slen = ki_strlen( s ) + 1;
	int  len = this->len();

	// Reallocate if buffer too small, or if s points into our own buffer (overlap).
	// In the overlap case, copy s into the new buffer before freeing the old one
	// to avoid use-after-free.
	if( m_ALen < len+slen+1 || (s >= m_pBuf && s < m_pBuf + m_ALen) )
	{
		wchar_t* tmp = new wchar_t[ m_ALen = ( m_ALen>slen+len+1 ? m_ALen : slen+len+1) ];
		ki_memcpy( tmp, m_pBuf, WB(len) );
		ki_memcpy( tmp+len, s, WB(slen) );
		delete [] m_pBuf;
		m_pBuf = tmp;
		return *this;
	}

	ki_memcpy( m_pBuf+len, s, WB(slen) );
	return *this;
}

kiStr& kiStr::operator += ( wchar_t c )
{
	int  len = this->len();

	if( m_ALen < len+2 )
	{
		wchar_t* tmp = new wchar_t[ m_ALen=len+20 ];
		ki_memcpy( tmp, m_pBuf, WB(len) );
		delete [] m_pBuf;
		m_pBuf = tmp;
	}

	m_pBuf[len]=c, m_pBuf[len+1]=L'\0';
	return *this;
}

kiStr& kiStr::setInt( int n, bool cm )
{
	if( n==0 )
		m_pBuf[0] = L'0', m_pBuf[1] = L'\0';
	else
	{
		bool minus = (n<0);
		if( minus )
			n= -n;

		wchar_t tmp[30];
		tmp[29]=L'\0';
		int i;

		for( i=28; i>=0; i-- )
		{
			if( cm && (29-i)%4==0 )
				tmp[i--] = L',';
			tmp[i] = L'0' + n%10;
			n /= 10;
			if( n==0 )
				break;
		}

		if( minus )
			tmp[--i] = L'-';

		(*this) = tmp+i;
	}
	return (*this);
}

//-------------------------- General string processing ------------------------//


kiStr::~kiStr()
{
	delete [] m_pBuf;
}

kiStr::operator const wchar_t*() const
{
	return m_pBuf;
}

bool kiStr::operator == ( const wchar_t* s ) const
{
	return 0==ki_strcmp( m_pBuf, s );
}

bool kiStr::equalsIgnoreCase( const wchar_t* s ) const
{
	return 0==ki_strcmpi( m_pBuf, s );
}

int kiStr::len() const
{
	return ki_strlen( m_pBuf );
}


//-------------------------- Utilities ------------------------//


kiStr& kiStr::removeTrailWS()
{
	wchar_t* m=m_pBuf-1;
	for( wchar_t *p=m_pBuf; *p!=L'\0'; p=next(p) )
		if( *p!=L' ' && *p!=L'\t' && *p!=L'\n' )
			m = p;
	*next(m) = L'\0';
	return *this;
}

kiStr& kiStr::loadRsrc( UINT id )
{
	::LoadStringW( GetModuleHandle(NULL), id, m_pBuf, m_ALen );
	return *this;
}

void kiPath::beSpecialPath( int nPATH )
{
	switch( nPATH )
	{
	case Win:	::GetWindowsDirectoryW( m_pBuf, m_ALen );	break;
	case Sys:	::GetSystemDirectoryW( m_pBuf, m_ALen );	break;
	case Tmp:	::GetTempPathW( m_ALen, m_pBuf );			break;
	case Cur:	::GetCurrentDirectoryW( m_ALen, m_pBuf );	break;
	case Exe_name:
				::GetModuleFileNameW( NULL, m_pBuf, m_ALen );break;
	case Exe:
		{
			::GetModuleFileNameW( NULL, m_pBuf, m_ALen );

			wchar_t* m=NULL;
			for( wchar_t *p=m_pBuf; *p!=L'\0'; p=next(p) )
				if( *p==L'\\' )
					m = p;
			if( m )
				*m=L'\0';
			break;
		}
	default:
		{
			*m_pBuf = L'\0';

			LPITEMIDLIST il;
			if( NOERROR!=::SHGetSpecialFolderLocation( NULL, nPATH, &il ) )
				return;
			::SHGetPathFromIDListW( il, m_pBuf );
			app()->shellFree( il );
		}
	}
}

void kiPath::beBackSlash( bool add )
{
	wchar_t* last = m_pBuf;
	for( wchar_t* p=m_pBuf; *p!=L'\0'; p=next(p) )
		last=p;
	if( *last==L'\\' || *last==L'/' )
	{
		if( !add )
			*last = L'\0';
	}
	else if( add && last!=m_pBuf )
		*this += L'\\';
}

bool kiPath::beDirOnly()
{
	wchar_t* lastslash = m_pBuf-1;
	for( wchar_t* p=m_pBuf; *p; p=next(p) )
		if( *p==L'\\' || *p==L'/' )
			lastslash = p;

	*(lastslash+1) = L'\0';

	return (lastslash+1 != m_pBuf);
}

bool kiPath::isInSameDir(const wchar_t* q) const
{
	bool diff=false;
	const wchar_t* p = m_pBuf;
	for( ; *p && *q; p=next(p), q=next(q) )
		if( *p != *q )
			diff = true;
		else if( diff && (*p==L'\\' || *p==L'/' || *q==L'\\' || *q==L'/') )
			return false;

	const wchar_t* r = (*p ? p : q);
	if( *r )
		for( ; *r; r=next(r) )
			if( *r==L'\\' || *r==L'/' )
				return false;
	return true;
}

void kiPath::beShortPath()
{
	::GetShortPathNameW( m_pBuf, m_pBuf, m_ALen );
}

void kiPath::mkdir()
{
	for( wchar_t *p=m_pBuf; *p; p=kiStr::next(p) )
	{
		if( (*p!=L'\\' && *p!=L'/') || (p-m_pBuf<=4) )
			continue;
		*p = L'\0';
		if( !kiSUtil::exist(m_pBuf) )
			if( ::CreateDirectoryW( m_pBuf, NULL ) )
				::SHChangeNotify( SHCNE_MKDIR,SHCNF_PATHW,(const void*)m_pBuf,NULL );
		*p = L'\\';
	}
}

void kiPath::remove()
{
	if( !kiSUtil::exist(*this) )
		return;
	if( !kiSUtil::isdir(*this) )
	{
		::DeleteFileW(*this);
		return;
	}

	// buf == filename with no last '\\'
	kiPath buf(*this);
	buf.beBackSlash(false);

	kiPath tmp(buf);
	WIN32_FIND_DATAW fd;
	kiFindFile find;
	find.begin( tmp += L"\\*" );
	while( find.next( &fd ) )
	{
		tmp = buf;
		tmp += L'\\';
		tmp += fd.cFileName;
		tmp.remove();
	}
	find.close();

	::RemoveDirectoryW( buf );
}

void kiPath::getBody( kiStr& str ) const
{
	wchar_t *p=const_cast<wchar_t*>(name()),*x,c;
	for( x=(*p==L'.'?p+1:p); *x; x=next(x) ) // Leading '.' is not treated as extension
		if( *x==L'.' )
			break;
	c=*x, *x=L'\0';
	str=p;
	*x=c;
}

void kiPath::getBody_all( kiStr& str ) const
{
// Strip only the last extension
	wchar_t *p=const_cast<wchar_t*>(name()),*x=NULL, *n, c;
	for( n=(*p==L'.'?p+1:p); *n; n=next(n) ) // Leading '.' is not treated as extension
		if( *n==L'.' )
			x = n;
	if( !x )x = n;

	c  =*x;
	*x =L'\0';
	str=p;
	*x =c;
}

const wchar_t* kiPath::ext( const wchar_t* str )
{
	const wchar_t *ans = NULL, *p = name(str);
	if( *p == L'.' ) ++p; // Leading '.' is not treated as extension
	for( ; *p; p=next(p) )
		if( *p==L'.' )
			ans = p;
	return ans ? (ans+1) : p;
}

const wchar_t* kiPath::ext_all( const wchar_t* str )
{
	const wchar_t* p = name(str);
	if( *p == L'.' ) ++p; // Leading '.' is not treated as extension
	for( ; *p; p=next(p) )
		if( *p==L'.' )
			return (p+1);
	return p;
}

const wchar_t* kiPath::name( const wchar_t* str )
{
	const wchar_t* ans = str - 1;
	for( const wchar_t* p=str; *p; p=next(p) )
		if( *p==L'\\' || *p==L'/' )
			ans = p;
	return (ans+1);
}

UINT kiPath::getDriveType() const
{
	wchar_t* p;
	for( p=m_pBuf; *p==L'\\'; p=next(p) );
	for( p=m_pBuf; *p && *p!=L'\\'; p=next(p) );
	wchar_t c=*(++p);*p=L'\0';
	UINT ans=::GetDriveTypeW( m_pBuf );
	*p=c; return ans;
}
