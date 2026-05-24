
#include "stdafx.h"
#include "ArcB2e.h"
#include "resource.h"
#include "AileFlowApp.h"

//----------------- ArcB2e class general processing ------------------------------

char CArcB2e::st_base[MAX_PATH];
int  CArcB2e::st_life=0;
CArcB2e::CB2eCore* CArcB2e::rvm=NULL;

const char* CArcB2e::init_b2e_path()
{
	kiPath dir( kiPath::Exe );
	ki_strcpy( st_base, dir+="b2e\\" );
	return st_base;
}

CArcB2e::CArcB2e( const char* scriptname ) : CArchiver( scriptname )
{
	st_life++;
	exe = NULL;
	m_LstScr = m_DcEScr = m_EncScr =
	m_DecScr = m_SfxScr = m_LoadScr= m_ScriptBuf = NULL;
}

CArcB2e::~CArcB2e()
{
	if( !(--st_life) )
		delete rvm;
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
		str += "\r\n";
		CArcModule(m_subFile[i]).ver( tmp );
		str += tmp;
	}
	return true;
}

//------------------- Load script & eval( load: ) -------------------

bool CArcB2e::load_module( const char* name )
{
	exe = new CArcModule( name );
	return exe->exist();
}

int CArcB2e::v_load()
{
	//-- Open extended script file
	kiStr fname( st_base ); fname += mlt_ext();
	kiFile fp;
	if( fp.open( fname ) )
	{
		//-- Read entire file
		unsigned int ln=fp.getSize();
		m_ScriptBuf = new char[ ln+1 ];
		ln = fp.read( (unsigned char*)m_ScriptBuf, ln );
		m_ScriptBuf[ ln ] = '\0';

		//-- Split into sections
		bool pack1;
		for( char* p=m_ScriptBuf; *p; p++ )
		{
			switch( *p )
			{
			case 'c': case 'd': case 'e': case 'l': case 's':
				if( ki_memcmp(p,"load:",5) )
					*p='\0', m_LoadScr = (p+=4)+1;
				else if( ki_memcmp(p,"encode:",7) )
					*p='\0', m_EncScr = (p+=6)+1, pack1=false;
				else if( ki_memcmp(p,"encode1:",8) )
					*p='\0', m_EncScr = (p+=7)+1, pack1=true;
				else if( ki_memcmp(p,"decode:",7) )
					*p='\0', m_DecScr = (p+=6)+1;
				else if( ki_memcmp(p,"sfx:",4) )
					*p='\0', m_SfxScr = (p+=3)+1, m_SfxDirect=false;
				else if( ki_memcmp(p,"sfxd:",5) )
					*p='\0', m_SfxScr = (p+=4)+1, m_SfxDirect=true;
				else if( ki_memcmp(p,"decode1:",8) )
					*p='\0', m_DcEScr = (p+=7)+1;
				else if( ki_memcmp(p,"list:",5) )
					*p='\0', m_LstScr = (p+=4)+1;
			}
			while( *p && *p!='\n' && *p!='\r' )
				p++;
			if( *p=='\0' )
				break;
		}

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
					 | (m_EncScr?aCompress|(pack1?0:aArchive)|(m_SfxScr?aSfx:0):0);
		}
	}
	return 0;
}

