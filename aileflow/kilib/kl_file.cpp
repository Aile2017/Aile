//--- K.I.LIB ---
// kl_file.cpp : file operations

#include "stdafx.h"
#include "kilib.h"

//--------------------------- static --------------------------//

unsigned long kiFile::getSize( const char* fname, unsigned long err )
{
	HANDLE h = ::CreateFile( fname,
				GENERIC_READ, FILE_SHARE_WRITE|FILE_SHARE_READ,
				NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL|FILE_FLAG_SEQUENTIAL_SCAN, NULL );
	if( h==INVALID_HANDLE_VALUE )
		return err;

	unsigned long ans = ::GetFileSize( h, NULL );
	::CloseHandle( h );

	return ans==0xffffffff ? err : ans;
}

//--------------------------- Open and close --------------------------//


bool kiFile::open( const char* filename, bool read, bool create )
{
	close();

	if( m_bReadMode = read )
		m_hFile = ::CreateFile( filename,
				GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL );
	else
		m_hFile = ::CreateFile( filename,
				GENERIC_WRITE, FILE_SHARE_READ, NULL,
				create ? CREATE_ALWAYS : OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL );
	if( m_hFile == INVALID_HANDLE_VALUE )
		return false;

	m_nBufPos = 0;
	if( m_bReadMode )
		flush();

	return true;
}

void kiFile::close()
{
	if( m_hFile != INVALID_HANDLE_VALUE )
	{
		if( !m_bReadMode )
			flush();

		::CloseHandle( m_hFile );
		m_hFile = INVALID_HANDLE_VALUE;
	}
}


//-------------------------- Read/Write ----------------------------//


unsigned long kiFile::read( unsigned char* buf, unsigned long len )
{
	unsigned long ans = 0;
	if( m_nBufSize!=0 && m_hFile!=INVALID_HANDLE_VALUE && m_bReadMode )
	{
		while( (m_nBufSize-m_nBufPos) <= len )
		{
			ans += (m_nBufSize-m_nBufPos);
			ki_memcpy( buf, m_pBuf+m_nBufPos, m_nBufSize-m_nBufPos );
			len -= (m_nBufSize-m_nBufPos);
			buf += (m_nBufSize-m_nBufPos);

			flush();
			if( m_nBufSize == 0 )
				return ans;
		}
		ans += len;
		ki_memcpy( buf, m_pBuf+m_nBufPos, len );
		m_nBufPos += len;
	}
	return ans;
}

void kiFile::write( const void* buf, unsigned long len )
{
	if( m_hFile==INVALID_HANDLE_VALUE || m_bReadMode )
		return;
	const unsigned char* ubuf = (const unsigned char*)buf;

	while( (kifile_bufsize-m_nBufPos) <= len )
	{
		ki_memcpy( m_pBuf+m_nBufPos, buf, (kifile_bufsize-m_nBufPos) );
		len -= (kifile_bufsize-m_nBufPos);
		ubuf += (kifile_bufsize-m_nBufPos);
		m_nBufPos = kifile_bufsize;
		flush();
	}
	ki_memcpy( m_pBuf+m_nBufPos, ubuf, len );
	m_nBufPos += len;
}

void kiFile::flush()
{
	if( m_bReadMode )
	{
		::ReadFile( m_hFile, m_pBuf, kifile_bufsize, &m_nBufSize, NULL );
		m_nBufPos = 0;
	}
	else
	{
		::WriteFile( m_hFile, m_pBuf, m_nBufPos, &m_nBufSize, NULL );
		m_nBufSize = m_nBufPos = 0;
	}
}
