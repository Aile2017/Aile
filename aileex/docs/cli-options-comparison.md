# AileEx vs AileFlow コマンドラインオプション比較

**作成日:** 2026-06-08  
**対象:** AileEx と AileFlow の起動オプション

---

## 概要

両ツールともコマンドラインオプションは Noah スタイルの軽量設計。
AileFlow はより多くのオプションを備えており、圧縮設定の詳細制御が可能です。

---

## 起動オプション一覧

### 基本動作

| 種別 | AileEx | AileFlow | 説明 |
|------|--------|----------|------|
| **引数なし** | ✅ 空のウィンドウ表示 | ✅ 空のウィンドウ表示 | メインウィンドウを起動 |
| **アーカイブパス** | ✅ 参照モード | ✅ 参照モード | アーカイブを開いて内容表示 |
| **ファイルパス** | ✅ 圧縮モード | ✅ 圧縮モード | 圧縮ダイアログ表示 |
| **混合** | ✅ 圧縮優先 | ✅ 参照優先 | 複数引数がある場合の動作 |

---

## オプション詳細

### `-x` — 抽出強制

| 項目 | AileEx | AileFlow |
|------|--------|----------|
| **機能** | ✅ 抽出ダイアログを強制 | ✅ 抽出ダイアログを強制 |
| **書式** | `-x <archive>` | `-x <archive>` |
| **説明** | リストビュー をスキップ | リストビュー をスキップ |
| **実装** | `main.cpp:68` | `main.cpp:83` |

**例：**
```powershell
AileEx.exe -x archive.7z
AileFlow.exe -x archive.7z
```

---

### `-a` / `-ca` — 圧縮強制

| 項目 | AileEx | AileFlow |
|------|--------|----------|
| **基本圧縮** | ✅ `-a` | ✅ `-a` |
| **SFX 圧縮** | ✅ `-ca`, `-ca:console` 等 | ✅ `-ca` (SFX 予設定) |
| **書式** | `-a <file...>`, `-ca <file...>`, `-ca:console <file...>` | `-a <file...>` or `-ca <file...>` |
| **説明** | `-ca`時はダイアログをスキップしSFX作成（未サポート形式時は`-a`にフォールバック） | 圧縮ダイアログ表示、SFX設定オプション |
| **実装** | `main.cpp:73-83`, `App.cpp` | `main.cpp:85-90` |

**例：**
```powershell
# 通常圧縮
AileEx.exe -a file1.txt file2.txt
AileFlow.exe -a file1.txt file2.txt

# SFX 圧縮
AileEx.exe -ca file1.txt file2.txt
AileEx.exe -ca:console file1.txt file2.txt
AileFlow.exe -ca file1.txt file2.txt
```

---

### `-d` — 出力ディレクトリ指定

| 項目 | AileEx | AileFlow |
|------|--------|----------|
| **機能** | ✅ 抽出/圧縮の出力先指定 | ✅ 抽出/圧縮の出力先指定 |
| **書式** | `-d <dir>` or `-d<dir>` | `-d <dir>` or `-d<dir>` |
| **適用先** | 抽出時：フォルダ選択ダイアログをスキップ | 同じ |
| | 圧縮時：出力ディレクトリをプリセット | 同じ |
| **実装** | `main.cpp:72-81` | `main.cpp:93-102` |

**例：**
```powershell
# 抽出（ダイアログなし、直接指定ディレクトリへ）
AileEx.exe -x archive.7z -d C:\extract
AileFlow.exe -x archive.7z -d C:\extract

# 圧縮（出力ディレクトリをプリセット、形式選択ダイアログのみ）
AileEx.exe -a file.txt -d C:\output
AileFlow.exe -a file.txt -d C:\output

# 連続指定パターン（スペース無し）
AileEx.exe -dC:\extract
AileFlow.exe -dC:\output
```

---

### `-w` / `-W` — ファイル別圧縮

| 項目 | AileEx | AileFlow |
|------|--------|----------|
| **機能** | ✅ 各ファイルを個別アーカイブに圧縮 | ✅ 各ファイルを個別アーカイブに圧縮 |
| **書式** | `-w` or `-W` | `-w` or `-W` |
| **説明** | ダイアログを最初の1回だけ表示（`-t`, `-ca` 指定時はスキップ）、同じ拡張子を除いたファイル名(stem)を持つ入力ファイルをグループ化して全ファイルに適用 | 同じ（stemグループ化あり） |
| **RAR 対応** | ✅ | ❌（B2E 経由のため RAR なし） |
| **実装** | `main.cpp`, `App.cpp:RunCompressEachMode` | `main.cpp:91-92`, `App.cpp:RunCompressEachMode` |

