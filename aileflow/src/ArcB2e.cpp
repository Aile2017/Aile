
#include "stdafx.h"
#include "ArcB2e.h"
#include "B2eScript.h"
#include "resource.h"
#include "AileFlowApp.h"
#include <vector>
#include <string>

//----------------- ArcB2e class general processing ------------------------------

wchar_t CArcB2e::st_base[MAX_PATH];
int  CArcB2e::st_life=0;
CArcB2e::CB2eCore* CArcB2e::rvm=NULL;
static HWND s_dialogParentHwnd = NULL;
void CArcB2e::SetDialogParent(HWND hwnd) { s_dialogParentHwnd = hwnd; }

const wchar_t* CArcB2e::init_b2e_path()
{
	kiPath dir( kiPath::Exe );
	ki_strcpy( st_base, dir+=L"b2e\\" );
	return st_base;
}

CArcB2e::CArcB2e( const wchar_t* scriptname ) : CArchiver( scriptname )
{
	st_life++;
	exe = NULL;
	m_LstScr = m_DcEScr = m_EncScr =
	m_DecScr = m_SfxScr = m_LoadScr= m_ScriptBuf = NULL;
	m_TstScr = m_DelScr = NULL;
	m_psTstOutput = NULL;
}

CArcB2e::~CArcB2e()
{
	delete exe;
	exe = NULL;
	if( !(--st_life) ) {
		delete rvm;
		rvm = NULL;  // prevent dangling pointer when instances are created sequentially
	}
	delete [] m_ScriptBuf;
}

//------------------- Parts mostly independent of the script -------------------------

bool CArcB2e::v_ver( kiStr& str )
{
	if( !exe )
		return false;
	exe->ver( str );

	kiStr tmp;
	for( int i=0,e=m_subFile.len(); i<e; ++i )
	{
		str += L"\r\n";
		CArcModule(m_subFile[i]).ver( tmp );
		str += tmp;
	}
	return true;
}

//------------------- Load script & eval( load: ) -------------------

bool CArcB2e::load_module( const wchar_t* name )
{
	exe = new CArcModule( name );
	return exe->exist();
}

int CArcB2e::v_load()
{
	//-- Open extended script file
	kiStr fname( st_base ); fname += mlt_ext();
	std::vector<wchar_t> scriptBuf;
	if( B2e_LoadAndPreprocessScriptFile( fname, &scriptBuf ) )
	{
		m_ScriptBuf = new wchar_t[ scriptBuf.size() ];
		ki_memcpy( m_ScriptBuf, scriptBuf.data(), scriptBuf.size()*sizeof(wchar_t) );

		B2eSections sections;
		B2e_SplitSectionsInPlace( m_ScriptBuf, &sections );
		m_LoadScr = sections.load;
		m_EncScr = sections.encode;
		m_DecScr = sections.decode;
		m_SfxScr = sections.sfx;
		m_DcEScr = sections.decode1;
		m_LstScr = sections.list;
		m_TstScr = sections.test;
		m_DelScr = sections.del;
		m_SfxDirect = sections.sfxDirect;

		//-- Execute [load:]!
		if( m_LoadScr )
		{
			//-- Start RythpVM
			if( !rvm )
				rvm = new CB2eCore;

			//-- Initialize
			m_Result=0;
			rvm->setPtr( this,mLod );

			//-- Execute
			rvm->eval( m_LoadScr );

			//-- Result
			if( m_Result==0 )
				return (m_DecScr?aMelt|(m_DcEScr?aList|aMeltEach:0):0)
					 | (m_EncScr?aCompress|(sections.pack1?0:aArchive)|(m_SfxScr?aSfx:0):0)
					 | (m_TstScr?aTest:0)
					 | (m_DelScr?aDelete:0);
		}
	}
	return 0;
}

int CArcB2e::exec_script( const wchar_t* scr, scr_mode mode )
{
	//-- Initialize
	m_Result = 0;
	rvm->setPtr( this, mode );

	//-- Execute
	wchar_t* script = new wchar_t[ki_strlen(scr)+8];
	ki_strcpy( script, L"(exec " );
	ki_strcat( script, scr );
	ki_strcat( script, L")" );
	rvm->eval( script );
	delete [] script;

	//-- Result
	return m_Result;
}

