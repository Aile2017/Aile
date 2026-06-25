// Archiver.h
//-- CArchiver -- common interface in 'Noah' for arhiving routine --

#ifndef AFX_ARCHIVER_H__359A2ED3_2F97_480E_BC94_24834EBA6498__INCLUDED_
#define AFX_ARCHIVER_H__359A2ED3_2F97_480E_BC94_24834EBA6498__INCLUDED_

enum {
	aMelt = 2, aList = 4, aMeltEach = 8, aCompress = 16, aArchive = 32, aSfx = 64,
	aTest = 128, aDelete = 256,
};

struct arcname {
	arcname( const kiPath& b,const wchar_t *s,const wchar_t *l )
		: basedir(b),sname(s),lname(l) {}
	const kiPath& basedir;
	const wchar_t* lname;
	const wchar_t* sname;
};

struct arcfile {
	INDIVIDUALINFO inf;
	wchar_t rawline[256];
	union {
		bool selected;
		bool isfile;
	};
};

#define aflArray kiArray<arcfile>
#define wfdArray kiArray<WIN32_FIND_DATA>

class CArchiver
{
public: //--< attribute >--

	int ability();
	int cancompressby( const wchar_t* ext, const wchar_t* mhd, bool sfx );

	const wchar_t*  mlt_ext();
	const kiStr&    cmp_ext();
	const StrArray& cmp_mhd_list();
	const int       cmp_mhd_default();
	bool            ver( kiStr& str );
	static bool GetVersionInfoStr( wchar_t* name, wchar_t* buf, size_t cbBuf );

public: //--< action >--

	int  melt( const arcname& aname, const kiPath& ddir, const aflArray* files=NULL );
	bool list( const arcname& aname, aflArray& files );
	int  compress( const kiPath& base, const wfdArray& files, const kiPath& ddir, int method, bool sfx );
	int  test( const arcname& aname, kiStr& output );
	int  delete_items( const arcname& aname, const aflArray& files );
	kiStr arctype_name(const wchar_t* an) const { return v_name(an); }

protected: //--< for child >--

	CArchiver( const wchar_t* mext );
	void set_cmp_ext( const wchar_t* ext );
	void add_cmp_mhd( const wchar_t* mhd, bool def=false );

	virtual int  v_load(){return 0;}
	virtual bool v_ver( kiStr& str ){return false;}
	virtual int  v_melt( const arcname& aname, const kiPath& ddir, const aflArray* files ){return false;}
	virtual bool v_list( const arcname& aname, aflArray& files ){return false;}
	virtual int  v_compress( const kiPath& base, const wfdArray& files, const kiPath& ddir, int method, bool sfx ){return false;}
	virtual int  v_test( const arcname& aname, kiStr& output ){(void)aname;(void)output;return 0xffff;}
	virtual int  v_delete( const arcname& aname, const aflArray& files ){(void)aname;(void)files;return 0xffff;}
	virtual kiStr v_name(const wchar_t*) const { return L""; }

private: //--< private >--

	void ensure_loaded() { if(not_loaded){ m_Able=v_load(); not_loaded=false; } }

	friend class CNoahArchiverManager;
	bool extCheck( const wchar_t* ext );
	kiStr m_MyExtList, m_MyCmpExt;
	StrArray m_Mhd;
	int m_MhdDef,m_Able;
	bool not_loaded;

public: //--< dummy >--

	virtual ~CArchiver(){}
};

inline int CArchiver::ability()
{
	ensure_loaded();
	return m_Able;
}

inline int CArchiver::cancompressby( const wchar_t* ext, const wchar_t* mhd, bool sfx )
{
	ensure_loaded();
	if( (sfx && !(m_Able&aSfx)) || !(m_Able&aCompress) || !m_MyCmpExt.equalsIgnoreCase(ext) )
		return -1; // no
	for( unsigned int i=0; i!=m_Mhd.len(); i++ )
		if( m_Mhd[i] == mhd )
			return (int)i;
	return -2; // only - 'type name' matched
}



inline int CArchiver::melt( const arcname& aname, const kiPath& ddir, const aflArray* files )
{
	ensure_loaded();
	return (m_Able&aMelt)?v_melt(aname,ddir,files):0xffff;
}

inline bool CArchiver::list( const arcname& aname, aflArray& files )
{
	ensure_loaded();
	return (m_Able&aList)?v_list(aname,files):false;
}

inline int CArchiver::compress( const kiPath& base, const wfdArray& files, const kiPath& ddir, int method, bool sfx )
{
	ensure_loaded();
	return (m_Able&aCompress)?v_compress(base,files,ddir,method,sfx):0xffff;
}

inline int CArchiver::test( const arcname& aname, kiStr& output )
{
	ensure_loaded();
	return (m_Able&aTest)?v_test(aname,output):0xffff;
}

inline int CArchiver::delete_items( const arcname& aname, const aflArray& files )
{
	ensure_loaded();
	return (m_Able&aDelete)?v_delete(aname,files):0xffff;
}

inline bool CArchiver::ver( kiStr& str )
{
	ensure_loaded();
	return v_ver(str);
}

inline const wchar_t* CArchiver::mlt_ext()
{
	return m_MyExtList;
}

inline const kiStr& CArchiver::cmp_ext()
{
	ensure_loaded();
	return m_MyCmpExt;
}

inline const StrArray& CArchiver::cmp_mhd_list()
{
	ensure_loaded();
	return m_Mhd;
}

inline const int CArchiver::cmp_mhd_default()
{
	ensure_loaded();
	return m_MhdDef;
}

inline CArchiver::CArchiver( const wchar_t* extlist )
		: m_MyExtList( extlist ), m_Mhd(3), m_MhdDef(0), not_loaded(true)
{
	m_MyExtList.lower();
}

inline void CArchiver::set_cmp_ext( const wchar_t* ext )
{
	m_MyCmpExt = ext;
}

inline void CArchiver::add_cmp_mhd( const wchar_t* method, bool Default )
{
	m_Mhd.add(method);
	if( Default )
		m_MhdDef = m_Mhd.len() - 1;
}

inline bool CArchiver::extCheck( const wchar_t* ext )
{
	const wchar_t *x=m_MyExtList,*y;
	int ln = ki_strlen(ext);
	while( *x )
	{
		for( y=x+1; *y && *y!=L'.'; y++ );
		if( *y==L'\0' ) break;

		if( y-x == ln )
		{
			while( x!=y )
			{
				if( *x!=ext[ln+(x-y)] )
					break;
				x++;
			}
			if( x==y )
				return true;
		}
		x=y+1;
	}
	return false;
}

/*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*/
// Archiver module layer. Also handles exe-based archivers.

class CArcModule
{
public:

		// Create by specifying the command name to execute.
		//  - If found as a file, treat it as an executable.
		//  - Otherwise assume it is a shell command and keep it as-is.
	CArcModule( const wchar_t* name );
	virtual ~CArcModule() {}
	bool exist();

	kiStr name() const { return kiPath::name(m_name); }

	int cmd( const wchar_t* cmd, bool mini=false );
	void ver( kiStr& str );

	bool lst_exe( const wchar_t* lstcmd, aflArray& files,
		const wchar_t* BL, int BSL, const wchar_t* EL, int SL, int dx );
	int  tst_exe( const wchar_t* tstcmd, kiStr& output );

private:
	enum { NOTEXIST=0, EXE, SHLCMD } m_type;
	wchar_t      m_name[MAX_PATH];
};

inline bool CArcModule::exist()
	{ return m_type!=NOTEXIST; }



#endif