**例：**
```powershell
# file1.7z, file2.7z, file3.7z を生成
AileEx.exe -w -a file1.txt file2.txt file3.txt -d C:\output
AileFlow.exe -w -a file1.txt file2.txt file3.txt -d C:\output

# -a なしでも動作（通常ファイルのみの場合）
AileEx.exe -w file1.txt file2.txt file3.txt
```

---

### `-t` — アーカイブ形式指定

| 項目 | AileEx | AileFlow |
|------|--------|----------|
| **機能** | ⏳ 実装予定 | ✅ アーカイブ形式をオーバーライド |
| **書式** | `-t<format>` | `-t<format>` |
| **値例** | `-t7z`, `-tzip`, `-ttar`, `-trar` など | `-t7z`, `-tzip`, `-ttar`, `-tlzh`, `-tcab` |
| **ダイアログ** | `-t` あり → スキップ | `-t` あり → スキップ |
| **スコープ** | `-a` / `-w` との組み合わせのみ | `-a` / `-w` / 自動検出も対応 |
| **実装** | `main.cpp`, `App.cpp` | `main.cpp:103-104` |

**例：**
```powershell
# ZIP 形式で圧縮（ダイアログスキップ）
AileEx.exe -a file.txt -tzip -d C:\output
AileFlow.exe -a file.txt -tzip -d C:\output
```

---

### `-m` — 圧縮方式指定

| 項目 | AileEx | AileFlow |
|------|--------|----------|
| **機能** | ⏳ 実装予定 | ✅ 圧縮方式をオーバーライド |
| **書式** | `-m<method>` | `-m<method>` |
| **値例（7z）** | `lzma2`, `lzma`, `ppmd`, `deflate`, `zstd` ... | 同左 |
| **値例（zip）** | `deflate`, `deflate64`, `bzip2`, `lzma`, `copy` ... | 同左 |
| **値例（rar）** | N/A（RAR に方式の概念なし。レベルは `-l` で指定） | N/A（RAR なし） |
| **`-t` なし時** | ダイアログ表示・方式をプリセット | ダイアログ表示・方式をプリセット |
| **実装** | `main.cpp`, `App.cpp` | `main.cpp:105-106` |

**例：**
```powershell
# Deflate 方式で ZIP 圧縮（ダイアログスキップ）
AileEx.exe -a file.txt -tzip -mdeflate -d C:\output

# 方式のみ指定（ダイアログ表示・Deflate プリセット）
AileEx.exe -a file.txt -mdeflate
```

---

### `-l` — 圧縮レベル指定（AileEx 予定）

| 項目 | AileEx | AileFlow |
|------|--------|----------|
| **機能** | ⏳ 実装予定 | ❌ なし |
| **書式** | `-l<level>` | — |
| **値例** | `-l0`〜`-l9` | — |
| **対象形式** | 7z / zip：`0`〜`9` / RAR：`0`〜`5` または `store`/`fastest`/`fast`/`normal`/`good`/`best` / ストリーム形式：無視 | — |
| **`-t` なし時** | ダイアログ表示・レベルをプリセット | — |
| **実装** | `main.cpp`, `App.cpp` | — |

**例：**
```powershell
# ZIP, Deflate, レベル9（最高圧縮）
AileEx.exe -a file.txt -tzip -mdeflate -l9

# レベルのみ指定（ダイアログ表示・レベル9 プリセット）
AileEx.exe -a file.txt -l9
```

---

## 実行モード別オプション組み合わせ

### 抽出モード

#### AileEx

```powershell
# ダイアログ表示（手動で抽出先を選択）
AileEx.exe -x archive.7z

# 自動抽出（フォルダ選択ダイアログをスキップ）
AileEx.exe -x archive.7z -d C:\extract
```

#### AileFlow

```powershell
# ダイアログ表示（手動で抽出先を選択）
AileFlow.exe -x archive.7z

# 自動抽出（フォルダ選択ダイアログをスキップ）
AileFlow.exe -x archive.7z -d C:\extract
```

---

### 圧縮モード

#### AileEx