//-------------------- List eval( list: ) -----------------------

bool CArcB2e::v_list( const arcname& aname, aflArray& files )
{
	//-- Do without script if possible.
	if( !exe )
		return false;
	else if( !m_LstScr )
		return false;

//-- Data required for the listing script

	// Archive name
	m_psArc = &aname;
	// File list
	m_psAInfo = &files;

//-- Execute! ---------------------

	return 0==exec_script( m_LstScr, mLst );
}

//-------------------- Extraction processing eval( decode: ) -----------------------

int CArcB2e::v_melt( const arcname& aname, const kiPath& ddir, const aflArray* files )
{
//-- Data required for extraction script

	// Set current directory
	::SetCurrentDirectory( ddir );
	// Archive name
	m_psArc = &aname;
	// Output directory
	m_psDir = &ddir;
	// File list
	m_psAInfo = files;

//-- Execute! ---------------------

	return exec_script( files ? m_DcEScr : m_DecScr,
						files ? mDc1     : mDec );
}

//-------------------- Integrity test eval( test: ) -----------------------

int CArcB2e::v_test( const arcname& aname, kiStr& output )
{
	m_psArc       = &aname;
	m_psTstOutput = &output;
	output        = L"";
	int result    = exec_script( m_TstScr, mTst );
	m_psTstOutput = NULL;
	return result;
}

//-------------------- Entry deletion eval( delete: ) -----------------------

int CArcB2e::v_delete( const arcname& aname, const aflArray& files )
{
	m_psArc   = &aname;
	m_psAInfo = &files;
	return exec_script( m_DelScr, mDel );
}

//-------------------- Compression processing eval( encode: sfx: ) -----------------------

int CArcB2e::cmpr( const wchar_t* scr, const kiPath& base, const wfdArray& files, const kiPath& ddir, const int method )
{
//-- Data required for the compression script

	arcname aname(
		ddir,
		files[0].cAlternateFileName,
		files[0].cFileName );
	int mhd=method+1;

	// Set current directory
	::SetCurrentDirectory( base );
	// Archive name
	m_psArc = &aname;
	// Base directory
	m_psDir = &base;
	// Method
	m_psMhd = &mhd;
	// List
	m_psList = &files;

//-- Execute! --------------------

	return exec_script( scr, mEnc );
}

bool CArcB2e::arc2sfx( const kiPath& temp, const kiPath& dest )
{
//-- Data required for the SFX conversion script

	kiFindFile f;
	WIN32_FIND_DATAW fd;
	kiPath wild( temp );
	f.begin( wild += L"*" );
	if( !f.next( &fd ) )
		return false;
	kiPath from, to, oldname( fd.cFileName );
	arcname aname( temp, fd.cAlternateFileName[0] ? fd.cAlternateFileName : fd.cFileName, fd.cFileName );

	// Set current directory
	::SetCurrentDirectory( temp );
	// Archive name
	m_psArc = &aname;
	// Directory
	m_psDir = &temp;

//-- Execute! ----------------------

	if( 0x8000<=exec_script( m_SfxScr, mSfx ) )
		return false;

//-- Copy ----------------------

	bool skipped=false, ans=false;
	f.begin( wild );
	while( f.next( &fd ) )
	{
		if( !skipped && oldname == fd.cFileName ) // Don't copy the temp archive.
		{
			skipped=true;
			continue;
		}
		from = temp, from += fd.cFileName;
		to   = dest, to   += fd.cFileName;
		if( ::CopyFile( from, to, FALSE ) )
			ans = true;
	}
	return ans;
}

int CArcB2e::v_compress( const kiPath& base, const wfdArray& files, const kiPath& ddir, int method, bool sfx )
{
	const wchar_t* theScript = m_EncScr;

	if( sfx )
	{
		if( m_SfxDirect )
			theScript = m_SfxScr;
		else
		{
			kiPath tmp;
			myapp().get_tempdir( tmp );

			// Compress to temp
			int ans = cmpr( m_EncScr, base, files, tmp, method );
			if( ans < 0x8000 )
				// Convert temp files to SFX & copy!
				ans = (arc2sfx( tmp, ddir ) ? 0 : 0x8020);

			// Cannot delete without restoring current directory...(;_;)
			::SetCurrentDirectory( base );
			tmp.remove();
			return ans;
		}
	}

	// Compress normally to destination
	return cmpr( theScript, base, files, ddir, method );
}

