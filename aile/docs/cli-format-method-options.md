# AileEx `-t` / `-m` / `-l` オプション 実装仕様

**作成日:** 2026-06-08  
**更新日:** 2026-06-09  
**対象プロジェクト:** AileEx  
**関連ドキュメント:** [`cli-options-comparison.md`](cli-options-comparison.md)

---

## 1. 概要

### 1.1 目的

AileEx の `a` / `w` アクションに `-t`（形式）/ `-m`（方式）/ `-l`（レベル）を組み合わせて、
圧縮設定のコマンドライン制御を行う。

```powershell
# ダイアログなしで自動実行
AileEx.exe a file.txt -tzip -mdeflate -l9

# ダイアログにプリセットして表示
AileEx.exe a file.txt -mdeflate -l9
```

### 1.2 有効スコープ

**すべての圧縮モードで有効。** ただしモードによって動作が異なる。

| モード | `-t` | `-m`/`-l` |
|---|---|---|
| `a`/`w`（明示的圧縮） | ダイアログスキップ → 直接圧縮 | 圧縮設定に適用 |
| 自動検出（通常ファイルをドロップ等） | ダイアログ表示（スキップしない）| ダイアログにプリセット |

抽出モード（`x`）とブラウズモードでは `-t`/`-m`/`-l` を無視する。

---

## 2. オプション仕様

### 2.1 `-t<format>` — アーカイブ形式

| 値 | 形式 | 備考 |
|---|---|---|
| `7z` | 7-Zip | |
| `zip` | ZIP | |
| `tar` | TAR | |
| `gz` | GZip | ストリーム形式 |
| `bz2` | BZip2 | ストリーム形式 |
| `xz` | XZ | ストリーム形式 |
| `zst` | Zstandard | 7-Zip ZS 拡張 DLL のみ |
| `rar` | RAR | rar.exe / WinRAR.exe が必要 |

値は大文字小文字不問（内部で小文字化）。

### 2.2 `-m<method>` — 圧縮方式

形式によって有効な値が異なる。

**`-t7z` の場合**

| 値 | 方式 | 備考 |
|---|---|---|
| `lzma2` | LZMA2 | **既定** |
| `lzma` | LZMA | |
| `ppmd` | PPMd | |
| `bzip2` | BZip2 | |
| `deflate` | Deflate | |
| `zstd` | Zstandard | 7-Zip ZS 拡張 DLL のみ |
| `brotli` | Brotli | 7-Zip ZS 拡張 DLL のみ |
| `lz4` / `lz5` | LZ4 / LZ5 | 7-Zip ZS 拡張 DLL のみ |
| `lizard` | Lizard | 7-Zip ZS 拡張 DLL のみ |
| `flzma2` | FastLZMA2 | 7-Zip ZS 拡張 DLL のみ |

**`-tzip` の場合**

| 値 | 方式 | 備考 |
|---|---|---|
| `deflate` | Deflate | **既定** |
| `deflate64` | Deflate64 | |
| `bzip2` | BZip2 | |
| `lzma` | LZMA | |
| `ppmd` | PPMd | |
| `copy` | 無圧縮 | |
| `zstd` | Zstandard | 7-Zip ZS 拡張 DLL のみ |

**`-trar` の場合**

RAR には方式（アルゴリズム）の概念がなく、`-m` は無視する。レベルは `-l` で指定する。

**ストリーム形式（`gz` / `bz2` / `xz` 等）の場合**

`-m` は無視する。形式そのものが圧縮アルゴリズムを決定する。

### 2.3 `-l<level>` — 圧縮レベル

| 形式 | 有効値 | 備考 |
|---|---|---|
| `7z` / `zip` | `0`〜`9` | 0=無圧縮、9=最高圧縮 |
| `rar` | `0`〜`5` または `store`/`fastest`/`fast`/`normal`/`good`/`best` | 大文字小文字不問。既定は `normal`（3） |
| ストリーム形式 | 無視 | |

---

## 3. 動作フロー

### 3.1 ダイアログの扱い

**`a`/`w` アクション（`SW_HIDE` で起動）:**

