// Archiver.cpp
//-- CArchiver -- common interface in 'Noah' for archiving routine --

#include "stdafx.h"
#include "Archiver.h"
#include "AileFlowApp.h"


// Drain this thread's message queue so the UI stays responsive while we block
// waiting on a child process.  Replaces the former kiWindow::msg() now that the
// kilib windowing framework has been removed (AileFlow uses the common UI layer).
static void pump_thread_messages()
{
	for( MSG m; ::PeekMessage( &m, NULL, 0, 0, PM_REMOVE ); )
		::TranslateMessage( &m ), ::DispatchMessage( &m );
}


CArcModule::CArcModule( const char* name )
{
	char prev_cur[MAX_PATH];
	::GetCurrentDirectory(MAX_PATH, prev_cur);
	kiSUtil::switchCurDirToExeDir();

	// Search order: (1) exe dir, (2) exe\bin\, (3) system PATH.
	kiPath exeDir( kiPath::Exe );
	kiPath binDir( kiPath::Exe ); binDir += "bin\\";

	if( 0!=::SearchPath( exeDir,name,NULL,MAX_PATH,m_name,NULL ) ||
		0!=::SearchPath( binDir,name,NULL,MAX_PATH,m_name,NULL ) ||
		0!=::SearchPath( NULL,  name,NULL,MAX_PATH,m_name,NULL ) )
	{
		m_type = EXE;
	}
	else
	{
		ki_strcpy( m_name, name );
		// A name with a file extension (e.g. "WinRAR.exe") is expected to be an
		// on-disk executable; not finding it means it is absent.  A name without
		// an extension (e.g. "copy") may be a shell built-in, so keep SHLCMD.
		const char* dot = ::strrchr(name, '.');
		const char* sep = ::strpbrk(name, "/\\");
		m_type = (dot && (!sep || dot > sep)) ? NOTEXIST : SHLCMD;
	}

	::SetCurrentDirectory(prev_cur);
}


int CArcModule::cmd( const char* cmd, bool mini )
{
	// Build shell command prefix
	static const char* const closeShell = "cmd.exe /c ";

	// Build command string
	kiVar theCmd( m_name );
	theCmd.quote();
	theCmd += ' ';
	theCmd += cmd;

	if( m_type==SHLCMD )
	{
		// Shell command case
		theCmd = closeShell + theCmd;
	}

	// Start process
	PROCESS_INFORMATION pi;
	STARTUPINFO si={sizeof(STARTUPINFO)};
	si.dwFlags    =STARTF_USESHOWWINDOW;
	// Shell built-ins (copy, del, etc.) run non-interactively; hide the cmd.exe window.
	si.wShowWindow=(m_type==SHLCMD)?SW_HIDE:(mini?SW_MINIMIZE:SW_SHOW);
	if( !::CreateProcess( NULL,const_cast<char*>((const char*)theCmd),
		NULL,NULL,FALSE,CREATE_NEW_PROCESS_GROUP|NORMAL_PRIORITY_CLASS,
		NULL,NULL, &si,&pi ) )
		return 0xffff;

	// Grant the child permission to bring its own dialogs to the foreground
	// (e.g. 7zG.exe's password prompt).  Without this, Windows' foreground
	// lock keeps Noah's windows on top and the child's dialogs stay behind.
	::AllowSetForegroundWindow( pi.dwProcessId );

	// Wait for exit
	::CloseHandle( pi.hThread );
	while( WAIT_OBJECT_0 != ::WaitForSingleObject( pi.hProcess, 500 ) )
		pump_thread_messages();
	int ex;
	::GetExitCodeProcess( pi.hProcess, (DWORD*)&ex );
	::CloseHandle( pi.hProcess );

	// Cleanup
	return ex;
}

void CArcModule::ver( kiStr& str )
{
	// Format and display version info
	char *verstr="----", buf[200];
	if( m_type != NOTEXIST )
	{
		// Try to get from resource if possible
		if( CArchiver::GetVersionInfoStr( m_name, buf, sizeof(buf) ) )
			verstr = buf;
		else
			verstr = "OK!";
	}

	char ans[300];
	::wsprintf( ans, "%-12s %s", kiPath::name(m_name), verstr );
	str = ans;
}


