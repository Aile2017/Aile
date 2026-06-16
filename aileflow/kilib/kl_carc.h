//--- K.I.LIB ---
// kl_carc.h : INDIVIDUALINFO from the common archiver DLL interface

#ifndef AFX_KIARCDLLRAW_H__C94DE2A0_4292_49CE_8471_2CAA1340D216__INCLUDED_
#define AFX_KIARCDLLRAW_H__C94DE2A0_4292_49CE_8471_2CAA1340D216__INCLUDED_

// FNAME_MAX
#if !defined(FNAME_MAX32)
#define FNAME_MAX32	512
#define	FNAME_MAX	FNAME_MAX32
#else
#if !defined(FNAME_MAX)
#define	FNAME_MAX	128
#endif
#endif

#if defined(__BORLANDC__)
#pragma option -a-
#else
#pragma pack(1)
#endif

typedef struct {
	DWORD	dwOriginalSize;
	DWORD	dwCompressedSize;
	DWORD	dwCRC;
	UINT	uFlag;
	UINT	uOSType;
	WORD	wRatio;
	WORD	wDate;
	WORD	wTime;
	wchar_t	szFileName[FNAME_MAX32 + 1];
	wchar_t	szAttribute[8];
	wchar_t	szMode[8];
} INDIVIDUALINFO, FAR *LPINDIVIDUALINFO;

#if !defined(__BORLANDC__)
#pragma pack()
#else
#pragma option -a.
#endif

#endif
