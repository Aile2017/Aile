==============================================================================
 Aile - Archive Manager for Windows
==============================================================================

Aile is a lightweight archive manager GUI for Windows. It reads and writes
all common archive formats through the bundled 7-Zip ZS engine (7z, zip,
tar, gz, xz, zstd, ...) and can drive external archiver tools such as
WinRAR through B2E scripts for formats the engine cannot write (e.g. RAR).

Project page:  https://github.com/Aile2017/Aile


Requirements
------------
  - Windows 10 / 11 (x64)
  - No installation, no runtime dependencies. Everything needed is in
    this folder.


Installation
------------
Aile is portable:

  1. Extract this ZIP anywhere you like (e.g. C:\Tools\Aile).
  2. Run Aile.exe.

Settings are stored in Aile.ini next to Aile.exe. Nothing is written to
the registry unless you register the optional Explorer context menu with
AileSetup.exe (see docs\shell-extension.txt).

To uninstall: unregister the shell extension with AileSetup.exe (if you
registered it), then delete the folder.


Quick start
-----------
  - Drop an archive onto the Aile window (or use File > Open) to browse it.
  - Press F5 to extract; select entries first to extract only those.
  - Drop regular files onto the window to compress them.
  - Ctrl+T tests archive integrity without extracting.

Full instructions are in the docs\ folder:

  docs\manual.txt           GUI manual (browse, extract, compress, settings)
  docs\cli.txt              Command-line usage (a / x / w / t actions)
  docs\b2e.txt              RAR support and other external archiver tools
  docs\shell-extension.txt  Explorer right-click menu setup
  docs\faq.txt              FAQ and troubleshooting


Package contents
----------------
  Aile.exe           Application
  README.txt         This file
  docs\              User documentation
  b2e\               B2E scripts for external archiver tools
  AileShell.dll      Explorer context-menu handler (64-bit hosts)
  AileShell32.dll    Explorer context-menu handler (32-bit file managers)
  AileSetup.exe      Registers / unregisters the context-menu handler
  bin\7z.dll         7-Zip ZS archive engine
  bin\7z.sfx         Self-extractor stub (GUI)
  bin\7zCon.sfx      Self-extractor stub (console)
  bin\License.txt    7-Zip license


License and credits
-------------------
  - The bundled archive engine is 7-Zip ZS (7-Zip with Zstandard support).
    See bin\License.txt for its license.
  - The .b2e archive handler scripts are based on scripts from Noah, a
    script-driven archive manager by Kazuhiro Inaba
    (https://www.kmonos.net/lib/noah.ja.html).
  - Application icon: "Paperplane" from icon-icons.com, used under its
    free-for-commercial-use license.