//-----------------------------------------------------------------//
//-------------------- RythpVM implementation --------------------------//
//-----------------------------------------------------------------//

bool CArcB2e::CB2eCore::exec_function( const kiVar& name, const CharArray& a, const BoolArray& b, int c, kiVar* r )
{
	bool processed = false;

	if( m_mode==mLod ){ //**Load-time-only functions****************************
		if( name==L"name" ){
			processed=true;

			//---------------------------//
			//-- (name module_filename)--//
			//---------------------------//
			if( c>=2 )
			{
				getarg( a[1],b[1],&t );
				if( x->load_module(t) )
					*r = L"exec";
				else
					*r = L"", x->m_Result=0xffff;
			}

		}else if( name==L"type" ){
			processed=true;

			//-----------------------------------//
			//-- (type ext method1 method2 ...)--//
			//-----------------------------------//
			for( int i=1; i<c; i++ )
			{
				getarg( a[i],b[i],&t );
				if( i==1 )
					x->set_cmp_ext( t );
				else
				{
					const wchar_t* ptr=t;
					x->add_cmp_mhd( *ptr==L'*' ? ptr+1 : ptr, *ptr==L'*' );
				}
			}
		}else if( name==L"use" ){
			processed=true;

			//-------------------------------//
			//-- (use module1 module2 ...) --//
			//-------------------------------//
			for( int i=1; i<c; i++ )
			{
				getarg( a[i],b[i],&t );
				x->m_subFile.add( t );
			}
		}
	}else{//************ Functions not available at load time *********************
		if( ki_memcmp( (const wchar_t*)name, L"arc", 3 ) ){
			processed=true;

			//---------------------------//
			//-- (arc[+-].xxx [slfrd]) --//
			//---------------------------//
			arc( ((const wchar_t*)name)+3, a, b, c, r );

		}else if( ki_memcmp( (const wchar_t*)name, L"list", 4 ) ){
			processed=true;

			//----------------------------//
			//-- (list[\*|\*.*] [slfn]) --//
			//----------------------------//
			list( ((const wchar_t*)name)+4, a, b, c, r );

		}else if( name==L"method" ){
			processed=true;

			//-------------------//
			//-- (method [no]) --//
			//-------------------//
			if( c>=2 )
			{
				getarg( a[1],b[1],&t );
				*r = t.getInt()==*x->m_psMhd ? L"1" : L"0";
			}
			else
				r->setInt( *x->m_psMhd );

		}else if( name==L"dir" ){
			processed=true;

			//-----------//
			//-- (dir) --//
			//-----------//
			*r = (x->m_psDir ? *x->m_psDir : (const wchar_t*)L"");

		}else if( name==L"del" ){
			processed=true;

			//-------------------//
			//-- (del filenam) --//
			//-------------------//
			if( c>=2 )
			{
				getarg( a[1],b[1],&t );
				::DeleteFileW( kiPath( t.unquote() ) );
			}

		}else if( ki_memcmp( (const wchar_t*)name, L"resp", 4 )
			||	  ki_memcmp( (const wchar_t*)name, L"resq", 4 ) ){
			processed=true;

			//----------------------------//
			//-- (resp[@|-o] (list a)) ---//
			//----------------------------//
			resp( name[3]==L'p', ((const wchar_t*)name)+4, a, b, c, r );

		}else if( name==L"cd" ){
			processed=true;

			//-------------------//
			//-- (cd directory)--//
			//-------------------//
			if( c>=2 )
			{
				getarg( a[1],b[1],&t );
				::SetCurrentDirectoryW( t.unquote() );
			}

		}else if( name==L"cmd" || name==L"xcmd" ){
			processed=true;

			//----------------------------//
			//-- (cmd command line ...)---//
			//-- (xcmd command line ...)--//
			//----------------------------//
			if( name[0]==L'x' && c<2 )
				x->m_Result = 0xffff;
			else
			{
				CArcModule* xxx = x->exe;
				kiVar       cmd;
				int         i=1;

				if( name[0] == L'x' )
				{
					kiVar mm;
					getarg( a[i],b[i],&mm );
					i++;
					xxx = new CArcModule( mm );
				}
				for( ; i<c; i++ )
					getarg( a[i],b[i],&t ), cmd+=t, cmd+=L' ';

				if( m_mode == mTst ) {
					// Capture stdout for test result display
					kiStr cap;
					x->m_Result = xxx->tst_exe( cmd, cap );
					if( x->m_psTstOutput )
						*x->m_psTstOutput += cap;
				} else {
					bool m = (mycnf().miniboot() || m_mode==mDc1);
					x->m_Result = xxx->cmd( cmd, m );
				}
				r->setInt( x->m_Result );

				if( name[0] == L'x' )
					delete xxx;
			}
		}else if( name==L"scan" || name==L"xscan" ){
			processed=true;

			//----------------------------------------//
			//-- (scan BL BSL EL SL dx cmd...) -------//
			//-- (xscan BL BSL EL SL dx CMD cmd...) --//
			//----------------------------------------//
			if( c<6 || (name[0]==L'x'&&c<7) )
				x->m_Result = 0xffff;
			else
			{
				CArcModule* xxx = x->exe;

				kiVar BL, EL;
				getarg( a[1],b[1],&BL );
				getarg( a[2],b[2],&t );
				int BSL = t.getInt();
				getarg( a[3],b[3],&EL );
				getarg( a[4],b[4],&t );
				int SL = t.getInt();
				getarg( a[5],b[5],&t );
				int dx = t.getInt();

				int i=6;
				if( name[0] == L'x' )
				{
					kiVar mm;
					getarg( a[i],b[i],&mm );
					i++;
					xxx = new CArcModule( mm );
				}

				kiVar cmd;
				for( ; i<c; ++i )
					getarg( a[i],b[i],&t ), cmd+=t, cmd+=L' ';

				x->m_Result = xxx->lst_exe(
					cmd, *const_cast<aflArray*>(x->m_psAInfo),
					BL, BSL, EL, SL, dx ) ? 0 : -1;

				if( name[0] == L'x' )
					delete xxx;
			}
		}else if( name==L"input" ){
			processed=true;

			//---------------------------------------//
			//-- (input MSG DEFAULT TITLE) --//
			//---------------------------------------//
			kiVar msg, defval, title;
			if( c>=2 )
				getarg( a[1],b[1],&msg );
			if( c>=3 )
				getarg( a[2],b[2],&defval );
			if( c>=4 )
				getarg( a[3],b[3],&title );
			input( msg, title, defval, r );
		}else if( name==L"inputpw" ){
			processed=true;

			//-----------------------------------------//
			//-- (inputpw MSG DEFAULT TITLE) --//
			//-----------------------------------------//
			kiVar msg, defval, title;
			if( c>=2 )
				getarg( a[1],b[1],&msg );
			if( c>=3 )
				getarg( a[2],b[2],&defval );
			if( c>=4 )
				getarg( a[3],b[3],&title );
			inputpw( msg, title, defval, r );
		}else if( name==L"size" ){
			processed=true;

			//---------------------//
			//-- (size FILENAME) --//
			//---------------------//
			if( c>=2 )
			{
				kiVar fnm;
				getarg( a[1],b[1],&fnm );
				r->setInt( kiFile::getSize( fnm.unquote() ) );
			}
		}else if( name==L"is_file" ){
			processed=true;

			//---------------------//
			//-- (is_file) --------//
			//---------------------//
			if( c==1 )
				*r = (x->m_psList->len()==2
					  && !kiSUtil::isdir( (*x->m_psList)[1].cFileName )) ? L"1" : L"0";
		}else if( name==L"is_folder" ){
			processed=true;

			//---------------------//
			//-- (is_folder) ------//
			//---------------------//
			if( c==1 )
				*r = (x->m_psList->len()==2
					  && kiSUtil::isdir( (*x->m_psList)[1].cFileName )) ? L"1" : L"0";
		}else if( name==L"is_multiple" ){
			processed=true;

			//---------------------//
			//-- (is_multiple) ----//
			//---------------------//
			if( c==1 )
				*r = x->m_psList->len()>2 ? L"1" : L"0";
		}else if( name==L"find" ){
			processed=true;

			//---------------------//
			//-- (find FILENAME) --//
			//---------------------//
			if( c>=2 )
			{
				kiVar fnm;
				getarg( a[1],b[1],&fnm );
				wchar_t buf[MAX_PATH];
				kiPath exeDir2( kiPath::Exe );
				kiPath binDir2( kiPath::Exe ); binDir2 += L"bin\\";
				const wchar_t* fn = fnm.unquote();
				if( 0!=::SearchPathW( exeDir2,fn,NULL,MAX_PATH,buf,NULL ) ||
					0!=::SearchPathW( binDir2,fn,NULL,MAX_PATH,buf,NULL ) ||
					0!=::SearchPathW( NULL,   fn,NULL,MAX_PATH,buf,NULL ) )
					*r = buf, r->quote();
				else
					*r = L"";
			}
		}
	}

	return processed ? true : kiRythpVM::exec_function(name,a,b,c,r);
}

