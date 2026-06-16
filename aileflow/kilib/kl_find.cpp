//--- K.I.LIB ---
// kl_find.h : FindFirstFile wrapper (UTF-16)

#include "stdafx.h"
#include "kilib.h"

#define isDots(p) (*p==L'.' && (p[1]==L'\0' || (p[1]==L'.' && p[2]==L'\0')))

bool kiFindFile::findfirst( const wchar_t* wild, WIN32_FIND_DATAW* pfd )
{
	HANDLE xh = ::FindFirstFileW( wild, pfd );
	if( xh==INVALID_HANDLE_VALUE )
		return false;
	while( isDots(pfd->cFileName) )
		if( !::FindNextFileW( xh, pfd ) )
		{
			::FindClose( xh );
			return false;
		}
	::FindClose( xh );
	return true;
}

void kiFindFile::close()
{
	first=true;
	if( h!=INVALID_HANDLE_VALUE )
	{
		::FindClose( h ), h=INVALID_HANDLE_VALUE;
	}
}

bool kiFindFile::begin( const wchar_t* wild )
{
	close();

	h = ::FindFirstFileW( wild, &fd );
	if( h==INVALID_HANDLE_VALUE )
		return false;
	while( isDots(fd.cFileName) )
		if( !::FindNextFileW( h, &fd ) )
		{
			close();
			return false;
		}
	return true;
}

bool kiFindFile::next( WIN32_FIND_DATAW* pfd )
{
	if( h==INVALID_HANDLE_VALUE )
		return false;
	if( first )
	{
		first = false;
		ki_memcpy( pfd, &fd, sizeof(fd) );
		return true;
	}
	if( !::FindNextFileW( h, pfd ) )
		return false;
	while( isDots(pfd->cFileName) )
		if( !::FindNextFileW( h, pfd ) )
			return false;
	return true;
}

#undef isDots