| `-t` | `-m`/`-l` | 動作 |
|---|---|---|
| あり | 任意 | ダイアログスキップ → 直接圧縮実行 |
| なし | あり | ダイアログ表示（値がプリセットされた状態） |
| なし | なし | ダイアログ表示（保存済み設定で起動、現状通り） |

**自動検出モード（通常の `nCmdShow` で起動）:**

| `-t`/`-m`/`-l` | 動作 |
|---|---|
| いずれかあり | ダイアログ表示（`-t` があっても**スキップしない**）、値をプリセット |
| すべてなし | ダイアログ表示（保存済み設定、現状通り） |

ダイアログスキップは `a`/`w` アクション（`SW_HIDE`）でのみ行う。
実装上は `nCmdShow == SW_HIDE` で判別する。

### 3.2 ストリーム形式 + 複数ファイルの挙動

`-tgz aaa.txt bbb.txt` のように複数ファイルをストリーム形式で圧縮する場合、
`SevenZip::Compress` が内部で自動的に TAR でまとめてから gz 圧縮する。
出力ファイルは `aaa.tar.gz` になる（単一ファイルなら `aaa.gz`）。
この挙動はダイアログから gz を選んだ場合と同一。

### 3.3 組み合わせ例

```powershell
# a/w アクション: -t あり → ダイアログスキップ
AileEx.exe a file.txt -tzip -mdeflate -l9
AileEx.exe a file.txt -t7z
AileEx.exe a file.txt -trar -mnormal
AileEx.exe w *.txt -tzip -l5

# a/w アクション: -t なし → ダイアログ表示（プリセット）
AileEx.exe a file.txt -mdeflate           # 方式だけプリセット
AileEx.exe a file.txt -l9                 # レベルだけプリセット
AileEx.exe a file.txt -mdeflate -l9       # 方式+レベルをプリセット

# 自動検出モード: -t があってもダイアログ表示（プリセットのみ）
AileEx.exe -tzip file.txt                  # ダイアログが ZIP プリセット状態で開く
AileEx.exe -mdeflate file.txt              # ダイアログが Deflate プリセット状態で開く
```

---

## 4. 実装スケッチ

### 4.1 `main.cpp` — パース追加

```cpp
std::wstring typeOverride;    // -t
std::wstring methodOverride;  // -m
std::wstring levelOverride;   // -l  (空文字 = 未指定)

// パースループ内に追加
else if ((a[0]==L'-'||a[0]==L'/') && (a[1]==L't'||a[1]==L'T') && a[2]) {
    typeOverride = a + 2;
    for (auto& c : typeOverride) c = (wchar_t)towlower(c);
}
else if ((a[0]==L'-'||a[0]==L'/') && (a[1]==L'm'||a[1]==L'M') && a[2]) {
    methodOverride = a + 2;
}
else if ((a[0]==L'-'||a[0]==L'/') && (a[1]==L'l'||a[1]==L'L') && a[2] != L'\0') {
    levelOverride = a + 2;
}

// a/w アクションのみ渡す（SW_HIDE → ダイアログスキップ判定の起点）
case Action::CompressEach:
    result = app.RunCompressEachMode(positional, SW_HIDE, destDir,
                                     typeOverride, methodOverride, levelOverride, sfxOverride);
    break;
case Action::Compress:
    result = app.RunCompressMode(positional, SW_HIDE, destDir,
                                 typeOverride, methodOverride, levelOverride, sfxOverride);
    break;

// 自動検出モードも渡す（nCmdShow → ダイアログは必ず表示、プリセットのみ）
if (!regularFiles.empty())
    result = app.RunCompressMode(regularFiles, nCmdShow, L"",
                                 typeOverride, methodOverride, levelOverride, sfxOverride);
```

### 4.2 `App.h` — シグネチャ変更

```cpp
int RunCompressMode(const std::vector<std::wstring>& filePaths, int nCmdShow,
                    const std::wstring& destDir       = L"",
                    const std::wstring& typeOverride  = L"",
                    const std::wstring& methodOverride= L"",
                    const std::wstring& levelOverride = L"");

int RunCompressEachMode(const std::vector<std::wstring>& filePaths, int nCmdShow,
                        const std::wstring& destDir       = L"",
                        const std::wstring& typeOverride  = L"",
                        const std::wstring& methodOverride= L"",
                        const std::wstring& levelOverride = L"");
```