void CArcB2e::CB2eCore::arc( const wchar_t* opt, const CharArray& a, const BoolArray& b,int c, kiVar* r )
{
	//---------------------------//
	//-- (arc[+-].xxx [slfrd]) --//
	//---------------------------//

	// Default option settings
	const wchar_t* anm=x->m_psArc->lname;
	enum{ full, nam, dir } part=full;
	if( m_mode==mSfx )	part=nam; // sfx

	// Override if specified
	if( c>=2 )
	{
		getarg( a[1],b[1],&t );
		for( const wchar_t* p=t; *p; p++ )
			switch(*p)
			{
			case L's': anm=x->m_psArc->sname; break;
			case L'l': anm=x->m_psArc->lname; break;
			case L'f': part=full; break;
			case L'n': part=nam;  break;
			case L'd': part=dir;  break;
			}
	}

	// Directory part
	*r = (part==nam ? (const wchar_t*)L"" : x->m_psArc->basedir);

	// Name part
	if( part != dir )
	{
		if( *opt==L'\0' || *opt==L'+' )
		{
			// (arc)       : return anm as-is
			*r += anm;
			// (arc+XXX)   : return anmXXX
			if( *opt==L'+' )
				*r += (opt+1);
		}
		else
		{
			const wchar_t* ext = kiPath::ext(anm);
			const wchar_t* add = L"";
			if( opt[0]==L'-' && opt[1]==L'.' )
			{
				// (arc-.XXX) : remove trailing .XXX if present.
				//            : otherwise append .decompressed
				if( 0!=ki_strcmpi( ext, opt+2 ) )
					ext = anm + ki_strlen(anm), add = L".decompressed";
			}
			else
			{
				// (arc.XXX) : replace last extension with .XXX
				// (arc.)    : remove all extensions
				if( opt[1]!=L'\0' )
					add = opt;
				switch(mycnf().extnum())
				{
				case 0: ext = anm + ::lstrlenW(anm);break;
				case 1: ext = kiPath::ext(anm);    break;
				default:ext = kiPath::ext_all(anm);break;
				}
			}
			if( *ext )
				ext--;

			wchar_t buf[MAX_PATH];
			ki_memcpy( buf, anm, (int)(ext-anm)*(int)sizeof(wchar_t) );
			buf[ ext-anm ] = L'\0';
			*r += buf;
			*r += add;
		}

		// Quote if necessary (full or nam)
		r->quote();
	}
	else
	{
		// part==dir: double any trailing separator before quoting.
		// Windows argument parsers treat '\"' as an escaped quote, so
		// "C:\dir\" would be malformed.  "C:\dir\\" is correct.
		const wchar_t* s = (const wchar_t*)*r;
		int n = r->len();
		if( n > 0 && (s[n-1]==L'\\' || s[n-1]==L'/') )
			*r += s[n-1];
		r->quote();
	}
}