int CArcB2e::exec_script( const char* scr, scr_mode mode )
{
	//-- Initialize
	m_Result = 0;
	rvm->setPtr( this, mode );

	//-- Execute
	char* script = new char[ki_strlen(scr)+8];
	ki_strcpy( script, "(exec " );
	ki_strcat( script, scr );
	ki_strcat( script, ")" );
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

//-------------------- Compression processing eval( encode: sfx: ) -----------------------

int CArcB2e::cmpr( const char* scr, const kiPath& base, const wfdArray& files, const kiPath& ddir, const int method )
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
	WIN32_FIND_DATA fd;
	kiPath wild( temp );
	f.begin( wild += "*" );
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
	const char* theScript = m_EncScr;

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
		if( name=="name" ){
			processed=true;

			//---------------------------//
			//-- (name module_filename)--//
			//---------------------------//
			if( c>=2 )
			{
				getarg( a[1],b[1],&t );
				if( x->load_module(t) )
					*r = "exec";
				else
					*r = "", x->m_Result=0xffff;
			}

		}else if( name=="type" ){
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
					const char* ptr=t;
					x->add_cmp_mhd( *ptr=='*' ? ptr+1 : ptr, *ptr=='*' );
				}
			}
		}else if( name=="use" ){
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
		if( ki_memcmp( (const char*)name, "arc", 3 ) ){
			processed=true;

			//---------------------------//
			//-- (arc[+-].xxx [slfrd]) --//
			//---------------------------//
			arc( ((const char*)name)+3, a, b, c, r );

		}else if( ki_memcmp( (const char*)name, "list", 4 ) ){
			processed=true;

			//----------------------------//
			//-- (list[\*|\*.*] [slfn]) --//
			//----------------------------//
			list( ((const char*)name)+4, a, b, c, r );

		}else if( name=="method" ){
			processed=true;

			//-------------------//
			//-- (method [no]) --//
			//-------------------//
			if( c>=2 )
			{
				getarg( a[1],b[1],&t );
				*r = t.getInt()==*x->m_psMhd ? "1" : "0";
			}
			else
				r->setInt( *x->m_psMhd );

		}else if( name=="dir" ){
			processed=true;

			//-----------//
			//-- (dir) --//
			//-----------//
			*r = (x->m_psDir ? *x->m_psDir : (const char*)"");

		}else if( name=="del" ){
			processed=true;

			//-------------------//
			//-- (del filenam) --//
			//-------------------//
			if( c>=2 )
			{
				getarg( a[1],b[1],&t );
				::DeleteFile( kiPath( t.unquote() ) );
			}

		}else if( ki_memcmp( (const char*)name, "resp", 4 )
			||	  ki_memcmp( (const char*)name, "resq", 4 ) ){
			processed=true;

			//----------------------------//
			//-- (resp[@|-o] (list a)) ---//
			//----------------------------//
			resp( name[3]=='p', ((const char*)name)+4, a, b, c, r );

		}else if( name=="cd" ){
			processed=true;

			//-------------------//
			//-- (cd directory)--//
			//-------------------//
			if( c>=2 )
			{
				getarg( a[1],b[1],&t );
				::SetCurrentDirectory( t.unquote() );
			}

		}else if( name=="cmd" || name=="xcmd" ){
			processed=true;

			//----------------------------//
			//-- (cmd command line ...)---//
			//-- (xcmd command line ...)--//
			//----------------------------//
			if( name[0]=='x' && c<2 )
				x->m_Result = 0xffff;
			else
			{
				CArcModule* xxx = x->exe;
				kiVar       cmd;
				int         i=1;

				if( name[0] == 'x' )
				{
					kiVar mm;
					getarg( a[i],b[i],&mm );
					i++;
					xxx = new CArcModule( mm );
				}
				for( ; i<c; i++ )
					getarg( a[i],b[i],&t ), cmd+=t, cmd+=' ';

				bool m = (mycnf().miniboot() || m_mode==mDc1);
				x->m_Result = xxx->cmd( cmd, m );
				r->setInt( x->m_Result );

				if( name[0] == 'x' )
					delete xxx;
			}
		}else if( name=="scan" || name=="xscan" ){
			processed=true;

			//----------------------------------------//
			//-- (scan BL BSL EL SL dx cmd...) -------//
			//-- (xscan BL BSL EL SL dx CMD cmd...) --//
			//----------------------------------------//
			if( c<6 || (name[0]=='x'&&c<7) )
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
				if( name[0] == 'x' )
				{
					kiVar mm;
					getarg( a[i],b[i],&mm );
					i++;
					xxx = new CArcModule( mm );
				}

				kiVar cmd;
				for( ; i<c; ++i )
					getarg( a[i],b[i],&t ), cmd+=t, cmd+=' ';

				x->m_Result = xxx->lst_exe(
					cmd, *const_cast<aflArray*>(x->m_psAInfo),
					BL, BSL, EL, SL, dx ) ? 0 : -1;

				if( name[0] == 'x' )
					delete xxx;
			}
		}else if( name=="input" ){
			processed=true;

			//-------------------------//
			//-- (input MSG DEFUALT) --//
			//-------------------------//
			kiVar msg, defval;
			if( c>=2 )
				getarg( a[1],b[1],&msg );
			if( c>=3 )
				getarg( a[2],b[2],&defval );
			input( msg, defval, r );
		}else if( name=="size" ){
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
		}else if( name=="is_file" ){
			processed=true;

			//---------------------//
			//-- (is_file) --------//
			//---------------------//
			if( c==1 )
				*r = (x->m_psList->len()==1
					  && !kiSUtil::isdir( (*x->m_psList)[0].cFileName )) ? "1" : "0";
		}else if( name=="is_folder" ){
			processed=true;

			//---------------------//
			//-- (is_folder) ------//
			//---------------------//
			if( c==1 )
				*r = (x->m_psList->len()==1
					  && kiSUtil::isdir( (*x->m_psList)[0].cFileName )) ? "1" : "0";
		}else if( name=="is_multiple" ){
			processed=true;

			//---------------------//
			//-- (is_multiple) ----//
			//---------------------//
			if( c==1 )
				*r = x->m_psList->len()>1 ? "1" : "0";
		}else if( name=="find" ){
			processed=true;

			//---------------------//
			//-- (find FILENAME) --//
			//---------------------//
			if( c>=2 )
			{
				kiVar fnm;
				getarg( a[1],b[1],&fnm );
				char buf[MAX_PATH];
				if( 0==::SearchPath( NULL,fnm.unquote(),NULL,MAX_PATH,buf,NULL ) )
					*r = "";
				else
					*r = buf, r->quote();
			}
		}
	}

	return processed ? true : kiRythpVM::exec_function(name,a,b,c,r);
}

