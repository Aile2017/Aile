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

## 進行順（ロードマップ）

1. ✅ **依存スコープ調査**（本メモ）
2. **死にコード削除**: `kl_dnd` / `kl_reg` / `kl_wnd` の未使用部分を撤去（`kiWindow::msg/init/finish`
   を最小ポンプヘルパに退避）。ビルド・全フォーマットの list/extract/compress/test が通ることを確認。
3. **OS ラッパ層の W API 化**: `kl_file` / `kl_find` / `kl_wcmn`。
4. **文字列/コンテナ wide 化**: `kiStr`/`kiPath`→ wchar 内部、`kiArray`/`StrArray`/`CharArray`→ std へ。
5. **プロセス I/O の wide 化**: `Archiver.cpp` を `CreateProcessW` + stdout CP 復号へ。
6. **Rythp VM wide 化**: `kl_rythp`（`kiVar`/`kiRythpVM`）。最後・最重点・回帰テスト必須。
7. **ブリッジ撤去**: `B2eBridge` を passthrough 化 → 削除、`SevenZipB2e` から直接呼び出し。

## 回帰テストの担保（各ステップ共通）

UI がモーダルで自動化しづらいため、`B2eBridge` の公開 API（`B2e_List`/`B2e_Extract`/
`B2e_Compress`/`B2e_Test`/`B2e_Delete`/`B2e_GetWritableFormats`）を叩く小さなテストハーネスを
用意し、代表フォーマット（7z / zip / tar.gz / rar など）で list→extract→compress→test の
往復が wide 化前後で一致することを確認するのが安全。
