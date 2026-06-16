# kilib モダン化 設計メモ（wide 化）

ブランチ: `refactor/kilib-wide-modernization`

AileFlow の `kilib/`（Noah / K.I.LIB 由来、90年代末〜2000年代初頭の Win32 C++）を
モダン化する長期作業の設計ドキュメント。本ファイルは **ステップ1（依存スコープ調査）** の成果物。

## 目的とライセンス前提

- **目的**: 古い narrow(ANSI/CP_ACP) ベースの kilib をモダン C++ + **wide(UNICODE)** へ移行する。
  副次効果として `kl_str.cpp` の `IsDBCSLeadByte` 系 DBCS 先行バイト処理が消滅し、
  非 ANSI ファイル名のロス（現状 `GetShortPathName` による 8.3 名回避策）も解消する。
- **ライセンス**: Noah/kilib は **CC0 1.0**（完全パブリックドメイン）。改変・全面書き換え・
  再ライセンス・帰属省略すべて自由。法的障害なし。ただし `.b2e` が呼ぶ外部実行ファイル
  （7z.exe / WinRAR.exe 等）は各自ライセンスで別判断。

## 戦略: B2eBridge を strangler の足場にする

`B2eBridge`（`src/B2eBridge.cpp/h`）は「kilib 型を一切外に漏らさず Win32/std 型だけを公開する」
境界。UI 層（`SevenZipB2e.cpp` 以上）は kilib を知らない。したがって **UI を一切触らず、
ブリッジの裏側のエンジンを段階置換できる**（Strangler Fig）。一気に倒さず、各段階でビルドと
回帰テストが通る状態を保つ。最終的に kilib が空になったらブリッジを passthrough 化 → 撤去。

> 注: ブリッジ自身は既に STL（std::map/set/string/vector）を使用している。
> CMakeLists の `# no STL` コメントは実態と不一致。wide 化の障害は STL の有無ではなく
> narrow(char/ANSI) vs wide(wchar_t/UNICODE) の幅の違い。

## kilib ファイル仕分け（依存スコープ調査結果）

到達起点: `CArcB2e` → `CArchiver` → `kiRythpVM`、および `Archiver.cpp` の外部ツール実行。
（`src kilib` 全 .cpp/.h を対象に各シンボルの外部参照数を計測）

| ファイル | LOC | 主シンボル | 判定 | 対応 |
|---|---:|---|---|---|
| `kl_dnd.cpp` | 217 | `kiDataObject` `kiDropSource` | **死にコード** | `kilib.h` の include 除去＋ビルドから削除 |
| `kl_reg.cpp` | 64 | `kiIniFile` | **死にコード**（外部参照0） | 削除候補 |
| `kl_wnd.cpp` | 438 | `kiWindow` `kiDialog` `kiListView` `kiPropSheet` | **大半が死にコード** | 使われるのは静的 `kiWindow::msg()/init()/finish()` のみ（メッセージポンプ）。小さなポンプヘルパに置換し残り ~400 LOC を削除 |
| `kl_app.cpp` | 89 | `kiApp` | 必要（最小） | `AileFlowKiApp` が継承。`kiWindow::init/finish` を呼ぶ。wide 化 |
| `kl_cmd.cpp` | 79 | `kiCmdParser` | 要精査 | `kiApp::run()` は no-op stub。`CharArray`(kiArray<char*>) 経由で ArcB2e が使用。縮小可能性あり |
| `kl_wcmn.cpp` | 146 | `kiSUtil` | 必要 | ArcB2e/Archiver/kl_str が使用。wide 化 |
| `kl_file.cpp` | 118 | （ファイル操作） | 必要 | W API 化 |
| `kl_find.cpp` | 67 | `kiFindFile` | 必要 | W API 化（`FindFirstFileW` 等） |
| `kl_str.cpp` | 375 | `kiStr` `kiPath` | **コア** | wide 化。`IsDBCSLeadByte`/`st_lb[]` 消滅。`kiStr`→ wchar 内部 |
| `kl_rythp.cpp` | 405 | `kiVar`(:public kiStr) `kiRythpVM` | **コア・最難関** | wide 化を最後に。`.b2e` 解釈の心臓部。要回帰テスト |
| `kl_misc.h` | (h) | `kiArray<T>` `StrArray` `CharArray` | コンテナ | ヘッダのみ。`std::vector` 化 or 維持。`CharArray=kiArray<char*>` は wide 化で要見直し |

