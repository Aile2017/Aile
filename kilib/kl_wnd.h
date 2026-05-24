//--- K.I.LIB ---
// kl_wnd.h : window information manager

#ifndef AFX_KIWINDOW_H__26105B94_1E36_42FA_8916_C2F7FB9EF994__INCLUDED_
#define AFX_KIWINDOW_H__26105B94_1E36_42FA_8916_C2F7FB9EF994__INCLUDED_

/*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*/
// kiWindow : Simple management of Window

class kiWindow
{
friend void kilib_startUp();

private: //-- Global initialization etc. ---------------------

	static void init();
	static void finish();

public: //-- Public interface --------------------------

	// Associated HWND
	HWND hwnd()
		{
			return m_hWnd;
		}

	// Load accelerator for the window
	void loadAccel( UINT id );

	// Check whether the window is still alive
	bool isAlive()
		{
			if( !m_hWnd )
				return false;
			if( ::IsWindow(m_hWnd) )
				return true;
			m_hWnd = NULL;
			return false;
		}

	// Parent
	kiWindow* parent()
		{
			return kiwnd( ::GetParent( hwnd() ) );
		}

	// Send message
	LRESULT sendMsg( UINT msg, WPARAM wp=0, LPARAM lp=0 )
		{
			return ::SendMessage( hwnd(), msg, wp, lp );
		}

	// [static] Process all queued messages
	static void msg();

	// [static] Run the message loop.
	static void msgLoop();

	// [static] Force window to front
	static void setFront( HWND wnd );

	// [static] Center the window
	static void setCenter( HWND wnd, HWND rel=NULL );

	// [static] HWND -> kiWindow (if any)
	static kiWindow* kiwnd( HWND wnd )
		{
			kiWindow* ptr = (kiWindow*)::GetWindowLongPtr( wnd, GWLP_USERDATA );
			if( !ptr ) return NULL;
			return ptr;
		}

protected: //-- For derived classes -----------------------------

	// Derived classes must call this just before creation.
	static void preCreate( kiWindow* wnd )
		{ st_pCurInit = wnd; }
	// Call this just before destruction.
	void detachHwnd();
	// Temporarily stop the GET/POST message loop
	static void loopbreak()
		{
			loopbreaker = true;
		}

private: //-- Internal processing -------------------------------------

	// Set window handle
	static LRESULT CALLBACK CBTProc( int code, WPARAM wp, LPARAM lp );
	static HHOOK st_hHook;
	static kiWindow* st_pCurInit;
	void setHwnd( HWND wnd )
		{
			m_hWnd = wnd;
		}

	// Variable for holding window info
	HWND m_hWnd;
	HACCEL m_hAccel;
	// Dialog message
	virtual bool isDlgMsg( MSG* msg )
		{ return false; }
	// Temporarily exit GET loop
	static bool loopbreaker;

protected:
	kiWindow();
public:
	virtual ~kiWindow();
};

/*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*/
// kiDialog : Manage Dialog as kiWindow

class kiDialog : public kiWindow
{
public: //-- Public interface --------------------------

	// Execute as modal dialog
	virtual void doModal( HWND parent=NULL );

	// Create as modeless dialog
	virtual void createModeless( HWND parent=NULL );

	// Get exit code
	UINT getEndCode()
		{
			return m_EndCode;
		}

	// Whether modal
	bool isModal()
		{
			return m_bStateModal;
		}

	// Dialog item
	LRESULT sendMsgToItem( UINT id, UINT msg, WPARAM wp=0, LPARAM lp=0 )
		{
			return ::SendDlgItemMessage( hwnd(), id, msg, wp, lp );
		}
	HWND item( UINT id )
		{
			return ::GetDlgItem( hwnd(), id );
		}

protected: //-- For derived classes -----------------------------

	// Initialize with resource ID
	kiDialog( UINT id );

	// Get resource ID
	UINT getRsrcID()
		{
			return m_Rsrc;
		}

	// Set exit code
	void setEndCode( UINT endcode )
		{
			m_EndCode = endcode;
		}

	// Toggle only the modal flag
	void setState( bool modal )
		{
			m_bStateModal = modal;
		}

	// Set exit code and end ( Note: passing IDOK does NOT call onOK()! )
	virtual void end( UINT endcode );

	// Called when a command/message occurs

		// OK -> onOK     -> if true end(IDOK)
		virtual bool onOK() {return true;}
		// Cancel -> onCancel -> if true end(IDCANCEL)
		virtual bool onCancel() {return true;}
		// WM_INITDIALOG      -> onInit
		virtual BOOL onInit() {return FALSE;}
		// WM_????            -> proc
		virtual BOOL CALLBACK proc( UINT msg, WPARAM wp, LPARAM lp ) {return FALSE;}

private: //-- Internal processing -------------------------------------

	UINT m_EndCode;
	UINT m_Rsrc;
	bool m_bStateModal;
	bool isDlgMsg( MSG* msg )
		{
			return (!!::IsDialogMessage( hwnd(), msg ));
		}
	static INT_PTR CALLBACK commonDlg( HWND dlg, UINT msg, WPARAM wp, LPARAM lp );
};