```powershell
# 通常圧縮（形式・方式選択ダイアログ表示）
AileEx.exe -a file1.txt file2.txt

# 出力先プリセット（形式・方式は手動選択）
AileEx.exe -a file1.txt file2.txt -d C:\output

# SFX 圧縮（ダイアログスキップで自己解凍書庫作成）
AileEx.exe -ca file.txt
AileEx.exe -ca:console file.txt -t7z

# ファイル別圧縮（各ファイルを個別アーカイブに）
AileEx.exe -w -ca file1.txt file2.txt file3.txt -d C:\output
```

#### AileFlow

```powershell
# 通常圧縮（形式・方式選択ダイアログ表示）
AileFlow.exe -a file1.txt file2.txt

# 形式・方式プリセット（出力先も指定）
AileFlow.exe -a file.txt -tzip -mdeflate -d C:\output

# SFX 圧縮（SFX チェックボックスを自動有効）
AileFlow.exe -ca file.txt -d C:\output

# ファイル別圧縮（各ファイルを個別アーカイブに）
AileFlow.exe -w -a file1.txt file2.txt file3.txt -d C:\output
```

---

## 自動検出モード（引数で指定なし）

| 検出内容 | 動作 |
|---------|------|
| **アーカイブ拡張子** | 参照モードで開く |
| **通常ファイル** | AileEx：圧縮モード / AileFlow：参照モード？ |
| **混合入力** | AileEx：圧縮優先 / AileFlow：参照優先 |
| **引数なし** | 空のウィンドウ表示 |

> **注意：** 自動検出の優先度が AileEx と AileFlow で異なります。

---

## 実装の詳細

### AileEx (`aileex/src/main.cpp`)

**オプション処理フロー：**

1. `CommandLineToArgvW()` で引数パース
2. フラグ処理：`-x`, `-a`, `-d` をチェック
3. ポジショナル引数を `positional` ベクタに格納
4. フラグの有無で実行モードを決定
5. 優先度：`-x` > `-a` > 自動検出

**コード位置：** L:25-129

---

### AileFlow (`aileflow/src/main.cpp`)

**オプション処理フロー：**

1. `CommandLineToArgvW()` で引数パース
2. フラグ処理：`-x`, `-a`, `-ca`, `-w`, `-d`, `-t`, `-m` をチェック
3. ポジショナル引数を格納、オーバーライド値を変数に保存
4. `RunCompressEachMode()` / `RunCompressMode()` でプリセット値を渡す
5. 同時実行制限：セマフォ管理（`ConcurrentLimit` INI 設定）

**コード位置：** L:25-164

---

## 差異のまとめ

### AileEx の特徴

✅ **シンプル**
- 最小限のオプション（`-x`, `-a`, `-d`）
- 直感的で覚えやすい
- DLL ベースの固定機能

❌ **拡張性の制限**
- 形式・方式の CLI 指定不可
- スクリプト駆動の形式をサポートしない（将来は B2E 統合で改善予定）

---

### AileFlow の特徴

✅ **高い自動化対応**
- `-t` (形式) / `-m` (方式) でスクリプト駆動機能をフル活用
- `-w` でバッチ処理支援（ファイル別圧縮）
- `-ca` で SFX 自動化
- 同時実行制限（セマフォ）で並行処理を制御

✅ **スクリプト駆動**
- B2E スクリプトで任意形式に対応可能
- `-t` / `-m` で新形式も同じインターフェースで処理

---

## 将来予定

### B2E 統合後の AileEx

B2E 統合時に AileFlow のオプション (`-w`, `-t`, `-m`) を AileEx に追加することで、以下が実現：

1. **スクリプト駆動形式の CLI 対応** — `-tzpaq -mzstd` 等
2. **バッチ処理対応** — `-w` でループ処理
3. **自動化スクリプト対応** — 形式・方式を完全に指定可能

---

## 参考資料

### ソースコード位置

| ツール | ファイル | 行数 | 内容 |
|--------|---------|------|------|
| AileEx | `src/main.cpp` | 25-129 | コマンドラインオプション処理 |
| AileFlow | `src/main.cpp` | 25-164 | コマンドラインオプション処理 + セマフォ管理 |

### オプション処理のパターン

**クォート処理（両ツール共通）：**
```cpp
auto StripQuotes = [](const std::wstring& s) -> std::wstring {
    // "C:\path" → C:\path に変換
};
```

**パス分割処理（引用符による結合パス対応）：**
```cpp
auto SplitAtQuote = [](const std::wstring& raw, std::wstring& dest, std::wstring& remainder) {
    // "C:\path"C:\archive.7z" → dest="C:\path", remainder="C:\archive.7z"
};
```

---

**ドキュメント作成完了**  
2026-06-08 09:13 JST