**先行して削除できる量**: `kl_dnd`(217) + `kl_reg`(64) + `kl_wnd` の約400 ≈ **~680 LOC（kilib 全体 ~2000 の約34%）**。
wide 化の本作業に入る前に、ここを落とすだけで対象が ~2000 → ~1300 LOC に縮む。

## 内部エンコーディング方式: UTF-16 全面化（2026-06-16 決定）

`char`(UTF-8) 維持案ではなく **UTF-16（`wchar_t`）全面化** を採用。型レベルで“真の wide”となり
UI 層の `std::wstring` と型が揃うのが利点。代償として `kiStr`→`wchar_t` の flip が
`kiPath`/`kiVar`/`kiRythpVM`/`CharArray`/`.b2e` 読み込みまで波及する大工事になる。
Rythp VM は元来 char バッファのテキストエンジン（`eval(char*)`/`split`/`CharArray=kiArray<char*>`）
なので、これらは**一斉に flip する必要があり**小さなコミットに割りづらい。
→ フリップ前に回帰テストの安全網を用意し、まとまった単位で landing する。

**UTF-16 フリップで同時に動く範囲（一括コミット候補）**:
- `kiStr`/`kiPath`/`kiVar`: 内部バッファ `char*`→`wchar_t*`、`st_lb[]`/`IsDBCSLeadByte` 撤去
- `kiRythpVM`: `eval`/`getarg`/`split`/`CharArray` を wide 化
- `.b2e` 読み込み（`B2eScript.cpp`）: load 時に UTF-8/ANSI→UTF-16 変換
- `Archiver.cpp` プロセス I/O: `CreateProcessW` + stdout を CP 指定で UTF-16 復号
- `B2eBridge`: CP_ACP 変換群を撤去し wstring パススルー化（境界が消える方向）

**先行/独立にできる作業**: 安全網（B2eBridge 公開 API を叩く小テスト）は flip と独立に先行可。
`kl_file`/`kl_find`/`kl_wcmn` の W API 化は `kiStr` wide 化と同時か直後。

## UTF-16 フリップ 実行計画（精密版・コア読込済み）

`kl_str.cpp`/`kl_rythp.cpp`/`kl_misc.h`/`kilibext.h` を精読した上での実行手順。
対象 ~3,490 行は密結合かつ VM が buffer を in-place 書換するため **原子的に一括** で倒し、
ハーネス（gate 16 + カナリア2本）で検証してから 1 コミットする。

### 鍵となるレバー: per-file `/UUNICODE` の除去

`ki_strlen/strcpy/strcmp/strcmpi` は `::lstrlen` 等のマクロ（`kl_misc.h`）で、`UNICODE` 定義の
有無で A/W が自動切替。同様に kilib 内の Win32 呼び出し（`GetModuleFileName`/`GetTempPath`/
`CreateProcess`/`FindFirstFile`/`CreateDirectory`/`LoadString`/`GetShortPathName`/
`SHGetPathFromIDList`/`GetDriveType` …）も A/W マクロ。
→ **`CMakeLists.txt` の `KILIB_B2E_SOURCES` の `COMPILE_OPTIONS` から `/UUNICODE;/U_UNICODE` を外す**
   だけで、これらが**一括で W 版に切替わる**。手作業は型と文字リテラルとバイト数に集中できる。

### ⚠ 最大のバグ源: mem 系のバイト数

`ki_memcpy/memmov/memzero` は `CopyMemory/MoveMemory/ZeroMemory`（**バイト単位**）。
現状 `slen = ki_strlen(s)+1`（文字数）をそのまま渡している箇所が多数（`kl_str.cpp` の
ctor/operator=/operator+=、`kl_rythp.cpp` の quote/unquote 等）。wchar 化後は
**`* sizeof(wchar_t)` を掛ける**必要がある。`new char[n]`→`new wchar_t[n]` も同様。
ここを機械的に潰すのが品質の要。

### 手順（順序）

1. **コンテナ/型**: `kl_misc.h` の `CharArray=kiArray<char*>`→`kiArray<wchar_t*>`、
   `cCharArray` 同様。`kl_carc.h` の `INDIVIDUALINFO::szFileName[]` を `wchar_t` 化。
2. **kiStr/kiPath**（`kl_str.h/.cpp`）: `m_pBuf` を `wchar_t*`、全 `char`→`wchar_t`、
   リテラル→`L""`、`next(p)`/`isLeadByte`/`st_lb[]` 撤去（wchar は固定幅 ⇒ `++p`）、
   mem 系にバイト換算。`standalone_init`/`init` は不要化（DBCS テーブル消滅）。