void CArcB2e::CB2eCore::arc( const char* opt, const CharArray& a, const BoolArray& b,int c, kiVar* r )
{
	//---------------------------//
	//-- (arc[+-].xxx [slfrd]) --//
	//---------------------------//

	// Default option settings
	const char* anm=x->m_psArc->lname;
	enum{ full, nam, dir } part=full;
	if( m_mode==mSfx )	part=nam; // sfx

	// Override if specified
	if( c>=2 )
	{
		getarg( a[1],b[1],&t );
		for( const char* p=t; *p; p++ )
			switch(*p)
			{
			case 's': anm=x->m_psArc->sname; break;
			case 'l': anm=x->m_psArc->lname; break;
			case 'f': part=full; break;
			case 'n': part=nam;  break;
			case 'd': part=dir;  break;
			}
	}

	// Directory part
	*r = (part==nam ? (const char*)"" : x->m_psArc->basedir);

	// Name part
	if( part != dir )
	{
		if( *opt=='\0' || *opt=='+' )
		{
			// (arc)       : return anm as-is
			*r += anm;
			// (arc+XXX)   : return anmXXX
			if( *opt=='+' )
				*r += (opt+1);
		}
		else
		{
			const char* ext = kiPath::ext(anm);
			const char* add = "";
			if( opt[0]=='-' && opt[1]=='.' )
			{
				// (arc-.XXX) : remove trailing .XXX if present.
				//            : otherwise append .decompressed
				if( 0!=ki_strcmpi( ext, opt+2 ) )
					ext = anm + ki_strlen(anm), add = ".decompressed";
			}
			else
			{
				// (arc.XXX) : replace last extension with .XXX
				// (arc.)    : remove all extensions
				if( opt[1]!='\0' )
					add = opt;
				switch(mycnf().extnum())
				{
				case 0: ext = anm + ::lstrlen(anm);break;
				case 1: ext = kiPath::ext(anm);    break;
				default:ext = kiPath::ext_all(anm);break;
				}
			}
			if( *ext )
				ext--;

			char buf[MAX_PATH];
			ki_memcpy( buf, anm, ext-anm );
			buf[ ext-anm ] = '\0';
			*r += buf;
			*r += add;
		}

		// Quote if necessary
		if( part==full )
			r->quote();
	}
}

