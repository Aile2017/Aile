#pragma once

#ifndef IDC_STATIC
#define IDC_STATIC (-1)
#endif

// Icons
#define IDI_AILEEX          101

// Toolbar bitmaps
#define IDB_TOOLBAR_EXTRACT 111
#define IDB_TOOLBAR_OPEN    112
#define IDB_TOOLBAR_ADD     113
#define IDB_TOOLBAR_SETTINGS 115
#define IDB_TOOLBAR_TEST     116

// Dialogs
#define IDD_COMPRESS        201
#define IDD_PROGRESS        202
#define IDD_SETTINGS        203
#define IDD_PASSWORD        207
#define IDD_ABOUT           208
#define IDD_TEST_RESULT     209
#define IDD_INPUT           210

// Compress dialog controls
#define IDC_OUTPUT_PATH     1001
#define IDC_BROWSE          1002
#define IDC_FORMAT          1003
#define IDC_METHOD          1005
#define IDC_CREATE_SFX      1006

// Progress dialog controls
#define IDC_PROGRESS_BAR    2001
#define IDC_PROGRESS_FILE   2002
#define IDC_CANCEL          2003
#define IDC_ELAPSED         2004

// Settings dialog controls
#define IDC_DEFAULT_DIR     3002
#define IDC_BROWSE_DIR      3003
// Extract subfolder creation policy (0=never / 1=single file / 2=multiple files / 3=always)
#define IDC_MKDIR_0         3010
#define IDC_MKDIR_1         3011
#define IDC_MKDIR_2         3012
#define IDC_MKDIR_3         3013
#define IDC_FONT_NAME       3014
// Output directory mode (radio buttons in the "Default output directory" group)
#define IDC_OUTDIR_SOURCE   3015   // Same as source file location
#define IDC_OUTDIR_FIXED    3016   // Use fixed DefaultOutputDir

// Font picker button (opens ChooseFont dialog)
#define IDC_BROWSE_FONT         3024

// Phase 1+2: Extraction behavior and general behavior controls
#define IDC_EXT_STRIP_ALL       3017  // Strip all known extensions (default)
#define IDC_EXT_STRIP_ONE       3018  // Strip one extension only
#define IDC_EXT_STRIP_KEEP      3019  // Keep all extensions
#define IDC_STRIP_TRAILING_NUM  3020  // Strip trailing digits/-/_/. from stem
#define IDC_COLLAPSE_SINGLE_DIR 3021  // Collapse single-subfolder output (break_ddir)
#define IDC_OPEN_FOLDER_AFTER   3023  // Open output folder after extraction

// Test result dialog controls
#define IDC_TEST_STATUS     9001
#define IDC_TEST_OUTPUT     9002

#define IDC_PASSWORD_INPUT  8001
#define IDC_INPUT_LABEL     8002

// About dialog controls
#define IDC_ABOUT_TITLE     8101
#define IDC_ABOUT_URL       8102
#define IDC_ABOUT_LIST      8103

// Toolbar / Menu commands
#define ID_EXTRACT          40001
#define ID_ADD              40002
#define ID_SETTINGS_DLG     40003
#define ID_REFRESH          40004
#define ID_TEST             40005
#define ID_DELETE           40006
#define ID_CLOSE            40007
#define ID_OPEN_ASSOC       40009
#define ID_EXTRACT_SELECTED 40016
#define ID_ADD_TO_CURRENT   40018  // Add files to the currently open archive
#define ID_EXTRACT_SMART    40020  // Toolbar extract: selected items if any, else all
#define ID_TOOLBAR_BROWSE_DEST 40021  // Browse button for toolbar extract-to path

// Menu-only commands (Phase 1 menubar)
#define IDM_FILE_OPEN       40010
#define IDM_FILE_EXIT       40011
#define IDM_FILE_MRU_PH     40012   // Recently used archives - "No history" placeholder
#define IDM_VIEW_TREE       40013   // Toggle tree view
#define IDM_HELP_ABOUT      40014
#define IDM_VIEW_TOOLBAR    40015   // Toggle toolbar
#define IDM_VIEW_ICONS      40022   // Toggle folder/file icons
#define IDM_VIEW_MENUBAR    40023   // Toggle menu bar

// Recently used archives - dynamic ID range (10 entries)
#define IDM_FILE_MRU_BASE   41000
#define IDM_FILE_MRU_LAST   41009

// Menu resource
#define IDR_MAIN_MENU       301

// Worker thread messages (WM_APP range)
#define WM_APP_PROGRESS     (WM_APP + 1)
#define WM_APP_DONE         (WM_APP + 2)

// =============================================================
// Localized strings (STRINGTABLE entries; loaded via I18n::Tr)
// English / Japanese 2 LANGUAGE blocks defined in res/AileEx.rc.
// =============================================================