3. **kiVar/kiRythpVM**（`kl_rythp.cpp`）: 同様に wchar 化。`ele[256]` は `(*p)&0xff`
   のままで可（`.b2e` 変数名は ASCII）。`split`/`getarg`/`eval` は ASCII デリミタ走査なので
   型・リテラル・mem 換算のみ。
4. **.b2e 読込**（`B2eScript.cpp`）: ファイルバイト（ANSI/UTF-8）を読み、
   `MultiByteToWideChar` で `wchar_t` バッファへ変換してからセクション分割（キーワードは ASCII）。
5. **Archiver.cpp**: `CreateProcess` は cmdline が `wchar_t*` になり自動 W 化。
   **stdout は `ReadFile` で生バイト**なので、ここだけ非機械的: ツール出力 CP を決めて
   `MultiByteToWideChar` で `wchar_t` 化してから VM に渡す（7-Zip は `-scc` で UTF-8 強制可、
   既定は OEM/コンソール CP）。`.b2e` に CP ヒントを持たせる設計に接続。
6. **ArcB2e.cpp**: kiStr/kiPath/kiVar/CharArray を使う全箇所が wchar 化に追従。
   コマンド組立の文字リテラル→`L""`。
7. **B2eBridge.cpp**: CP_ACP 変換群（`WToA`/`AToWString`/`WideFsPathToAnsiPath` の
   短名回避）を**撤去**し wstring パススルー化。`szFileName`(wchar) を直接 `std::wstring` へ。
   → ここで**ブリッジが大幅に縮む**（境界が消える方向）。
8. **kl_app.h**: `msgBox(const char*)`→`wchar`、`log`→`wchar`（軽微）。

### 緑ゲート（検証）

`cmake --build build --target AileFlowHarness` → 実行。
- gate 16 が PASS のまま（既存機能の非回帰）
- **カナリア**: `listed entry present: 日本語_😀.txt` と
  `selective-extract locate/round-trip: 日本語_😀.txt` が **WARN→PASS** に転じれば成功。
- AileFlow.exe 本体でも起動・圧縮・解凍のスモーク（ユーザー検証）。

## フリップ進捗（2026-06-16）

**✅ マイルストーン達成: 全面 UTF-16 フリップが「コンパイル＋ASCII ゲート緑」に到達。**
kilib コア層＋エンジン層（`Archiver`/`ArcB2e`/`B2eScript`/`B2eBridge`/`AileFlowApp`）を
すべて `wchar_t` 化。AileFlow（Debug/Release）・ハーネスともビルド成功し、ハーネスの
gate 16 項目 PASS / 0 fail。コマンドラインは `CreateProcessW` で可逆化。境界（stdout 復号・
レスポンスファイル）は当面 CP_ACP のままなので非 ASCII カナリア（`日本語_😀.txt` の
listing / selective-extract）は WARN 据置＝非回帰。

**最終段（残・別作業）= 非 ASCII カナリア WARN→PASS**: 境界 CP を CP_ACP→UTF-8 へ。
`Archiver` の stdout 復号（`lst_exe`/`tst_exe`）と `resp` 書き出しを UTF-8 化し、`.b2e` の
list/test/resp コマンドに 7-Zip の `-scc`/`-scs`（UTF-8）スイッチを付与。

### ▶ 次回の最初の一手（resume here）

現状: `refactor/kilib-wide-modernization @ 409add7`、ビルド緑・実機スモーク合格。
最終段の具体手順:
1. `Archiver.cpp` の `lst_exe`/`tst_exe`: stdout 復号の `MultiByteToWideChar(CP_ACP, …)` を
   `CP_UTF8` に変更（2 箇所）。
2. `ArcB2e.cpp` の `resp`: `writeAnsi` ラムダの `WideCharToMultiByte(CP_ACP, …)` を `CP_UTF8` に。
3. `.b2e` スクリプト（まず `7z.b2e` / `zip.zipx.b2e`）の list/test/encode コマンドに 7-Zip の
   `-sccUTF-8`（コンソール出力）/`-scsUTF-8`（リスト/レスポンスファイル）を付与。
   ※どのフォーマットまで対応するかは作業前にユーザーと方針確認。
4. 検証: `cmake --build build --target AileFlowHarness` → 実行し、
   `listed entry present: 日本語_😀.txt` と `selective-extract locate: 日本語_😀.txt` が
   **WARN→PASS** になること。ASCII ゲート 16 は緑維持。
5. GUI 実機で日本語名アーカイブの一覧・展開を確認。

