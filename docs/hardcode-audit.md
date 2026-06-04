# AileEx ハードコード箇所 監査結果

調査日: 2026-06-04  
対象コミット: 95bce64  
実施完了: 2026-06-04（コミット 2dc46f5）

## 背景

tar-in-stream 透過展開の動的 DLL 判定化対応（IsStreamExt / IsStreamFormat）の過程で、
DLL の実際の対応状況を見ずに固定リストで判定している箇所が複数あることが判明したため、
全体を調査した。

---

## 🔴 本来動的であるべきなのに固定（修正候補）

### 1. `SevenZip.cpp` — m_extToClsid 空時のフォールバック拡張子リスト ✅ 完了

- **対応内容**: `kFallback[]` に `zst`/`lz4`/`lz5`/`br`/`liz` を追加。`IsStreamExt()` の `kStreamExts[]` と内容を統一。

---

### 2. `SevenZip.cpp` — 拡張子→CLSID の固定マッピング ✅ 完了

- **対応内容**: ZS版 CLSID 定数を実機列挙（`GetNumberOfFormats`/`GetHandlerProperty2`）で確認し `sdk/7zip/Archive/IArchive.h` に追加。`FormatToInGuid` / `FormatToOutGuid` 両フォールバックに ZS形式の分岐を追加。
  - Zstd=0x0E / LZ4=0x0F / LZ5=0x10 / Lizard=0x11 / Brotli=0x1F

---

### 3. `MainWindow.cpp` — 展開先フォルダ名生成時の拡張子リスト ✅ 完了

- **対応内容**: `ArchiveBaseName()` の静的 `kExts[]` を削除。`SevenZip&` 参照を引数に追加し `sz.IsArchiveExt()` による動的判定に変更。

---

### 4. `res/AileEx.rc` — IDS_FILTER_ARCHIVE（ファイルを開くダイアログ） ✅ 完了

- **対応内容**: `SevenZip::GetExtensionFilterPattern()` を追加。`m_extToClsid` から `*.7z;*.zip;...` を動的生成。`MainWindow::OnFileOpen()` は DLL ロード済みなら動的パターンを使用、未ロード時は RC の文字列にフォールバック。

---

### 5. `CompressDlg.cpp` — DLL 未ロード時の圧縮形式フォールバック ✅ 完了

- **対応内容**: `kFallbackFormats[]` を削除。`Show()` 先頭で `writableFormats` が null/空なら即 `false` を返す。呼び出し元3か所（`App::RunCompressMode`・`MainWindow::OnAddFiles`・`OnDropFiles`）に `Ensure7zLoaded()` ガードを追加。

---

## 🟡 設計上ある程度仕方ないが改善余地あり

### 6. `CompressDlg.cpp` — 7z/ZIP のメソッドリスト固定 — **対応不要と判断**

- **理由**: `kMethods7z[]` / `kMethodsZip[]` は「形式ごとの有効コーデック」の静的定義であり、7z.dll の API では形式とコーデックの対応関係を取得できない。`GetEncoderNames()` との突合による有効/無効フィルタリングは既に実装済みで、これが取り得るベスト。

---

### 7. `CompressDlg.cpp` — 形式ごとの level/method 制御が固定 — **対応不要と判断**

- **理由**: 7z.dll の API にレベル範囲・暗号化対応・ソリッド対応等のフォーマット属性は存在しない。動的化は不可能。

---

### 8. `res/AileEx.rc` — UI 文言に形式名をハードコード ✅ 完了

- **対応内容**: 詳細圧縮ダイアログの注記を汎用表現に変更（EN/JA 両方）。
  - `"* Volume splitting is not applied to gz/bz2/xz/tar."` → `"* Volume splitting is not supported for stream compression formats."`
  - `"※ 分割ボリュームは gz/bz2/xz/tar では適用されません。"` → `"※ ストリーム圧縮形式では分割ボリュームは使用できません。"`

---

## 🟢 設計上の固定（変更不要）

| 箇所 | 内容 | 理由 |
|---|---|---|
| `MainWindow.cpp` `isRar` 分岐 | RAR → unrar.dll / rar.exe 専用処理 | RAR は 7z.dll 以外のバックエンドを持つため別扱いは正当 |
| `App.cpp` RAR → `RunRarCompressSync` | 同上 | rar.exe 経由圧縮は別フロー |
| `CompressDlg.cpp` SFX が 7z/RAR のみ | SFX モジュールの実態がこの2形式のみ | 正当 |
| `SevenZip.cpp` RAR5→RAR4 フォールバック | RAR 固有の後方互換処理 | 正当 |
| `SevenZip.cpp` `IsStreamExt()` 固定リスト | CompressDlg 用。DLL確認不要なUI判定 | `IsStreamFormat()`（DLL確認付き）と役割分担済み |

---

## 修正優先度まとめ（実施結果）

| 優先度 | 箇所 | 効果 | 状態 |
|---|---|---|---|
| 高 | #3 MainWindow 展開先フォルダ名の拡張子リスト | 新形式追加時に即影響 | ✅ 完了 |
| 高 | #2 SevenZip 拡張子→CLSID 固定マッピング | ZS版新形式が開けないリスク | ✅ 完了 |
| 中 | #1 SevenZip フォールバック拡張子リスト | 古いDLL利用時に影響 | ✅ 完了 |
| 中 | #4 IDS_FILTER_ARCHIVE 動的生成化 | 手動追加漏れのリスク | ✅ 完了 |
| 低 | #5 CompressDlg フォールバック形式リスト | UI 表示のみ | ✅ 完了 |
| 低 | #6 メソッドリスト動的突合 | ZS版コーデックのUI反映 | 対応不要（API制約） |
| 低 | #7 level/method 制御の動的化 | 将来の新形式対応 | 対応不要（API制約） |
| 低 | #8 UI 文言の汎用化 | 表示上の問題のみ | ✅ 完了 |
