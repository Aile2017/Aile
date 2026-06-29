// Archiver.cpp
//-- CArchiver -- common interface in 'Noah' for archiving routine --

#include "stdafx.h"
#include <string>
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


CArcModule::CArcModule( const wchar_t* name )
{
	wchar_t prev_cur[MAX_PATH];
	::GetCurrentDirectoryW(MAX_PATH, prev_cur);
	kiSUtil::switchCurDirToExeDir();

	// Search order: (1) exe dir, (2) exe\bin\, (3) system PATH.
	kiPath exeDir( kiPath::Exe );
	kiPath binDir( kiPath::Exe ); binDir += L"bin\\";

	if( 0!=::SearchPathW( exeDir,name,NULL,MAX_PATH,m_name,NULL ) ||
		0!=::SearchPathW( binDir,name,NULL,MAX_PATH,m_name,NULL ) ||
		0!=::SearchPathW( NULL,  name,NULL,MAX_PATH,m_name,NULL ) )
	{
		m_type = EXE;
	}
	else
	{
		m_type = NOTEXIST;

		ki_strcpy( m_name, name );
		// A name with a file extension (e.g. "WinRAR.exe") is expected to be an
		// on-disk executable; not finding it means it is absent.  A name without
		// an extension (e.g. "copy") may be a shell built-in, so keep SHLCMD.
		const wchar_t* dot = ::wcsrchr(name, L'.');
		const wchar_t* sep = ::wcspbrk(name, L"/\\");
		m_type = (dot && (!sep || dot > sep)) ? NOTEXIST : SHLCMD;
	}

	::SetCurrentDirectoryW(prev_cur);
}