static void selfR(
	const wchar_t* writedir, const wchar_t* fullpath, bool lfn, kiVar* r )
{
	kiFindFile       f;
	WIN32_FIND_DATAW fd;
	f.begin( kiStr(fullpath) += L"\\*" );

	kiVar t, t2, t3;
	while( f.next(&fd) )
	{
		t = writedir;
		t+= L'\\';
		t+= (lfn ? fd.cFileName : fd.cAlternateFileName);
		if( fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
		{
			t2 = t;
			t  = L"";
			t3 = fullpath;
			t3+= L'\\';
			t3+= (lfn ? fd.cFileName : fd.cAlternateFileName);
			selfR( t2, t3, lfn, &t );
		}
		else
		{
			if( lfn )
				t.quote();
		}
		*r += t;
		*r += L' ';
	}
}

void CArcB2e::CB2eCore::list( const wchar_t* opt, const CharArray& a, const BoolArray& b,int c, kiVar* r )
{
	//---------------------------//
	//-- (list[r|\*.*] [slfn]) --//
	//---------------------------//

	if( m_mode!=mEnc ) // For extraction
	{
		*r = L"";

		for( unsigned int i=0; i!=x->m_psAInfo->len(); i++ )
			if( (*x->m_psAInfo)[i].selected )
			{
				// Handle args starting with '-'?
				t = (*x->m_psAInfo)[i].inf.szFileName;
				t.quote();
				*r += t;
				*r += L' ';
			}
	}
	else // For compression
	{
		// Default option settings
		bool lfn=true;
		enum{ full, nam } part=nam;
		// Override if specified
		if( c>=2 )
		{
			getarg( a[1],b[1],&t );
			for( const wchar_t* p=t; *p; p++ )
				switch(*p)
				{
				case L's': lfn=false; break;
				case L'l': lfn=true;  break;
				case L'f': part=full; break;
				case L'n': part=nam;  break;
				}
		}
		// Whether to do recursive listing ourselves
		bool selfrecurse = (*opt==L'r');

		// Suffix to append after directory name.
		if( *opt==L'\\' || *opt==L'/' )
			opt++;

		// List up — skip wfd[0] (output archive); sources start at index 1
		kiVar t2,t3;
		*r = L"";
		for( unsigned int i=1; i!=x->m_psList->len(); i++ )
		{
			// Filename part
			t = ( part==full ? *x->m_psDir : (const wchar_t*)L"");
			t += lfn ? (*x->m_psList)[i].cFileName : (*x->m_psList)[i].cAlternateFileName;

			if( selfrecurse && ((*x->m_psList)[i].dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) )
			{
				// Self recursion
				t2 = t;
				t  = L"";
				t3 = *x->m_psDir;
				t3+= lfn ? (*x->m_psList)[i].cFileName : (*x->m_psList)[i].cAlternateFileName;
				selfR( t2, t3, lfn, &t );
			}
			else
			{
				// Normal processing
				if( *opt && ((*x->m_psList)[i].dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) )
					t += L'\\', t += opt;
				if( lfn )
					t.quote();
			}
			*r += t;
			*r += L' ';
		}
	}

	r->removeTrailWS();
}

void CArcB2e::CB2eCore::resp( bool needq, const wchar_t* opt, const CharArray& a, const BoolArray& b,int c, kiVar* r )
{
	//-----------------------------//
	//-- (resp[@|-o] (list) ...) --//
	//-----------------------------//

	// Create response file name
	kiPath rspfile;
	myapp().get_tempdir(rspfile);
	rspfile += L"filelist";

	// Combine with options and return
	*r  = opt;
	*r += rspfile;

	// Write to file
	kiFile fp;
	if( !fp.open( rspfile,false ) )
		return;

	// The response file is consumed by the external tool, which still expects
	// its current (ANSI / CP_ACP) encoding.  Convert each segment to CP_ACP
	// before writing.  (Switching this + the tool's -scs to UTF-8 is the later
	// step that makes non-ASCII names lossless.)
	auto writeAnsi = [&]( const wchar_t* w, int wlen )
	{
		if( wlen<=0 ) return;
		int n = ::WideCharToMultiByte( CP_ACP, 0, w, wlen, NULL, 0, NULL, NULL );
		if( n<=0 ) return;
		std::vector<char> abuf( n );
		::WideCharToMultiByte( CP_ACP, 0, w, wlen, abuf.data(), n, NULL, NULL );
		fp.write( abuf.data(), (unsigned long)n );
	};

	kiVar tmp;
	for( int i=1; i<c; i++ )
	{
		// Write each argument split-by-split to fp
		getarg( a[i],b[i],&tmp );

		for( const wchar_t *s,*p=tmp; *p; p++ )
		{
			// Skip extra whitespace
			while( *p==L' ' )
				p++;
			if( *p==L'\0' )
				break;

			// Move toward end of argument...
			s=p;
			for( int q=0; *p!=L'\0' && (*p!=L' ' || (q&1)!=0); p++ )
				if( *p==L'"' )
					q++;

			// Quote balancing fix #1
			if( !needq && *s==L'"' )
			{
				s++;
				if( p!=s && *(p-1)==L'"' )
					p--;
			}

			writeAnsi( s, static_cast<int>(p-s) );
			fp.write( "\r\n", 2 );

			// Quote balancing fix #2
			if( *p==L'"' )
				p++;
			if( *p==L'\0' )
				break;
		}
	}
}

// Dialog data passed via LPARAM to B2eInputDlgProc.
struct B2eInputData {
	wchar_t result[512];
	const wchar_t* msg;          // body label; null = keep RC default
	const wchar_t* title;        // title bar; null = keep RC default
	const wchar_t* initialValue;
};

static INT_PTR CALLBACK B2eInputDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	if (msg == WM_INITDIALOG) {
		SetWindowLongPtrW(hwnd, DWLP_USER, lp);
		auto* d = reinterpret_cast<B2eInputData*>(lp);
		if (d && d->title && d->title[0])
			SetWindowTextW(hwnd, d->title);
		if (d && d->msg && d->msg[0])
			SetDlgItemTextW(hwnd, IDC_INPUT_LABEL, d->msg);
		if (d && d->initialValue && d->initialValue[0])
			SetDlgItemTextW(hwnd, IDC_PASSWORD_INPUT, d->initialValue);
		return TRUE;
	}
	auto* d = reinterpret_cast<B2eInputData*>(GetWindowLongPtrW(hwnd, DWLP_USER));
	if (msg == WM_COMMAND && d) {
		if (LOWORD(wp) == IDOK) {
			GetDlgItemTextW(hwnd, IDC_PASSWORD_INPUT, d->result, 512);
			EndDialog(hwnd, IDOK);
		} else if (LOWORD(wp) == IDCANCEL) {
			EndDialog(hwnd, IDCANCEL);
		}
	}
	return FALSE;
}

