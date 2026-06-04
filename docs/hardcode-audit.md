# AileEx ハードコード箇所 監査結果

調査日: 2026-06-04  
対象コミット: 95bce64

## 背景

tar-in-stream 透過展開の動的 DLL 判定化対応（IsStreamExt / IsStreamFormat）の過程で、
DLL の実際の対応状況を見ずに固定リストで判定している箇所が複数あることが判明したため、
全体を調査した。

---

## 🔴 本来動的であるべきなのに固定（修正候補）

### 1. `SevenZip.cpp:440-446` — m_extToClsid 空時のフォールバック拡張子リスト

```cpp
static const wchar_t* kFallback[] = {
    L"7z", L"zip", L"rar", L"tar", L"gz", L"bz2", L"xz",
    L"cab", L"iso", L"jar", L"wim", L"lzma", L"lzh", L"arj",
    nullptr
};
```

- **問題**: DLL の `GetNumberOfFormats` が使えないとき（古いDLL等）のフォールバック。ZS版で増える `zst`/`lz4` 等が含まれず、DLL実態を反映しない。
- **修正方針**: フォールバック時も `IsStreamExt()` のリストと統合するか、ZS形式を追加。
- **難度**: Low

---

### 2. `SevenZip.cpp:516-543` — 拡張子→CLSID の固定マッピング

```cpp
if (ext == L"7z")  return CLSID_Format_7z;
if (ext == L"zip" || ext == L"jar") return CLSID_Format_Zip;
if (ext == L"tar") return CLSID_Format_Tar;
if (ext == L"gz")  return CLSID_Format_GZip;
...
```

- **問題**: `m_extToClsid` に無い形式の CLSID 解決が固定。ZS版独自形式（lz4 等）を開けない。
- **修正方針**: `m_extToClsid` が先に引けるはずなのでフォールバックとして残しつつ、ZS形式の CLSID 定数を追加。
- **難度**: Low

---

### 3. `MainWindow.cpp:82-86` — 展開先フォルダ名生成時の拡張子リスト

```cpp
static const wchar_t* kExts[] = {
    L".7z", L".zip", L".rar", L".tar", L".gz", L".bz2", L".xz",
    L".cab", L".iso", L".jar", L".wim", L".lzh", L".lzma", L".arj",
    L".zst", L".lz4", L".lz5", L".br", L".liz", nullptr
};
```

- **問題**: 「archive.tar.gz → archive」のように複合拡張子を剥ぐ処理が固定リスト依存。将来の新形式は漏れる。
- **修正方針**: `IsArchiveExt()` / `IsStreamExt()` を使って動的判定に変更。
- **難度**: Low

---

### 4. `res/AileEx.rc` — IDS_FILTER_ARCHIVE（ファイルを開くダイアログ）

```rc
"Archive files|*.zip;*.7z;*.rar;...;*.lzma;*.zst;*.lz4;*.lz5;*.br;*.liz;..."
```

- **問題**: DLL の対応形式と独立した固定文字列。DLL が更新されても手動追加が必要。
- **修正方針**: 起動時に DLL の `m_extToClsid` からフィルター文字列を動的生成する。
- **難度**: Medium（起動タイミングでの生成が必要）

---

### 5. `CompressDlg.cpp:51-58` — DLL 未ロード時の圧縮形式フォールバック

```cpp
static const WritableFormat kFallbackFormats[] = {
    {L"7-Zip (.7z)",  L"7z"},
    {L"ZIP (.zip)",   L"zip"},
    {L"TAR (.tar)",   L"tar"},
    {L"GZip (.gz)",   L"gz"},
    {L"BZip2 (.bz2)", L"bz2"},
    {L"XZ (.xz)",     L"xz"},
};
```

- **問題**: DLL未ロード時の UI フォールバックが固定。実態と乖離する。
- **修正方針**: DLL 未ロード時は圧縮ダイアログ自体を無効化するか、メッセージ表示に統一。
- **難度**: Low

---

## 🟡 設計上ある程度仕方ないが改善余地あり

### 6. `CompressDlg.cpp:61-84` — 7z/ZIP のメソッドリスト固定

```cpp
static const MethodEntry kMethods7z[] = {
    {IDS_METHOD_LZMA2,   L"LZMA2"},
    {IDS_METHOD_LZMA,    L"LZMA"},
    {IDS_METHOD_BZIP2,   L"BZip2"},
    ...
};
```

- **問題**: DLL の実際のコーデック一覧（`GetNumberOfMethods`）と突合していない。ZS版では追加コーデックが存在する可能性がある。
- **修正方針**: `SevenZip::GetEncoderNames()` と突合して、未対応メソッドはグレーアウトまたは非表示。
- **難度**: Medium

---

### 7. `CompressDlg.cpp:272-367` — 形式ごとの level/method 制御が固定

```cpp
bool is7z  = (fmtId == L"7z");
bool isZip = (fmtId == L"zip");
bool isRar = (fmtId == L"rar");
// これ以外は level/method 無効化
```

- **問題**: 将来 DLL が `zst` 等の圧縮レベル指定をサポートしても UI が対応できない。
- **修正方針**: `WritableFormat` 構造体に `hasLevel`/`hasMethods` フラグを持たせて動的制御。
- **難度**: Medium

---

### 8. `res/AileEx.rc` — UI 文言に形式名をハードコード

```rc
"* Solid block applies only to 7z format."
"* Volume splitting is not applied to gz/bz2/xz/tar."
```

- **問題**: 形式が増えても文言が更新されない。
- **修正方針**: 説明文をより汎用的な表現に変更（形式名を列挙しない）。
- **難度**: Low

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

## 修正優先度まとめ

| 優先度 | 箇所 | 効果 |
|---|---|---|
| 高 | #3 MainWindow 展開先フォルダ名の拡張子リスト | 新形式追加時に即影響 |
| 高 | #2 SevenZip 拡張子→CLSID 固定マッピング | ZS版新形式が開けないリスク |
| 中 | #1 SevenZip フォールバック拡張子リスト | 古いDLL利用時に影響 |
| 中 | #4 IDS_FILTER_ARCHIVE 動的生成化 | 手動追加漏れのリスク |
| 低 | #5 CompressDlg フォールバック形式リスト | UI 表示のみ |
| 低 | #6 メソッドリスト動的突合 | ZS版コーデックのUI反映 |
| 低 | #7 level/method 制御の動的化 | 将来の新形式対応 |
| 低 | #8 UI 文言の汎用化 | 表示上の問題のみ |