### 4.3 `App.cpp` — オーバーライド適用ロジック

```cpp
// params を設定ファイルから読み込んだ後、オーバーライドを上書き適用
params.LoadFromSettings(m_settings);
if (!typeOverride.empty())   params.format = typeOverride;
if (!methodOverride.empty()) ApplyMethodOverride(params, methodOverride);
if (!levelOverride.empty())  ApplyLevelOverride(params, levelOverride);

// ダイアログスキップは a/w アクション（SW_HIDE）かつ -t 指定時のみ
bool skipDialog = !typeOverride.empty() && (nCmdShow == SW_HIDE);

if (skipDialog) {
    // 出力パスに拡張子付与
    if (params.outputPath.find(L'.') == std::wstring::npos)
        params.outputPath += L"." + params.format;
} else {
    // ダイアログ表示（-t/-m/-l のプリセット値が入った状態）
    CompressDlg dlg;
    if (!dlg.Show(wnd.Hwnd(), params, enc, wf, rarAvailable)) return 0;
    params.SaveToSettings(m_settings);
    m_settings.Save();
}
```

### 4.4 `-l` RAR 文字列→数値変換

RAR の `-l` は数値（`0`〜`5`）または文字列（`store`/`fastest`/...）を受け付ける。

```cpp
// ApplyLevelOverride() のイメージ（RAR 時のみ文字列変換が必要）
void ApplyLevelOverride(CompressDlg::Params& params, const std::wstring& levelStr) {
    if (params.format == L"rar") {
        static const struct { const wchar_t* name; int level; } kRarLevels[] = {
            {L"store",   0}, {L"fastest", 1}, {L"fast",   2},
            {L"normal",  3}, {L"good",    4}, {L"best",   5},
        };
        std::wstring lower = levelStr;
        for (auto& c : lower) c = (wchar_t)towlower(c);
        for (auto& e : kRarLevels) {
            if (lower == e.name) { params.level = params.rarLevel = e.level; return; }
        }
        // 数値指定 ("0"〜"5")
        if (levelStr.size() == 1 && iswdigit(levelStr[0])) {
            int v = levelStr[0] - L'0';
            if (v <= 5) params.level = params.rarLevel = v;
        }
    } else {
        // 7z / zip: 0〜9
        if (levelStr.size() == 1 && iswdigit(levelStr[0]))
            params.level = levelStr[0] - L'0';
    }
}
```

---

## 5. 実装チェックリスト

- [x] `main.cpp` — `-t` / `-m` / `-l` パース実装済み
- [x] `App.h` — `RunCompressMode` / `RunCompressEachMode` シグネチャ実装済み
- [x] `App.cpp` — `ApplyOverrides()` ヘルパー実装済み
- [x] `App.cpp` — `RunCompressMode` オーバーライド適用 + ダイアログスキップロジック実装済み
- [x] `App.cpp` — `RunCompressEachMode` 同上
- [ ] テスト
  - [ ] `a file.txt -tzip -mdeflate -l9` でダイアログなし ZIP 圧縮
  - [ ] `a file.txt -t7z` でダイアログなし 7z 圧縮
  - [ ] `a file.txt -trar -mnormal` でダイアログなし RAR 圧縮
  - [ ] `a file.txt -mdeflate` でダイアログあり・Deflate プリセット
  - [ ] `a file.txt -l9` でダイアログあり・レベル9 プリセット
  - [ ] `a file.txt`（オプションなし）で現状通りの動作
  - [ ] `w *.txt -tzip` で各ファイル個別 ZIP 圧縮
  - [ ] `-tzip file.txt`（自動検出）で `-t` 無視・ダイアログ表示
  - [ ] `-tgz aaa.txt bbb.txt` で `aaa.tar.gz` 生成

---

## 6. リスク・注意点

| リスク | 対策 |
|---|---|
| 無効な形式名 | エラーメッセージ表示・中断 |
| DLL が非対応の方式 | 7z.dll 側でエラー返却（許容）|
| RAR に `-l` 指定 | 無視（`-m` で代替） |
| ストリーム形式に `-m`/`-l` | 無視 |
| `-t` なしで `-m` がダイアログで選べない形式 | ダイアログ上で無効化される（既存動作） |