int CArcModule::cmd( const wchar_t* cmd, bool mini )
{
	// Build shell command prefix
	static const wchar_t* const closeShell = L"cmd.exe /c ";

	// Build command string
	kiVar theCmd( m_name );
	theCmd.quote();
	theCmd += L' ';
	theCmd += cmd;

	if( m_type==SHLCMD )
	{
		// Shell command case
		theCmd = closeShell + theCmd;
	}

	// Start process
	PROCESS_INFORMATION pi;
	STARTUPINFOW si={sizeof(STARTUPINFOW)};
	si.dwFlags    =STARTF_USESHOWWINDOW;
	// Shell built-ins (copy, del, etc.) run non-interactively; hide the cmd.exe window.
	si.wShowWindow=(m_type==SHLCMD)?SW_HIDE:(mini?SW_MINIMIZE:SW_SHOW);
	if( !::CreateProcessW( NULL,const_cast<wchar_t*>((const wchar_t*)theCmd),
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
	wchar_t *verstr=L"----", buf[200];
	if( m_type != NOTEXIST )
	{
		// Try to get from resource if possible
		if( CArchiver::GetVersionInfoStr( m_name, buf, _countof(buf) ) )
			verstr = buf;
		else
			verstr = L"OK!";
	}

	wchar_t ans[300];
	::wsprintfW( ans, L"%-12s %s", kiPath::name(m_name), verstr );
	str = ans;
}


bool CArcModule::lst_exe( const wchar_t* lstcmd, aflArray& files,
	const wchar_t* BL, int BSL, const wchar_t* EL, int SL, int dx )
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
	theCmd += L' ';
	theCmd += lstcmd;

	// Create pipe (both inherit. Too much hassle to DupHandle...)
	HANDLE rp, wp;
	SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES),NULL,TRUE};
	::CreatePipe( &rp, &wp, &sa, 65536 );

	// Start process
	PROCESS_INFORMATION pi;
	STARTUPINFOW si = {sizeof(STARTUPINFOW)};
	si.dwFlags     = STARTF_USESHOWWINDOW|STARTF_USESTDHANDLES;
	si.wShowWindow = SW_MINIMIZE;
	si.hStdOutput  = si.hStdError = wp;
	BOOL ok =
		::CreateProcessW( NULL,const_cast<wchar_t*>((const wchar_t*)theCmd),NULL,
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

	// Drain the whole stdout (the tool emits raw bytes in the console/ANSI code
	// page).  Reading it all up front lets us decode once and parse in wide.
	std::string bytes;
	{
		char rbuf[8192];
		bool endpr = false;
		for(;;)
		{
			if( !endpr )
			{
				endpr = (WAIT_OBJECT_0==::WaitForSingleObject(pi.hProcess,50));
				pump_thread_messages();
			}
			DWORD red=0;
			::PeekNamedPipe( rp, NULL, 0, NULL, &red, NULL );
			if( red==0 )
			{
				if( endpr ) break;
				continue;
			}
			DWORD got=0;
			if( ::ReadFile( rp, rbuf, sizeof(rbuf), &got, NULL ) && got>0 )
				bytes.append( rbuf, got );
			else if( endpr )
				break;
		}
	}
	int ex = -1;
	::GetExitCodeProcess( pi.hProcess, (DWORD*)&ex );
	::CloseHandle( pi.hProcess );
	::CloseHandle( rp );

	// Decode to UTF-16.  The 7z-family .b2e list commands now pass -sccUTF-8, so
	// the console output is UTF-8 and non-ANSI names survive the round-trip.
	std::wstring wtext;
	if( !bytes.empty() )
	{
		int need = ::MultiByteToWideChar( CP_UTF8, 0, bytes.data(), (int)bytes.size(), NULL, 0 );
		if( need>0 )
		{
			wtext.resize( need );
			::MultiByteToWideChar( CP_UTF8, 0, bytes.data(), (int)bytes.size(), &wtext[0], need );
		}
	}

	// Parse line by line over the full wide buffer.
	const wchar_t* buf = wtext.c_str();
	const wchar_t* end = buf + wtext.size();
	wchar_t header_line[256] = L"";  // last non-empty pre-separator line (column header)
	bool done = false;
	for( const wchar_t* le=buf; !done && le<end; )
	{
		// Find end of line; ls..le is one line, le points at '\n' (or end).
		const wchar_t* ls = le;
		while( le<end && *le!=L'\n' )
			++le;
		if( le==end )
			break;                  // incomplete trailing line: drop (matches old behaviour)
		const wchar_t* nl = le;     // points at '\n'
		++le;                       // advance past '\n' for next iteration

		// Skip header line processing
		if( *BL )
		{
			if( BLLEN<=nl-ls && ki_memcmp(BL,ls,BLLEN) )
			{
				// Separator found: store header_line as the first arcfile entry
				arcfile hdr; ki_memzero(&hdr, sizeof(hdr));
				hdr.isfile = false;
				ki_strcpy( hdr.rawline, header_line );
				files.add( hdr );
				BL = L"";
			}
			else if( nl-ls > 1 )
			{
				// Non-empty, non-separator line: keep as candidate column header
				int rn = 0;
				const wchar_t* p = ls;
				while( rn < _countof(header_line)-1 && p < nl && *p != L'\r' && *p != L'\n' )
					header_line[rn++] = *p++;
				header_line[rn] = L'\0';
			}
		}
		// Line step processing
		else if( --step<=0 )
		{
			step = SL;

			// End-of-data line processing
			if( ELLEN==0 )
				{ if( nl-ls<=1 ) { done=true; break; } }
			else if( ELLEN<=nl-ls && ki_memcmp(EL,ls,ELLEN) )
				{ done=true; break; }

			// Character skip processing
			const wchar_t* ls_raw = ls;
			if( dx>=0 )
				ls += dx;
			// Argument block skip processing
			else
			{
				for( ;ls<nl;++ls )
					if( *ls!=L' ' && *ls!=L'\t' && *ls!=L'\r' )
						break;
				for( int t=dx; ++t; )
				{
					for( ;ls<nl;++ls )
						if( *ls==L' ' || *ls==L'\t' || *ls==L'\r' )
							break;
					for( ;ls<nl;++ls )
						if( *ls!=L' ' && *ls!=L'\t' && *ls!=L'\r' )
							break;
				}
			}
			// Copy filename
			if( ls<nl )
			{
				arcfile af; ki_memzero(&af, sizeof(af));
				af.inf.dwOriginalSize = 0xffffffff;

				// Raw display line (full line before dx skip)
				{
					int rn = 0;
					const wchar_t* p = ls_raw;
					while( rn < _countof(af.rawline)-1 && p < nl && *p != L'\r' && *p != L'\n' )
						af.rawline[rn++] = *p++;
					af.rawline[rn] = L'\0';
				}

				int i=0;
				bool prev_is_space=false;
				while( i<FNAME_MAX32 && ls<nl )
				{
					if( *ls==L' ' )
					{
						if( prev_is_space )
							break;
						prev_is_space = true;
					}
					else if( *ls==L'\t' || *ls==L'\r' )
						break;
					else
						prev_is_space = false;

					af.inf.szFileName[i++] = *ls++;
				}
				if( prev_is_space )
					--i;
				if( i )
				{
					af.inf.szFileName[i] = L'\0';
					af.isfile = true;
					files.add(af);
				}
			}
		}
	}

	return (ex == 0 || files.len() > 0);
}