bool CArcModule::lst_exe( const char* lstcmd, aflArray& files,
	const char* BL, int BSL, const char* EL, int SL, int dx )
	// BeginLine, BeginSkipLine, EndLine, SkipLine, delta-x
{
	files.forcelen(0);

	// Working variables
	const int BLLEN = ki_strlen(BL);
	const int ELLEN = ki_strlen(EL);
	int /*ct=0,*/ step=BSL;

	// Non-EXE types are not supported here
	if( m_type!=EXE )
		return false;

	// Build command string
	kiVar theCmd( m_name );
	theCmd.quote();
	theCmd += ' ';
	theCmd += lstcmd;

	// Create pipe (both inherit. Too much hassle to DupHandle...)
	HANDLE rp, wp;
	SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES),NULL,TRUE};
	::CreatePipe( &rp, &wp, &sa, 65536 );

	// Start process
	PROCESS_INFORMATION pi;
	STARTUPINFO si = {sizeof(STARTUPINFO)};
	si.dwFlags     = STARTF_USESHOWWINDOW|STARTF_USESTDHANDLES;
	si.wShowWindow = SW_MINIMIZE;
	si.hStdOutput  = si.hStdError = wp;
	BOOL ok = 
		::CreateProcess( NULL,const_cast<char*>((const char*)theCmd),NULL,
			NULL, TRUE, CREATE_NEW_PROCESS_GROUP|NORMAL_PRIORITY_CLASS,
			NULL, NULL, &si,&pi );
	::CloseHandle( wp );

	// On failure, close pipe and exit immediately
	if( !ok )
	{
		::CloseHandle( rp );
		return false;
	}
	::CloseHandle( pi.hThread );

	// Parsing etc.
	char buf[8192], *end=buf;
	char header_line[256] = "";  // last non-empty pre-separator line (used as column header)
	bool endpr = false;
	for(;;)
	{
		// Wait (only while process is still running)
		if( !endpr )
		{
			endpr = (WAIT_OBJECT_0==::WaitForSingleObject(pi.hProcess,50));
			pump_thread_messages();
		}

		// Read from pipe; exit only when process has exited AND pipe is fully drained
		DWORD red;
		::PeekNamedPipe( rp, NULL, 0, NULL, &red, NULL );
		if( red==0 )
		{
			if( endpr ) break;
			continue;
		}
		const DWORD cbAvail = static_cast<DWORD>((buf+sizeof(buf))-end);
		::ReadFile( rp, end, cbAvail, &red, NULL );
		end += red;

		// Split into lines
		char *lss=buf;
		for( char *ls, *le=buf; le<end; ++le )
		{
			// Find end of line
			for( lss=ls=le; le<end; ++le )
				if( *le=='\n' )
					break;
			if( le==end )
				break;

			// Skip header line processing
			if( *BL )
			{
				if( BLLEN<=le-ls && ki_memcmp(BL,ls,BLLEN) )
				{
					// Separator found: store header_line as the first arcfile entry
					arcfile hdr; ki_memzero(&hdr, sizeof(hdr));
					hdr.isfile = false;
					ki_strcpy( hdr.rawline, header_line );
					files.add( hdr );
					BL = "";
				}
				else if( le-ls > 1 )
				{
					// Non-empty, non-separator line: keep as candidate column header
					int rn = 0;
					const char* p = ls;
					while( rn < (int)sizeof(header_line)-1 && p < le && *p != '\r' && *p != '\n' )
						header_line[rn++] = *p++;
					header_line[rn] = '\0';
				}
			}
			// Line step processing
			else if( --step<=0 )
			{
				step = SL;

				// End-of-data line processing
				if( ELLEN==0 )
					{ if( le-ls<=1 ) break; }
				else if( ELLEN<=le-ls && ki_memcmp(EL,ls,ELLEN) )
					break;

				// Character skip processing
				const char* ls_raw = ls;
				if( dx>=0 )
					ls += dx;
				// Argument block skip processing
				else
				{
					for( ;ls<le;++ls )
						if( *ls!=' ' && *ls!='\t' && *ls!='\r' )
							break;
					for( int t=dx; ++t; )
					{
						for( ;ls<le;++ls )
							if( *ls==' ' || *ls=='\t' || *ls=='\r' )
								break;
						for( ;ls<le;++ls )
							if( *ls!=' ' && *ls!='\t' && *ls!='\r' )
								break;
					}
				}
				// Copy filename
				if( ls<le )
				{
					arcfile af; ki_memzero(&af, sizeof(af));
					af.inf.dwOriginalSize = 0xffffffff;

					// Raw display line (full line before dx skip)
					{
						int rn = 0;
						const char* p = ls_raw;
						while( rn < (int)sizeof(af.rawline)-1 && p < le && *p != '\r' && *p != '\n' )
							af.rawline[rn++] = *p++;
						af.rawline[rn] = '\0';
					}

					int i=0;
					bool prev_is_space=false;
					while( i<FNAME_MAX32 && ls<le )
					{
						if( *ls==' ' )
						{
							if( prev_is_space )
								break;
							prev_is_space = true;
						}
						else if( *ls=='\t' || *ls=='\r' )
							break;
						else
							prev_is_space = false;

						af.inf.szFileName[i++] = *ls++;
					}
					if( prev_is_space )
						--i;
					if( i )
					{
						af.inf.szFileName[i] = '\0';
						af.isfile = true;
						files.add(af);
					}
				}
			}
		}
		// Buffer shift
		if( lss != buf )
			ki_memmov( buf, lss, end-lss ), end=buf+(end-lss);
		else if( end==buf+sizeof(buf) )
			end = buf;
	}

	// Done
	::CloseHandle( pi.hProcess );
	::CloseHandle( rp );
	return true;
}