/*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*/
// kiPropSheet : Manage PropertySheet as kiWindow.

#define IDAPPLY      (0x3021)
#define ID_KIPS_HELP (0x0009)

class kiPropSheetPage : public kiDialog
{
friend class kiPropSheet;

protected: //-- For derived classes ----------------------------

	// Initialize with dialog or icon ID
	kiPropSheetPage( UINT dlgid )
		: kiDialog( dlgid ), m_hIcon( NULL ) {}
	void setIcon( HICON h )
		{ m_hIcon = h; }

	// OK/Apply -> page::onOK -> sheet::onOK -> (if ok, close)
	// virtual bool onOK()
	// WM_INITDIALOG
	// virtual BOOL onInit()
	// Other
	// virtual BOOL CALLBACK proc( UINT msg, WPARAM wp, LPARAM lp )

private: //-- Internal processing -------------------------------------

	void end( UINT endcode ) {}
	void setInfo( PROPSHEETPAGE* p );
	HICON m_hIcon;
};

class kiPropSheet : public kiDialog
{
friend class kiPropSheetPage;

public: //-- Public interface --------------------------

	// Execute as modal dialog
	void doModal( HWND parent );

	// Create as modeless dialog
	void createModeless( HWND parent );

protected: //-- For derived classes ----------------------------

	// Modify the fields below near the constructor
	PROPSHEETHEADER m_Header;
	kiArray<kiPropSheetPage*> m_Pages;

	// End
	void end( UINT endcode );
	// 
	void sendOK2All()
	{
		for( unsigned int i=0;i!=m_Pages.len(); i++ )
			if( m_Pages[i]->isAlive() )
				m_Pages[i]->onOK();
	}

	// OK/Apply -> page::onOK -> sheet::onOK -> (if ok, close)
	// virtual void onOK()
	// Cancel -> sheet::onCancel -> end
	// virtual void onCancel()
	// PSCB_INITIALIZED
	// virtual BOOL onInit()
	// Apply
	virtual void onApply() {}
	// Help
	virtual void onHelp() {}
	// Other commands
	virtual void onCommand( UINT id ) {}
	// File drop
	virtual void onDrop( HDROP hdrop ) {}

private: //-- Internal processing ---------------------------------------

	void begin();
	bool m_bStateModal;

	static kiPropSheet* st_CurInitPS;
	WNDPROC m_DefProc;
	bool isDlgMsg( MSG* msg )
		{ return !!PropSheet_IsDialogMessage( hwnd(),msg ); }
	static int CALLBACK main_initProc( HWND dlg, UINT msg, LPARAM lp );
	static LRESULT CALLBACK main_cmmnProc( HWND dlg, UINT msg, WPARAM wp, LPARAM lp );
	static INT_PTR CALLBACK page_cmmnProc( HWND dlg, UINT msg, WPARAM wp, LPARAM lp );
	static UINT CALLBACK page_initProc( HWND dlg, UINT msg, LPPROPSHEETPAGE ppsp );

protected:
	kiPropSheet();
public:
	~kiPropSheet()
		{ for( unsigned int i=0; i!=m_Pages.len(); i++ ) delete m_Pages[i]; }
};

/*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*/
// kiListView : Simple wrapper for ListView control

class kiListView
{
public:
	kiListView( kiDialog* dlg, UINT id )
		{
			m_hWnd = ::GetDlgItem( dlg->hwnd(), id );
		}

	void insertColumn( int y, const char* title,
						int width=100, int fmt=LVCFMT_LEFT )
		{
			LVCOLUMN col;
			col.mask = LVCF_TEXT | LVCF_FMT | LVCF_WIDTH;
			col.pszText = const_cast<char*>(title);
			col.cx = width;
			col.fmt = fmt;
			::SendMessage( m_hWnd, LVM_INSERTCOLUMN, y, (LPARAM)&col );
		}

	void insertItem( int x, const char* str, LPARAM param=0, int iImage=-1 )
		{
			LVITEM item;
			item.mask = LVIF_TEXT | LVIF_PARAM | (iImage!=-1 ? LVIF_IMAGE : 0);
			item.pszText = const_cast<char*>(str);
			item.iItem = x;
			item.iSubItem = 0;
			item.iImage = iImage;
			item.lParam = param; 
			::SendMessage( m_hWnd, LVM_INSERTITEM, 0, (LPARAM)&item );
		}

	void setSubItem( int x, int y, const char* str )
		{
			LVITEM item;
			item.mask = LVIF_TEXT;
			item.pszText = const_cast<char*>(str);
			item.iItem = x;
			item.iSubItem = y;
			::SendMessage( m_hWnd, LVM_SETITEM, 0, (LPARAM)&item );
		}

	void setImageList( HIMAGELIST Large, HIMAGELIST Small )
		{
			::SendMessage( m_hWnd, LVM_SETIMAGELIST, LVSIL_NORMAL, (LPARAM)Large );
			::SendMessage( m_hWnd, LVM_SETIMAGELIST, LVSIL_SMALL,  (LPARAM)Small );
		}

private:
	HWND m_hWnd;
};

#endif