static void b2e_input_impl(const wchar_t* msg, const wchar_t* title, const wchar_t* defval,
                            UINT dialogId, kiVar* r)
{
	// msg/title/defval are already wide (UTF-16), so no conversion is needed.
	B2eInputData data = {};
	data.msg          = (msg    && msg[0])    ? msg    : nullptr;
	data.title        = (title  && title[0])  ? title  : nullptr;
	data.initialValue = (defval && defval[0]) ? defval : nullptr;

	HWND parent = s_dialogParentHwnd ? s_dialogParentHwnd : GetActiveWindow();
	INT_PTR res = DialogBoxParamW(
		GetModuleHandleW(NULL),
		MAKEINTRESOURCEW(dialogId),
		parent, B2eInputDlgProc, (LPARAM)&data);

	if (res == IDOK)
		*r = data.result;
	else
		*r = defval ? defval : L"";
}

void CArcB2e::CB2eCore::input(const wchar_t* msg, const wchar_t* title, const wchar_t* defval, kiVar* r)
{
	b2e_input_impl(msg, title, defval, IDD_INPUT, r);
}

void CArcB2e::CB2eCore::inputpw(const wchar_t* msg, const wchar_t* title, const wchar_t* defval, kiVar* r)
{
	b2e_input_impl(msg, title, defval, IDD_PASSWORD, r);
}