int CArcModule::tst_exe( const char* tstcmd, kiStr& output )
{
	output = "";

	if( m_type != EXE )
		return -1;

	// Build command string
	kiVar theCmd( m_name );
	theCmd.quote();
	theCmd += ' ';
	theCmd += tstcmd;

	// Create pipe
	HANDLE rp, wp;
	SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
	::CreatePipe( &rp, &wp, &sa, 65536 );

	// Start process (hidden — output captured via pipe)
	PROCESS_INFORMATION pi;
	STARTUPINFO si = {sizeof(STARTUPINFO)};
	si.dwFlags     = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
	si.wShowWindow = SW_HIDE;
	si.hStdOutput  = si.hStdError = wp;
	BOOL ok = ::CreateProcess( NULL, const_cast<char*>((const char*)theCmd),
		NULL, NULL, TRUE, CREATE_NEW_PROCESS_GROUP | NORMAL_PRIORITY_CLASS,
		NULL, NULL, &si, &pi );
	::CloseHandle( wp );

	if( !ok ) {
		::CloseHandle( rp );
		return -1;
	}
	::CloseHandle( pi.hThread );

	// Read all stdout/stderr into output
	char buf[4096];
	bool endpr = false;
	for(;;) {
		if( !endpr ) {
			endpr = (WAIT_OBJECT_0 == ::WaitForSingleObject( pi.hProcess, 50 ));
			pump_thread_messages();
		}
		DWORD avail = 0;
		::PeekNamedPipe( rp, NULL, 0, NULL, &avail, NULL );
		if( avail == 0 ) {
			if( endpr ) break;
			continue;
		}
		if( avail > sizeof(buf) ) avail = sizeof(buf);
		DWORD red = 0;
		::ReadFile( rp, buf, avail, &red, NULL );
		if( red > 0 ) {
			char tmp[4097];
			ki_memcpy( tmp, buf, red );
			tmp[red] = '\0';
			output += tmp;
		}
	}

	int ex = -1;
	::GetExitCodeProcess( pi.hProcess, (DWORD*)&ex );
	::CloseHandle( pi.hProcess );
	::CloseHandle( rp );
	return ex;
}

/*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*/
// Get version info resource

bool CArchiver::GetVersionInfoStr( char* name, char* buf, size_t cbBuf )
{
	DWORD dummy = 0;
	DWORD siz = ::GetFileVersionInfoSize( name, &dummy );
	if( siz == 0 )
		return false;

	bool got = false;
	BYTE* vbuf = new BYTE[siz];
	if( 0 != ::GetFileVersionInfo( name, 0, siz, vbuf ) )
	{
		WORD* tr = NULL;
		UINT cbTr = 0;

		// Get info using the first found language and code page
		if( ::VerQueryValue( vbuf,
			"\\VarFileInfo\\Translation", (void**)&tr, &cbTr )
		 && cbTr >= 4 )
		{
			char blockname[500]="";
			::wsprintf( blockname,
				"\\StringFileInfo\\%04x%04x\\ProductVersion",
				tr[0], tr[1] );

			char* inf = NULL;
			UINT cbInf = 0;
			if( ::VerQueryValue( vbuf, blockname, (void**)&inf, &cbInf )
			 && cbInf < cbBuf-1 )
			{
				char* v = buf;
				for( ; *inf && cbInf; ++inf,--cbInf )
					if( *inf != ' ' )
						*v++ = (*inf==',' ? '.' : *inf);
				*v = '\0';
				got = true;
			}
		}
		else
		{
			void* fi = NULL;
			UINT cbFi = 0;
			VS_FIXEDFILEINFO vffi;
			if( ::VerQueryValue( vbuf, "\\", &fi, &cbFi )
			 && sizeof(vffi)<=cbFi )
			{
				ki_memcpy( &vffi, fi, sizeof(vffi) );
				if( vffi.dwFileVersionLS >= 0x10000 )
					::wsprintf( buf, "%d.%d.%d", vffi.dwFileVersionMS>>16,
						vffi.dwFileVersionMS&0xffff, vffi.dwFileVersionLS>>16 );
				else
					::wsprintf( buf, "%d.%d", vffi.dwFileVersionMS>>16,
						vffi.dwFileVersionMS&0xffff );
				got = true;
			}
		}
	}

	delete [] vbuf;
	return got;
}
