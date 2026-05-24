//--- K.I.LIB ---
// kl_file.h : file operations

#ifndef AFX_KIFILE_H__7D126C1E_3E5C_476E_9A4E_81CA8055621D__INCLUDED_
#define AFX_KIFILE_H__7D126C1E_3E5C_476E_9A4E_81CA8055621D__INCLUDED_

/*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*/
// Binary file operations

class kiFile
{
public: //-- static ----------------------------------------

	// Get file size (name, value to return on error)
	static unsigned long getSize( const char* fname, unsigned long err=0xffffffff );

public: //-- Public interface --------------------------

	// Open and close
	bool open( const char* filename, bool read=true, bool create=true );
	void close();

	// Read and write
	unsigned long read( unsigned char* buf, unsigned long len );
	void write( const void* buf, unsigned long len );

	// Get information
	unsigned long getSize( unsigned long* higher=NULL )
		{
			return ::GetFileSize( m_hFile, higher );
		}

public: //-- Internal processing -----------------------------------

	kiFile() : kifile_bufsize( 65536 )
		{
			m_hFile= INVALID_HANDLE_VALUE;
			m_pBuf = new unsigned char[kifile_bufsize];
		}

	virtual ~kiFile()
		{
			close();
			delete [] m_pBuf;
		}

private:
	const int kifile_bufsize;
	void flush();

	HANDLE m_hFile;
	bool   m_bReadMode;
	unsigned char* m_pBuf;
	unsigned long  m_nBufSize, m_nBufPos;
};

#endif