注意: 7-Zip 版により `-scc/-scs` の対応可否・既定挙動が異なるため、まず 7z 単体で挙動確認すると安全。
[[既存]] のハーネスがそのまま回帰ゲートになる。

以下は作業当時のコア層メモ（履歴）。

**完了（kilib コア）**:
- `CMakeLists.txt`: `KILIB_B2E_SOURCES` から `/UUNICODE;/U_UNICODE` 除去（W 一括切替レバー）
- `kl_misc.h`: `CharArray`/`cCharArray` を `wchar_t*`、`ki_memcmp` を `const wchar_t*`
- `kl_carc.h`: `INDIVIDUALINFO::szFileName/szAttribute/szMode` を `wchar_t`（`dummy1` 削除）
- `kl_str.h/.cpp`: `kiStr`/`kiPath` 全面 wide。`next()`=`p+1`、`st_lb`/DBCS 撤去、`standalone_init` no-op、
  mem 系は `WB(n)=n*sizeof(wchar_t)` でバイト換算、Win32 を W 明示
- `kl_rythp.h/.cpp`: `kiVar`/`kiRythpVM` 全面 wide（`eval`/`split`/`getarg`、`ele[(*p)&0xff]` 維持）
- `kl_file.h/.cpp`・`kl_find.h/.cpp`・`kl_wcmn.h/.cpp`・`kl_cmd.h/.cpp`: W API 化
- `kl_app.h`: `msgBox` を wide

**残作業（エンジン層）= 次セッション**:
- `Archiver.h`: `arcname`(lname/sname)・`CArcModule`(m_name)・`CArchiver`(mext)・`arcfile.rawline` を wchar
- `Archiver.cpp`:
  - `cmd`/`lst_exe`/`tst_exe`: `theCmd` は wide → `CreateProcessW`（cmdline 可逆化）
  - **方針(暫定)**: stdout は全バイトを溜めて `MultiByteToWideChar(CP_ACP)` で一括 wide 化し、
    既存の行解析を wide で実行（`BL`/`EL` wide、`szFileName` へ直接 wide 代入）。
    レスポンスファイル(`resp`)は当面 CP_ACP バイトで書く（現状維持）。
    → これで**コンパイル＋ASCII ゲート緑**を達成。非 ASCII カナリアは WARN 据置（非回帰）。
  - `GetVersionInfoStr` も wide 化
- `ArcB2e.h/.cpp`: `st_base`/`init_b2e_path`/`CArcB2e(ctor)`、`CB2eCore` の `arc/list/resp/input/inputpw`
  を wide、リテラル `L""`、`ki_memcmp((const wchar_t*)name, L"...")`、`input` 系は既に wide ダイアログ
  なので `b2e_input_impl` の MB↔WC 変換を撤去して簡素化
- `B2eScript.h/.cpp`: `B2eSections` を `wchar_t*`、`B2e_LoadAndPreprocessScriptFile` は ANSI/UTF-8
  ファイルを `MultiByteToWideChar` で wide バッファ化
- `B2eBridge.cpp`: CP_ACP 変換群（`WToA`/`AToWString`/`WideFsPathToAnsiPath` 短名回避）を撤去し
  wstring パススルー化（**大幅縮小**）。`.b2e` スキャンの char 解析を wide 化
- `AileFlowApp.h`(`get_tempdir`)・`AileFlowKiLib.cpp`(`init_b2e_path` 戻り値 wide)

**最終段（非 ASCII カナリア WARN→PASS, さらに後続）**: 境界 CP を CP_ACP→UTF-8 へ。
`Archiver` の stdout 復号と `resp` 書き出しを UTF-8 化し、`.b2e` の list/test/resp コマンドに
7-Zip の `-scc`/`-scs`（UTF-8）スイッチを付与。これでハーネスのカナリア2本が PASS に転じる。

## 外部ツール I/O 境界（wide 化の本丸）

`src/Archiver.cpp` に集約。ここが narrow の最終境界。

- コマンドライン生成: `CreateProcess(NULL, (char*)theCmd, ...)`（行 ~64 / ~138 / ~320）
  → `CreateProcessW` + wide コマンドラインへ。非 ANSI ファイル名を渡せるようになる。
- stdout 取り込み: `CreatePipe`（行 ~129 / ~312）+ `ReadFile(rp, char buf, ...)`（行 ~173 / ~347）
  → 子プロセスが吐く生バイトを `char` で受け、Rythp VM がパースしている。
  **ここが本質的に厄介**: 出力エンコーディングを決めるのは外部ツール側。