// --- Common ---
#define IDS_APP_TITLE                   11001
#define IDS_ERR_INIT_FAILED             11002
#define IDS_ERR_7Z_NOT_LOADED           11003
#define IDS_CANCELLED                   11005
#define IDS_CANCELLING                  11006
#define IDS_DONE                        11007
#define IDS_ERROR_OCCURRED              11008
#define IDS_DASH                        11009
#define IDS_YES                         11010
#define IDS_NO                          11011

// --- Progress / Status ---
#define IDS_PROGRESS_EXTRACTING         11021
#define IDS_PROGRESS_DELETING           11023
#define IDS_PROGRESS_ADDING             11024
#define IDS_FMT_STATUS_ENTRIES          11026

// --- Column headers ---
#define IDS_COL_NAME                    11030
#define IDS_COL_SIZE                    11031
#define IDS_COL_PACKED                  11032
#define IDS_COL_TYPE                    11033
#define IDS_COL_MODIFIED                11034
#define IDS_COL_RATIO                   11038
#define IDS_TYPE_FOLDER                 11035
#define IDS_TYPE_FILE                   11036
#define IDS_FMT_TYPE_FILE_EXT           11037

// --- Toolbar tooltips ---
#define IDS_TIP_EXTRACT                 11040
#define IDS_TIP_VIEW                    11041
#define IDS_TIP_ADD                     11042
#define IDS_TIP_TEST                    11044
#define IDS_TIP_SETTINGS                11045

// --- Context menu ---
#define IDS_CTX_EXTRACT_SELECTED        11050
#define IDS_CTX_OPEN_ASSOC              11051
#define IDS_CTX_TEST                    11052
#define IDS_CTX_DELETE                  11054
#define IDS_CTX_EXTRACT                 11055

// --- File dialog filters / titles (filter strings use '|' as NUL sentinel) ---
#define IDS_FILTER_ARCHIVE              11060
#define IDS_FILTER_ALL_FILES            11061
#define IDS_TITLE_OPEN_ARCHIVE          11064
#define IDS_TITLE_SELECT_OUTPUT         11065
#define IDS_TITLE_SELECT_COMPRESS       11066
#define IDS_TITLE_SELECT_ADD            11067
#define IDS_TITLE_SELECT_DEFAULT_DIR    11071
#define IDS_TITLE_SELECT_DEST_FOLDER    11072

// --- MainWindow / general messages ---
#define IDS_ERR_OPEN_ARCHIVE            11080
#define IDS_INFO_SELECT_FILE            11083
#define IDS_INFO_FOLDERS_NOT_VIEWABLE   11084
#define IDS_ERR_EXTRACT_FILE_FAILED     11086
#define IDS_FMT_NO_ASSOC_APP            11087
#define IDS_ERR_EXTRACT_FAILED          11088
#define IDS_INFO_NO_FILES_SELECTED      11089
#define IDS_INFO_NO_ARCHIVE_TO_TEST     11090
#define IDS_FMT_FILE_NOT_FOUND          11093
#define IDS_MRU_NO_HISTORY              11094
#define IDS_OP_FAILED                   11095
#define IDS_ERR_ADD_FAILED              11096
#define IDS_FMT_DELETE_CONFIRM          11100
#define IDS_TITLE_DELETE_CONFIRM        11101
#define IDS_ERR_DELETE_FAILED           11102
#define IDS_ERR_COMPRESS_FAILED         11103
#define IDS_ERR_OP_NOT_SUPPORTED        11107
#define IDS_TITLE_TEST_RESULT           11112
#define IDS_TEST_PASSED                 11113
#define IDS_TEST_FAILED                 11114
#define IDS_ERR_TEST_NOT_SUPPORTED      11115

// --- Drag & Drop ---
#define IDS_FMT_DND_PROMPT              11110
#define IDS_DND_ELLIPSIS                11111

// --- CompressDlg ---
#define IDS_DEFAULT_SUFFIX              11119  // " (default)"
#define IDS_SFX_NONE                    11122
#define IDS_SFX_GUI                     11123
#define IDS_SFX_CONSOLE                 11124
#define IDS_LEVEL_0                     11125
#define IDS_LEVEL_1                     11126
#define IDS_LEVEL_3                     11127
#define IDS_LEVEL_5                     11128
#define IDS_LEVEL_7                     11129
#define IDS_LEVEL_9                     11130
#define IDS_INFO_SPECIFY_OUTPUT         11131

// --- FormatSize / Misc ---
#define IDS_FMT_SIZE_GB                 11250
#define IDS_FMT_SIZE_MB                 11251
#define IDS_FMT_SIZE_KB                 11252
#define IDS_FMT_SIZE_BYTES              11253
#define IDS_ABOUT_NOT_LOADED            11256

// --- Advanced Dialog (Compression settings) ---
#define IDS_ADV_AUTO                    11400  // "Auto"
#define IDS_ADV_NONE                    11401  // "None"
#define IDS_ADV_DEFAULT                 11402  // "Default"
#define IDS_ADV_NOT_SOLID               11403  // "Not solid"
