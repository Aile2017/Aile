# Aile 圧縮系 CLI 修飾子 (`-d` / `-t` / `-m` / `-l` / `-sfx`)

**作成日:** 2026-06-08  
**更新日:** 2026-07-01  
**対象プロジェクト:** Aile

---

## 1. 概要

この文書は、Aile の圧縮系 CLI 起動で使う修飾子の現在の実装仕様をまとめたものです。

- `a <files...>`: 明示的圧縮
- `w <files...>`: 各ファイル個別圧縮
- 通常起動 + 通常ファイル入力: 自動検出圧縮

対象修飾子:

- `-d<dir>` — 出力先 / 抽出先
- `-t<format>` — 形式
- `-m<method>` — 方式
- `-l<level>` — レベル
- `-sfx` / `-sfx:<variant>` — SFX

```powershell
# ダイアログなしで直接圧縮
Aile.exe a file.txt -tzip -mdeflate -l9

# 出力先と SFX を指定して直接圧縮
Aile.exe a file.txt -dC:\out -sfx:console

# 自動検出モード: ダイアログにプリセットして表示
Aile.exe file.txt -t7z -mlzma2 -l9
```

---

## 2. 起動モードごとの効き方

| モード | `-d` | `-t` / `-m` / `-l` / `-sfx` |
|---|---|---|
| `a` | 出力パス初期値に反映 | `-t` または `-sfx` があればダイアログ省略、なければプリセットして表示 |
| `w` | 出力先ディレクトリ初期値に反映 | `-t` または `-sfx` があればダイアログ省略、なければ最初の 1 回だけ表示 |
| 自動検出圧縮 | 出力パス初期値に反映 | 常にダイアログ表示。指定値はプリセットのみ |
| `x` | 抽出先として使う。指定時はフォルダ選択省略 | 無視 |
| `t` | 無視 | すべて無視 |

ダイアログ省略は **`a` / `w` かつ `-t` または `-sfx` 指定時のみ** です。

---

## 3. 修飾子仕様

### 3.1 `-d<dir>` — 出力先 / 抽出先

- `x <archive>`: `<dir>` に直接展開
- `a` / `w` / 自動検出圧縮: 圧縮ダイアログの出力パス初期値に反映
- `t`: 解析はされるが無視

例:

```powershell
Aile.exe x archive.7z -dC:\temp\out
Aile.exe a file.txt -dC:\temp\out -tzip
```

