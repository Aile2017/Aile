#ifndef AFX_ARCB2e_H__697CC1BD_2C28_434C_8C53_239D624227C3__INCLUDED_
#define AFX_ARCB2e_H__697CC1BD_2C28_434C_8C53_239D624227C3__INCLUDED_

#include "Archiver.h"

class CArcB2e : public CArchiver
{
public: //--<action>--

	CArcB2e( const char* scriptname );
	virtual ~CArcB2e();
	static const char* init_b2e_path();

private: //--<CArchiver>--

	int  v_load();
	bool v_ver( kiStr& str );
	int  v_melt( const arcname& aname, const kiPath& ddir, const aflArray* files );
	bool v_list( const arcname& aname, aflArray& files );
	int  v_compress( const kiPath& base, const wfdArray& files, const kiPath& ddir, int method, bool sfx );
	int  v_test( const arcname& aname, kiStr& output );
	int  v_delete( const arcname& aname, const aflArray& files );
	kiStr v_name(const char*) const { return exe ? exe->name() : kiStr(""); }

	bool arc2sfx( const kiPath& temp, const kiPath& dest );
	int  cmpr( const char* scr, const kiPath& base, const wfdArray& files, const kiPath& ddir, const int method );

private: //--<RythpScript>--

	// scripts
	char*       m_ScriptBuf;
	char*       m_LoadScr;
	const char* m_EncScr;
	const char* m_DecScr;
	const char* m_SfxScr;
	const char* m_DcEScr;
	const char* m_LstScr;
	const char* m_TstScr;
	const char* m_DelScr;
	bool        m_SfxDirect;

	enum scr_mode { mLod, mEnc, mDec, mDc1, mSfx, mLst, mTst, mDel };
	int exec_script( const char* scr, scr_mode mode );

	// B2e Core
	class CB2eCore : public kiRythpVM
	{
		friend class CArcB2e;

		bool exec_function( const kiVar& name, const CharArray& a, const BoolArray& b,int c, kiVar* r );

		void arc( const char* opt, const CharArray& a, const BoolArray& b,int c, kiVar* r );
		void list( const char* opt, const CharArray& a, const BoolArray& b,int c, kiVar* r );
		void resp( bool needq, const char* opt, const CharArray& a, const BoolArray& b,int c, kiVar* r );
		void input( const char* msg, const char* defval, kiVar* r );

		void setPtr( CArcB2e* p, scr_mode m ){x=p;m_mode=m;}
		CArcB2e* x;
		scr_mode m_mode;
		kiVar t;
	};
	friend class CB2eCore;
	static char st_base[MAX_PATH];
	static int  st_life;
	static CB2eCore* rvm;

	// module
	CArcModule* exe;
	kiArray<kiStr> m_subFile;

	// script interface
	bool load_module( const char* name );
	int m_Result;
	const arcname*  m_psArc;
	const kiPath*   m_psDir;
	const int*      m_psMhd;
	const wfdArray* m_psList;
	const aflArray* m_psAInfo;
	kiStr*          m_psTstOutput;
};

#endif
