# AileEx vs AileFlow コマンドラインオプション比較

**更新日:** 2026-06-10  
**対象:** AileEx と AileFlow の起動オプション

---

## 概要

サブコマンドスタイルの CLI。最初の引数でアクション（`a`/`x`/`w`）を指定し、
修飾子（`-sfx`, `-d`, `-t`, `-m`, `-l`）はダッシュ付き・連結のみ（スペース不可）。

---

## アクション一覧

| アクション | AileEx | AileFlow | 説明 |
|---|---|---|---|
| **引数なし** | ✅ | ✅ | 空のウィンドウ表示 |
| **アーカイブパス** | ✅ 参照モード | ✅ 参照モード | アーカイブを開いて内容表示 |
| **ファイルパス** | ✅ 圧縮モード | ✅ 圧縮モード | 圧縮ダイアログ表示 |
| **複数ファイル（自動検出）** | ✅ 全て圧縮対象 | ✅ 全て圧縮対象 | 種類混在も含め全て圧縮 |
| `a <file...>` | ✅ | ✅ | 圧縮強制 |
| `x <archive>` | ✅ | ✅ | 抽出強制 |
| `w <file...>` | ✅ | ✅ | ファイル別圧縮（各ファイルを個別アーカイブに） |

---

## 修飾子一覧

修飾子はすべて**連結のみ**（`-d<dir>`, `-tzip` 等）。スペース区切り（`-d <dir>`）は不可。

| 修飾子 | `a` | `x` | `w` | AileEx | AileFlow | 説明 |
|---|---|---|---|---|---|---|
| `-sfx` / `-sfx:<v>` | ✅ | ❌ | ✅ | ✅ バリアントあり | ✅ フラグのみ | SFX 作成 |
| `-d<dir>` | ✅ | ✅ | ✅ | ✅ | ✅ | 出力ディレクトリ指定 |
| `-t<format>` | ✅ | ❌ | ✅ | ✅ | ✅ | アーカイブ形式指定 |
| `-m<method>` | ✅ | ❌ | ✅ | ✅ | ✅ | 圧縮方式指定 |
| `-l<level>` | ✅ | ❌ | ✅ | ✅ | ❌ | 圧縮レベル指定 |

無効な組み合わせ（例：`x -tzip`）は黙って無視。

---

## 修飾子詳細

### `-sfx` / `-sfx:<variant>` — SFX 作成

| 項目 | AileEx | AileFlow |
|------|--------|----------|
| **機能** | ✅ SFX アーカイブを作成 | ✅ SFX アーカイブを作成 |
| **書式** | `-sfx` または `-sfx:<variant>` | `-sfx` のみ |
| **バリアント** | `gui`（既定）/ `console` 等 | なし（bool フラグ） |
| **対応形式** | 7z / RAR のみ（他は無視） | B2E 経由（形式依存） |
| **ダイアログ** | `a`/`w` + `-sfx` → スキップ | `a`/`w` + `-sfx` → スキップ |

**例：**
```powershell
AileEx.exe a file1.txt file2.txt -sfx
AileEx.exe a file1.txt file2.txt -sfx:console -t7z
AileFlow.exe a file1.txt file2.txt -sfx
```

---

### `-d<dir>` — 出力ディレクトリ指定

| 項目 | AileEx | AileFlow |
|------|--------|----------|
| **機能** | ✅ 抽出/圧縮の出力先指定 | ✅ 抽出/圧縮の出力先指定 |
| **書式** | `-d<dir>`（連結のみ） | `-d<dir>`（連結のみ） |
| **`a`/`w` 時** | 出力ディレクトリをプリセット | 同じ |
| **`x` 時** | フォルダ選択ダイアログをスキップ | 同じ |

**例：**
```powershell
AileEx.exe x archive.7z -dC:\extract
AileEx.exe a file.txt -dC:\output
AileFlow.exe x archive.7z -dC:\extract
```

---

### `-t<format>` — アーカイブ形式指定

| 項目 | AileEx | AileFlow |
|------|--------|----------|
| **機能** | ✅ アーカイブ形式をオーバーライド | ✅ アーカイブ形式をオーバーライド |
| **書式** | `-t<format>` | `-t<format>` |
| **値例** | `-t7z`, `-tzip`, `-ttar`, `-trar` など | `-t7z`, `-tzip`, `-ttar`, `-tlzh`, `-tcab` |
| **`a`/`w` 時** | `-t` あり → ダイアログスキップ | `-t` あり → ダイアログスキップ |
| **自動検出時** | ダイアログ表示（形式プリセット） | ダイアログ表示（形式プリセット） |

**例：**
```powershell
AileEx.exe a file.txt -tzip -dC:\output
AileFlow.exe a file.txt -tzip -dC:\output
```

---

### `-m<method>` — 圧縮方式指定