static void selfR(
	const char* writedir, const char* fullpath, bool lfn, kiVar* r )
{
	kiFindFile       f;
	WIN32_FIND_DATA fd;
	f.begin( kiStr(fullpath) += "\\*" );

	kiVar t, t2, t3;
	while( f.next(&fd) )
	{
		t = writedir;
		t+= '\\';
		t+= (lfn ? fd.cFileName : fd.cAlternateFileName);
		if( fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
		{
			t2 = t;
			t  = "";
			t3 = fullpath;
			t3+= '\\';
			t3+= (lfn ? fd.cFileName : fd.cAlternateFileName);
			selfR( t2, t3, lfn, &t );
		}
		else
		{
			if( lfn )
				t.quote();
		}
		*r += t;
		*r += ' ';
	}
}

void CArcB2e::CB2eCore::list( const char* opt, const CharArray& a, const BoolArray& b,int c, kiVar* r )
{
	//---------------------------//
	//-- (list[r|\*.*] [slfn]) --//
	//---------------------------//

	if( m_mode!=mEnc ) // For extraction
	{
		*r = "";

		for( unsigned int i=0; i!=x->m_psAInfo->len(); i++ )
			if( (*x->m_psAInfo)[i].selected )
			{
				// Handle args starting with '-'?
				t = (*x->m_psAInfo)[i].inf.szFileName;
				t.quote();
				*r += t;
				*r += ' ';
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
			for( const char* p=t; *p; p++ )
				switch(*p)
				{
				case 's': lfn=false; break;
				case 'l': lfn=true;  break;
				case 'f': part=full; break;
				case 'n': part=nam;  break;
				}
		}
		// Whether to do recursive listing ourselves
		bool selfrecurse = (*opt=='r');

		// Suffix to append after directory name.
		if( *opt=='\\' || *opt=='/' )
			opt++;

		// List up
		kiVar t2,t3;
		*r = "";
		for( unsigned int i=0; i!=x->m_psList->len(); i++ )
		{
			// Filename part
			t = ( part==full ? *x->m_psDir : (const char*)"");
			t += lfn ? (*x->m_psList)[i].cFileName : (*x->m_psList)[i].cAlternateFileName;

			if( selfrecurse && ((*x->m_psList)[i].dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) )
			{
				// Self recursion
				t2 = t;
				t  = "";
				t3 = *x->m_psDir;
				t3+= lfn ? (*x->m_psList)[i].cFileName : (*x->m_psList)[i].cAlternateFileName;
				selfR( t2, t3, lfn, &t );
			}
			else
			{
				// Normal processing
				if( *opt && ((*x->m_psList)[i].dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) )
					t += '\\', t += opt;
				if( lfn )
					t.quote();
			}
			*r += t;
			*r += ' ';
		}
	}

	r->removeTrailWS();
}

void CArcB2e::CB2eCore::resp( bool needq, const char* opt, const CharArray& a, const BoolArray& b,int c, kiVar* r )
{
	//-----------------------------//
	//-- (resp[@|-o] (list) ...) --//
	//-----------------------------//

	// Create response file name
	kiPath rspfile;
	myapp().get_tempdir(rspfile);
	rspfile += "filelist";

	// Combine with options and return
	*r  = opt;
	*r += rspfile;

	// Write to file
	kiFile fp;
	if( !fp.open( rspfile,false ) )
		return;

	kiVar tmp;
	for( int i=1; i<c; i++ )
	{
		// Write each argument split-by-split to fp
		getarg( a[i],b[i],&tmp );

		for( const char *s,*p=tmp; *p; p++ )
		{
			// Skip extra whitespace
			while( *p==' ' )
				p++;
			if( *p=='\0' )
				break;

			// Move toward end of argument...
			s=p;
			for( int q=0; *p!='\0' && (*p!=' ' || (q&1)!=0); p++ )
				if( *p=='"' )
					q++;

			// Quote balancing fix #1
			if( !needq && *s=='"' )
			{
				s++;
				if( p!=s && *(p-1)=='"' )
					p--;
			}

			fp.write( s, static_cast<unsigned long>(p-s) );
			fp.write( "\r\n", 2 );

			// Quote balancing fix #2
			if( *p=='"' )
				p++;
			if( *p=='\0' )
				break;
		}
	}
}

void CArcB2e::CB2eCore::input( const char* msg, const char* defval, kiVar* r )
{
	// Phase 1 stub: show a simple Win32 input box.
	// Phase 4 will replace this with a proper integrated dialog.
	char buf[32768] = {};
	if( defval ) ki_strcpy( buf, defval );

	// Use a minimal Win32 dialog via InputBox pattern.
	// For now, prompt via a MessageBox + use defval as the result.
	// Real password input is not functional until Phase 4.
	*r = buf;
}