補足（引用符の扱い）: Aile は `CommandLineToArgvW` を使わず自前で引数分割する
（`src/main.cpp` の `SplitCommandLine`）。`"` は引用の開始/終了のみを意味し、
`\` は常にリテラル。そのため末尾 `\` 付きディレクトリを引用した
`-d"C:\some dir\"` も位置を問わず正しく解釈される（ファイラーの外部コマンド
テンプレート `%D\` 形式に対応）。`\"` エスケープは不要かつ解釈されない。
値の末尾 `\` はルート（`C:\`）を除き除去して正規化される。

### 3.2 `-t<format>` — アーカイブ形式

| 値 | 形式 | 備考 |
|---|---|---|
| `7z` | 7-Zip | |
| `zip` | ZIP | |
| `tar` | TAR | |
| `gz` | GZip | ストリーム形式 |
| `bz2` | BZip2 | ストリーム形式 |
| `xz` | XZ | ストリーム形式 |
| `zst` | Zstandard | 7-Zip ZS 拡張 DLL のみ |

値は大文字小文字不問です。内部では小文字化され、いくつかの別名は正規化されます。

- `gzip` → `gz`
- `bzip2` → `bz2`
- `brotli` → `br`
- `lizard` → `liz`
- `zstd` → `zst`

また、登録済みの B2E 形式の拡張子（例: `rar`, `lzh`）も指定できます。

### 3.3 `-m<method>` — 圧縮方式

形式によって有効な値が異なります。

**`-t7z` の場合**

| 値 | 方式 | 備考 |
|---|---|---|
| `lzma2` | LZMA2 | 既定 |
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
| `deflate` | Deflate | 既定 |
| `deflate64` | Deflate64 | |
| `bzip2` | BZip2 | |
| `lzma` | LZMA | |
| `ppmd` | PPMd | |
| `copy` | Store | |
| `zstd` | Zstandard | 7-Zip ZS 拡張 DLL のみ |

**`-ttar` の場合**

- `gz` / `bz2` / `xz` / `zst` / `lz4` / `lz5` / `br` / `liz` などを指定すると
  `tar.<method>` を作成
- method 未指定なら素の `.tar`

**B2E 形式（`rar`, `lzh` など）の場合**

`-m<name>` は、その形式の `.b2e` スクリプトが公開する type list の**名前一致**で解決され、
内部ではその index が `level` に設定されます。

**単体ストリーム形式（`gz` / `bz2` / `xz` 等）の場合**

`-m` は使いません。形式そのものが圧縮アルゴリズムを決定します。

### 3.4 `-l<level>` — 圧縮レベル

| 形式 / 方式 | 有効値 | 備考 |
|---|---|---|
| `7z` / `zip` (標準) | `0`〜`9` | 0=無圧縮、9=最高圧縮 |
| `zstd` | `1`〜`22` | Zstandard の範囲 |
| `lizard` | `10`〜`49` | Lizard の範囲 |
| `brotli` | `0`〜`11` | Brotli の範囲 |
| `lz4` / `lz5` | `1`〜`12` | LZ4/LZ5 の範囲 |
| B2E | 数値 index | `.b2e` の type list index を指す |
| 単体ストリーム形式 | 実質未使用 | 形式自体を指定するため |

数値は実装側で範囲内に丸められます。数値でない文字列は無視されます。

### 3.5 `-sfx` / `-sfx:<variant>` — 自己解凍形式

| 値 | 動作 |
|---|---|
| `-sfx` | `gui` を指定した扱い |
| `-sfx:gui` | GUI SFX |
| `-sfx:console` | Console SFX |

- `a` / `w` では `-sfx` 単独でもダイアログ省略条件になります
- 7z と B2E 形式のみ有効です
- 非対応形式では通常圧縮にフォールバックします

7z の場合は最終出力が `.exe` になります。B2E の場合は実際の SFX 生成は script 側に委ねられます。

---

## 4. 重要な動作ルール

### 4.1 自動検出モードでは常にダイアログを出す

通常起動で通常ファイルを渡した場合は、`-t` や `-sfx` があってもダイアログは省略されません。
指定値はプリセットとして使われるだけです。

### 4.2 単体ストリーム形式は単一ファイル専用

現在の実装では、`gz` / `bz2` / `xz` / `zst` などの**単体ストリーム形式は単一ファイル専用**です。

以下はエラーになります。

- 複数ファイル入力
- ディレクトリ入力

複数ファイルを 1 つにまとめたい場合は、`tar` を選んで `-m` にストリーム方式を指定してください。

```powershell
# 複数ファイルを gz 系でまとめる
Aile.exe a a.txt b.txt -ttar -mgz

# 各ファイルを個別に gzip する
Aile.exe w a.txt b.txt -tgz
```

### 4.3 方式指定と既定方式の正規化

`-t` だけを変えたとき、既定方式が新しい形式に合わなくならないように、
実装側で method は必要に応じてクリア/正規化されます。

例:

- `7z` → `zip` に切り替えたとき、`lzma2` を持ち越さない
- `tar` / 単体ストリーム形式では不要な method を落とす

---

## 5. 例

```powershell
# a/w: -t あり → ダイアログ省略
Aile.exe a file.txt -tzip -mdeflate -l9
Aile.exe a file.txt -t7z
Aile.exe a file.txt -ttar -mzst -l22
Aile.exe w a.txt b.txt -tzip -l5

# a/w: -sfx だけでもダイアログ省略
Aile.exe a file.txt -sfx
Aile.exe a file.txt -sfx:console

# a/w: -t/-sfx なし → ダイアログ表示
Aile.exe a file.txt -mdeflate
Aile.exe a file.txt -l9
Aile.exe a file.txt -dC:\out -mdeflate -l9

# 自動検出圧縮: 常にダイアログ表示
Aile.exe -tzip file.txt
Aile.exe -mdeflate file.txt
```

---

## 6. 実装対応箇所

- `src/main.cpp`
  - `-d` / `-t` / `-m` / `-l` / `-sfx` をパース
  - `a` / `w` / `x` / `t` ごとに引き回しを分岐
- `src/App.cpp`
  - `ApplyOverrides(...)` で形式・方式・レベル・SFX を正規化
  - `RunCompressMode(...)` / `RunCompressEachMode(...)` でダイアログ省略条件を判定
- `src/CompressPolicy.cpp`
  - 形式ごとの method 正規化
  - ストリーム形式の入力制約
  - 出力拡張子計算