| 項目 | AileEx | AileFlow |
|------|--------|----------|
| **機能** | ✅ 圧縮方式をオーバーライド | ✅ 圧縮方式をオーバーライド |
| **書式** | `-m<method>` | `-m<method>` |
| **値例（7z）** | `lzma2`, `lzma`, `ppmd`, `deflate`, `zstd` ... | 同左 |
| **値例（zip）** | `deflate`, `deflate64`, `bzip2`, `lzma`, `copy` ... | 同左 |
| **RAR** | 無視（RAR に方式の概念なし） | N/A（RAR なし） |
| **単独指定時** | ダイアログ表示・方式をプリセット | 同左 |

**例：**
```powershell
AileEx.exe a file.txt -tzip -mdeflate -dC:\output
AileEx.exe a file.txt -mdeflate   # ダイアログ表示・Deflate プリセット
```

---

### `-l<level>` — 圧縮レベル指定（AileEx のみ）

| 項目 | AileEx | AileFlow |
|------|--------|----------|
| **機能** | ✅ 圧縮レベルをオーバーライド | ❌ なし |
| **書式** | `-l<level>` | — |
| **値（7z/zip）** | `0`〜`9` | — |
| **値（RAR）** | `0`〜`5` または `store`/`fastest`/`fast`/`normal`/`good`/`best` | — |
| **ストリーム形式** | 無視 | — |
| **単独指定時** | ダイアログ表示・レベルをプリセット | — |

**例：**
```powershell
AileEx.exe a file.txt -tzip -mdeflate -l9
AileEx.exe a file.txt -l9   # ダイアログ表示・レベル9 プリセット
```

---

## 実行モード別コマンド例

### 抽出モード

```powershell
# ダイアログ表示（手動で抽出先を選択）
AileEx.exe x archive.7z
AileFlow.exe x archive.7z

# 自動抽出（フォルダ選択ダイアログをスキップ）
AileEx.exe x archive.7z -dC:\extract
AileFlow.exe x archive.7z -dC:\extract
```

### 圧縮モード

```powershell
# 通常圧縮（形式・方式選択ダイアログ表示）
AileEx.exe a file1.txt file2.txt
AileFlow.exe a file1.txt file2.txt

# 形式・方式を指定してダイアログスキップ
AileEx.exe a file.txt -tzip -mdeflate -l9 -dC:\output
AileFlow.exe a file.txt -tzip -mdeflate -dC:\output

# SFX 圧縮
AileEx.exe a file.txt -sfx
AileEx.exe a file.txt -sfx:console -t7z
AileFlow.exe a file.txt -sfx -dC:\output
```

### ファイル別圧縮

```powershell
# file1.7z, file2.7z, file3.7z を生成
AileEx.exe w file1.txt file2.txt file3.txt -dC:\output
AileFlow.exe w file1.txt file2.txt file3.txt -dC:\output

# 形式指定（ダイアログスキップ）
AileEx.exe w file1.txt file2.txt -t7z -l5 -dC:\output
```

---

## 自動検出モード（アクションなし）

| 検出内容 | AileEx | AileFlow |
|---------|--------|--------|
| **単一アーカイブ** | 参照モードで開く | 参照モードで開く |
| **それ以外（単一通常ファイル・複数ファイル）** | 全て圧縮対象 | 全て圧縮対象 |
| **引数なし** | 空のウィンドウ | 空のウィンドウ |

---

## 差異のまとめ

| 項目 | AileEx | AileFlow |
|---|---|---|
| `-l<level>` | ✅ | ❌ |
| `-sfx:<variant>` | ✅ (`gui`/`console` 等) | ❌（`-sfx` のみ） |
| 自動検出の動作 | 単一アーカイブのみ参照、それ以外は全て圧縮 | 同左 |
| RAR 圧縮 | ✅ | ❌（B2E 経由のため非対応） |
| 同時実行制限 | ❌ | ✅（セマフォ `ConcurrentLimit`） |

---

## 実装の詳細

### 両アプリ共通のパース構造

```cpp
enum class Action { None, Extract, Compress, CompressEach };
Action action = Action::None;
int argStart = 1;
if (argc > 1) {
    if      (_wcsicmp(argv[1], L"a") == 0) { action = Action::Compress;     argStart = 2; }
    else if (_wcsicmp(argv[1], L"x") == 0) { action = Action::Extract;      argStart = 2; }
    else if (_wcsicmp(argv[1], L"w") == 0) { action = Action::CompressEach; argStart = 2; }
}
// argStart 以降で修飾子をパース → switch(action) でディスパッチ
```

### ソースコード位置

| ツール | ファイル | 内容 |
|--------|---------|------|
| AileEx | `aileex/src/main.cpp` | コマンドラインオプション処理 |
| AileFlow | `aileflow/src/main.cpp` | コマンドラインオプション処理 + セマフォ管理 |