### stdout 復号の方針

ツール出力 CP を決めて `MultiByteToWideChar` で wide 化してから VM に渡す。レバー:

- **7-Zip 系**: `-scc`/`-scs`（コンソール文字コード）スイッチで UTF-8 出力を強制。
- 子コンソール出力 CP の制御（環境 / `chcp 65001` 相当）。
- 制御不能なツール: OEM/コンソール CP として `OEM→wide` 変換にフォールバック。

→ **`.b2e` にコードページヒントを持たせ、パイプ境界で参照して復号する**設計に収束させる。
   これで残存 narrow ポイントが「指定 CP で stdout を復号する1関数」だけになる。

## ⚠ 注意: kl_app.cpp の load-bearing スキャフォールディング

`kl_app.cpp` は **グローバル `operator new/delete`（`new[]`/`delete[]` 含む）を
`GlobalAlloc`/`GlobalFree` で置き換えている**。これはプロセス全体に効くため、UI 層・
ブリッジの STL アロケーションもこの経路を通る可能性がある。`__cxa_pure_virtual` と
ダミー `int main()` も同様にリンク時スキャフォールディング。**死にコード削除では触れず温存**。
このアロケータ差し替えの撤去は単独で切り出し、十分なテストを伴って行うこと（高リスク）。

## 進行順（ロードマップ）

1. ✅ **依存スコープ調査**（本メモ）
2. ✅ **死にコード削除**: `kl_dnd`(217) / `kl_reg`(64) を撤去（commit `be4157b`）。
   `kl_wnd`(438) を撤去し `kiWindow::msg()` を `Archiver.cpp` のローカル `pump_thread_messages()`
   に退避、`kiApp` の `kiWindow*` 依存を `HWND` 直持ちに置換、死んだ `kilib_startUp()` を削除。
   Debug/Release 両方リンク成功。**合計 ~719 LOC 削減**。
3. **OS ラッパ層の W API 化**: `kl_file` / `kl_find` / `kl_wcmn`。
4. **文字列/コンテナ wide 化**: `kiStr`/`kiPath`→ wchar 内部、`kiArray`/`StrArray`/`CharArray`→ std へ。
5. **プロセス I/O の wide 化**: `Archiver.cpp` を `CreateProcessW` + stdout CP 復号へ。
6. **Rythp VM wide 化**: `kl_rythp`（`kiVar`/`kiRythpVM`）。最後・最重点・回帰テスト必須。
7. **ブリッジ撤去**: `B2eBridge` を passthrough 化 → 削除、`SevenZipB2e` から直接呼び出し。

## 回帰テストの担保（各ステップ共通）

UI がモーダルで自動化しづらいため、`B2eBridge` の公開 API（`B2e_List`/`B2e_Extract`/
`B2e_Compress`/`B2e_Test`）を叩くヘッドレスハーネスを用意した。

- 実体: `tests/harness.cpp`、CMake ターゲット `AileFlowHarness`（`EXCLUDE_FROM_ALL`・
  WIN32/wWinMain で `kl_app.cpp` のダミー `main()` と衝突回避）。
- 実行: `cmake --build build --target AileFlowHarness` → `build/aileflow/AileFlowHarness.exe`。
  結果は `%TEMP%\aileflow_harness_result.txt` ＋ AllocConsole。終了コード 0=gate 合格。
- 内容: 一時ディレクトリで 7z / zip について Compress→List→Test→Extract のラウンドトリップ。
  各ケースに **ASCII 名（gate）** と **非 ASCII 名（日本語＋絵文字, baseline）** を含む。
- 7z.exe は `CArcModule` の検索順（exe→bin\→PATH）でシステム 7-Zip が見つかるため dev ビルドで実行可。

### フリップ前ベースライン（2026-06-16, commit 段階）

| ケース | 結果 | 意味 |
|---|---|---|
| ASCII 全項目（7z/zip） | PASS | 既存機能の健全性 |
| 非 ASCII **解凍内容** | PASS | 全解凍は `7z.exe` が Unicode 名で書くため成功 |
| 非 ASCII **一覧表示** | **WARN** | kilib が stdout を CP_ACP 復号 → 名前が化ける（narrow 境界） |

→ **UTF-16 フリップ成功の判定 = 「listed entry present: 日本語_😀.txt」が WARN→PASS**。
   選択解凍（index→`WToA`）も同様に wide 化で改善されるはず（現ハーネスは全解凍のみ検証。
   将来 wide 経路の確証を強めるなら選択解凍ケースの追加を検討）。