int CArcModule::tst_exe( const wchar_t* tstcmd, kiStr& output )
{
	output = L"";

	if( m_type != EXE )
		return -1;

	// Build command string
	kiVar theCmd( m_name );
	theCmd.quote();
	theCmd += L' ';
	theCmd += tstcmd;

	// Create pipe
	HANDLE rp, wp;
	SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
	::CreatePipe( &rp, &wp, &sa, 65536 );

	// Start process (hidden — output captured via pipe)
	PROCESS_INFORMATION pi;
	STARTUPINFOW si = {sizeof(STARTUPINFOW)};
	si.dwFlags     = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
	si.wShowWindow = SW_HIDE;
	si.hStdOutput  = si.hStdError = wp;
	BOOL ok = ::CreateProcessW( NULL, const_cast<wchar_t*>((const wchar_t*)theCmd),
		NULL, NULL, TRUE, CREATE_NEW_PROCESS_GROUP | NORMAL_PRIORITY_CLASS,
		NULL, NULL, &si, &pi );
	::CloseHandle( wp );

	if( !ok ) {
		::CloseHandle( rp );
		return -1;
	}
	::CloseHandle( pi.hThread );

	// Drain all stdout/stderr bytes, then decode once.
	std::string bytes;
	{
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
			DWORD red = 0;
			if( ::ReadFile( rp, buf, sizeof(buf), &red, NULL ) && red > 0 )
				bytes.append( buf, red );
			else if( endpr )
				break;
		}
	}

	// Decode to UTF-16 (CP_UTF8; see lst_exe — tools emit UTF-8 via -sccUTF-8).
	if( !bytes.empty() )
	{
		int need = ::MultiByteToWideChar( CP_UTF8, 0, bytes.data(), (int)bytes.size(), NULL, 0 );
		if( need>0 )
		{
			std::wstring w; w.resize( need );
			::MultiByteToWideChar( CP_UTF8, 0, bytes.data(), (int)bytes.size(), &w[0], need );
			output += w.c_str();
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

bool CArchiver::GetVersionInfoStr( wchar_t* name, wchar_t* buf, size_t cbBuf )
{
	DWORD dummy = 0;
	DWORD siz = ::GetFileVersionInfoSizeW( name, &dummy );
	if( siz == 0 )
		return false;

	bool got = false;
	BYTE* vbuf = new BYTE[siz];
	if( 0 != ::GetFileVersionInfoW( name, 0, siz, vbuf ) )
	{
		WORD* tr = NULL;
		UINT cbTr = 0;

		// Get info using the first found language and code page
		if( ::VerQueryValueW( vbuf,
			L"\\VarFileInfo\\Translation", (void**)&tr, &cbTr )
		 && cbTr >= 4 )
		{
			wchar_t blockname[500]=L"";
			::wsprintfW( blockname,
				L"\\StringFileInfo\\%04x%04x\\FileVersion",
				tr[0], tr[1] );

			wchar_t* inf = NULL;
			UINT cbInf = 0;
			if( ::VerQueryValueW( vbuf, blockname, (void**)&inf, &cbInf )
			 && cbInf < cbBuf-1 )
			{
				wchar_t* v = buf;
				for( ; *inf && cbInf; ++inf,--cbInf )
					*v++ = (*inf==L',' ? L'.' : *inf);
				*v = L'\0';
				got = true;
			}
		}
		else
		{
			void* fi = NULL;
			UINT cbFi = 0;
			VS_FIXEDFILEINFO vffi;
			if( ::VerQueryValueW( vbuf, L"\\", &fi, &cbFi )
			 && sizeof(vffi)<=cbFi )
			{
				ki_memcpy( &vffi, fi, sizeof(vffi) );
				if( vffi.dwFileVersionLS >= 0x10000 )
					::wsprintfW( buf, L"%d.%d.%d", vffi.dwFileVersionMS>>16,
						vffi.dwFileVersionMS&0xffff, vffi.dwFileVersionLS>>16 );
				else
					::wsprintfW( buf, L"%d.%d", vffi.dwFileVersionMS>>16,
						vffi.dwFileVersionMS&0xffff );
				got = true;
			}
		}
	}

	delete [] vbuf;
	return got;
}
