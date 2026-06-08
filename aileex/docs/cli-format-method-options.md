# AileEx への `-t` (形式) / `-m` (方式) オプション実装

**作成日:** 2026-06-08  
**対象プロジェクト:** AileEx  
**優先度:** 高（B2E 統合の前提要件）  
**関連ドキュメント:** [`b2e-integration-design.md`](b2e-integration-design.md), [`cli-options-comparison.md`](cli-options-comparison.md)

---

## 1. 概要

### 1.1 現状の問題

AileEx の `-a` (圧縮強制) オプションは、**ユーザー操作が必須**です。

```powershell
AileEx.exe -a file.txt
# ↓ 圧縮ダイアログが表示される
# ↓ ユーザーが形式・方式・出力先を選択して OK を押す必要がある
```

**現状の制限：**
- ✅ メインウィンドウは非表示にできる
- ❌ 圧縮ダイアログは必ず表示
- ❌ ユーザー操作なしで自動完了できない
- ❌ バッチ処理・スクリプト駆動に不向き

### 1.2 提案される改善

AileFlow で既に実装済みの `-t` (形式) / `-m` (方式) オプションを AileEx に追加することで、以下を実現：

```powershell
# 完全自動化（ユーザー操作なし）
AileEx.exe -a file.txt -t7z -mdeflate -d C:\output
# ↓ ダイアログなし → 直接圧縮実行 → 終了
```

**メリット：**
1. **バッチ処理対応** — スクリプトやタスクスケジューラから自動実行可能
2. **B2E 統合への前提** — 新形式（zpaq など）の自動化に必須
3. **AileFlow との機能同等化** — 両ツール間の互換性向上

---

## 2. B2E 統合との関係図

```
┌─ AileEx 現状 ────────────────────────┐
│  -a オプション                       │
│  ├─ 圧縮ダイアログ表示（必須）      │
│  └─ 新形式 (zpaq等) 未対応          │
└──────────────────────────────────────┘
           ↓
┌─ Phase 1: -t -m オプション追加 ─────┐
│  -a -t7z -mdeflate 形式で対応       │
│  ├─ 圧縮ダイアログをスキップ        │
│  ├─ 7z.dll の形式のみ対応           │
│  └─ バッチ処理対応                  │
└──────────────────────────────────────┘
           ↓
┌─ Phase 2: B2E 統合 ──────────────────┐
│  -t -m で任意形式の自動化            │
│  ├─ -tzpaq -mzstd で zpaq 自動実行  │
│  ├─ B2E スクリプト駆動              │
│  └─ 完全な形式独立性実現            │
└──────────────────────────────────────┘
```

**重要：** Phase 1 の実装（`-t -m` オプション）は、Phase 2 の B2E 統合のための **前提要件** です。

---

## 3. 技術仕様

### 3.1 新規オプション

| オプション | 書式 | 値例 | 説明 |
|---|---|---|---|
| **`-t`** | `-t<format>` | `-t7z`, `-tzip`, `-ttar` | アーカイブ形式を指定 |
| **`-m`** | `-m<method>` | `-mdeflate`, `-mlzma`, `-mzstd` | 圧縮方式を指定 |

### 3.2 動作フロー

**現状（AileEx）:**
```
入力: AileEx.exe -a file.txt
  ↓
メインウィンドウ作成（SW_HIDE）
  ↓
CompressDlg.Show() ← ユーザーが形式・方式・出力先を選択
  ↓
圧縮実行
```

**改善後（AileEx）:**
```
入力: AileEx.exe -a file.txt -t7z -mdeflate -d C:\out
  ↓
-t -m が指定 → ダイアログをスキップ
  ↓
CompressDlg.Show() をスキップ
params.format = "7z"
params.method = "deflate"
params.outputPath = "C:\out\file.7z"
  ↓
圧縮実行
```

### 3.3 互換性

**優先度（既に AileFlow で確認済み）:**

1. `-t` が指定されている → ダイアログスキップ
2. `-t` がない → ダイアログ表示（現在のままで互換性保持）

**フォールバック：**
- `-t` で指定した形式が無効 → エラー返却（ダイアログ表示せず）
- `-m` で指定した方式が無効 → エラー返却

---

## 4. 実装スケッチ

### 4.1 コマンドラインパース追加

**ファイル:** `aileex/src/main.cpp`

```cpp
// 現在の実装（L:62-85）
bool forceExtract  = false;
bool forceCompress = false;
std::wstring destDir;
std::vector<std::wstring> positional;

// ↓ 以下を追加
std::wstring typeOverride;     // -t7z など
std::wstring methodOverride;   // -mdeflate など

// パース処理に追加（L:66-85）
for (int i = 1; i < argc; ++i) {
    const wchar_t* a = argv[i];
    // ... 既存の -x, -a, -d 処理 ...
    
    // 新規：-t オプション
    else if ((a[0] == L'-' || a[0] == L'/') && 
             (a[1] == L't' || a[1] == L'T') && 
             a[2] != L'\0') {
        typeOverride = a + 2;  // -t7z → "7z"
    }
    
    // 新規：-m オプション
    else if ((a[0] == L'-' || a[0] == L'/') && 
             (a[1] == L'm' || a[1] == L'M') && 
             a[2] != L'\0') {
        methodOverride = a + 2;  // -mdeflate → "deflate"
    }
    
    else {
        positional.push_back(a);
    }
}

// RunCompressMode へ渡す
if (forceCompress && !positional.empty()) {
    result = app.RunCompressMode(positional, SW_HIDE, destDir, 
                                 typeOverride, methodOverride);  // ← 追加
    app.Shutdown();
    return result;
}
```

### 4.2 RunCompressMode() 署名変更

**ファイル:** `aileex/src/App.h`

```cpp
// 現在
int RunCompressMode(const std::vector<std::wstring>& filePaths, int nCmdShow,
                    const std::wstring& destDir = L"");

// ↓ 改善後
int RunCompressMode(const std::vector<std::wstring>& filePaths, int nCmdShow,
                    const std::wstring& destDir = L"",
                    const std::wstring& typeOverride = L"",
                    const std::wstring& methodOverride = L"");
```

### 4.3 RunCompressMode() 実装変更

**ファイル:** `aileex/src/App.cpp` (L:107-...)

```cpp
int App::RunCompressMode(const std::vector<std::wstring>& filePaths, int nCmdShow,
                         const std::wstring& destDir,
                         const std::wstring& typeOverride,
                         const std::wstring& methodOverride) {
    // ... 既存初期化コード ...
    
    CompressDlg::Params params;
    params.inputFiles = filePaths;
    params.LoadFromSettings(m_settings);
    params.outputPath = Settings::ComputeDefaultOutputPath(m_settings, filePaths, destDir);
    
    // ─ 新規：-t -m で ダイアログをスキップ ─
    if (!typeOverride.empty()) {
        // -t が指定されている → 形式・出力ファイルを自動決定
        params.format = typeOverride;
        if (!methodOverride.empty()) {
            params.method = methodOverride;
        }
        // 出力ファイル名に拡張子を追加
        if (params.outputPath.find(L'.') == std::wstring::npos) {
            params.outputPath += L"." + params.format;
        }
        // ↓ ダイアログをスキップ、以下へ進む
    } else {
        // -t がない → 既存の実装（ダイアログ表示）
        CompressDlg dlg;
        if (!dlg.Show(wnd.Hwnd(), params, enc, wf, rarAvailable)) {
            return 0;
        }
        params.SaveToSettings(m_settings);
        m_settings.Save();
    }
    
    // ─ ここから圧縮実行（既存のまま） ─
    ProgressDlg progDlg;
    progDlg.Show(wnd.Hwnd(), I18n::Tr(IDS_PROGRESS_COMPRESSING).c_str());
    
    if (params.format == L"rar") {
        // ... RAR 処理 ...
    } else {
        // ... 7z.dll 処理 ...
    }
}
```

### 4.4 検証・エラーハンドリング

```cpp
// 形式の妥当性チェック
if (!typeOverride.empty()) {
    bool isValidFormat = m_sevenZip.IsArchiveExt(typeOverride.c_str());
    if (!isValidFormat && typeOverride != L"rar") {
        MessageBoxW(nullptr, 
                    (std::wstring(L"Unknown format: ") + typeOverride).c_str(),
                    L"AileEx", MB_ICONERROR);
        return 1;
    }
}

// 方式の妥当性チェック（形式ごと）
if (!methodOverride.empty() && !typeOverride.empty()) {
    // 7z.dll の エンコーダ名リストから検証
    auto& encoders = m_sevenZip.GetEncoderNames();
    bool isValidMethod = std::find(encoders.begin(), encoders.end(), 
                                    methodOverride) != encoders.end();
    if (!isValidMethod) {
        // ⚠️ 警告のみ（メソッド名の大文字小文字の違いに対応）
        // AileFlow も同じ方針（厳密なチェックはしない）
    }
}
```

---

## 5. 実装の段階

### Phase 1: 基本実装（優先度：高）

**目標：** 7z.dll の形式に対して `-t -m` で自動実行

**実装項目：**

- [ ] `main.cpp` でコマンドラインパース追加（`-t`, `-m` オプション処理）
- [ ] `App.h` の `RunCompressMode()` 署名を変更
- [ ] `App.cpp` の `RunCompressMode()` 実装を修正
  - [ ] `-t` が指定されている場合、`CompressDlg::Show()` をスキップ
  - [ ] 出力ファイル名に自動的に拡張子を追加
  - [ ] 形式・方式の妥当性チェック
- [ ] テスト
  - [ ] `AileEx.exe -a file.txt -t7z -mdeflate` で自動実行確認
  - [ ] `-t` なしで `-a` は現在のままダイアログ表示
  - [ ] 無効な形式でエラー処理

**推定工数：** 4-6 時間

---

### Phase 2: B2E 形式への対応準備（優先度：高）

**目標：** Phase 1 の実装が完了した後、B2E 統合で zpaq などの新形式に対応できるようにする

**条件：**
- B2E エンジンの統合完了（[`b2e-integration-design.md`](b2e-integration-design.md) 参照）
- `B2e_GetWritableFormats()` で形式リストを取得可能

**実装項目（将来）：**

- [ ] `-t` で指定された形式が 7z.dll でサポートされない場合 → B2E で処理
- [ ] 形式検出ロジック
  ```cpp
  if (m_sevenZip.IsArchiveExt(typeOverride)) {
      // 7z.dll で処理
  } else if (B2e_IsArchiveExt(typeOverride)) {
      // B2E で処理
  } else {
      // エラー
  }
  ```

---

## 6. 既存コードの流用

### 6.1 AileFlow の実装パターン

以下が参考になります：

| 項目 | ファイル | 行数 | 内容 |
|------|---------|------|------|
| コマンドラインパース | `aileflow/src/main.cpp` | 103-106 | `-t`, `-m` オプション処理 |
| RunCompressMode 実装 | `aileflow/src/App.cpp` | 103-164 | ダイアログスキップロジック |
| 出力ファイル名決定 | `aileflow/src/App.cpp` | 124-133 | 拡張子自動追加 |

### 6.2 コピー可能なコード

AileFlow の実装を参考に、以下をそのまま流用できます：

1. **クォート処理** — `main.cpp` の `StripQuotes`, `SplitAtQuote` ラムダ
2. **形式検出ロジック** — `RunCompressMode()` の `if (!typeOverride.empty())` ブロック
3. **出力ファイル名決定** — 拡張子追加ロジック

---

## 7. テスト戦略

### 7.1 単体テスト

| テスト項目 | 入力 | 期待結果 |
|---|---|---|
| **基本：7z** | `AileEx.exe -a file.txt -t7z` | ダイアログなし、7z で圧縮実行 |
| **基本：zip** | `AileEx.exe -a file.txt -tzip` | ダイアログなし、zip で圧縮実行 |
| **方式指定** | `AileEx.exe -a file.txt -t7z -mlzma` | ダイアログなし、LZMA 圧縮 |
| **出力先** | `AileEx.exe -a file.txt -t7z -d C:\out` | `C:\out\file.7z` に出力 |
| **形式なし** | `AileEx.exe -a file.txt` | 従来どおりダイアログ表示（互換性） |
| **無効形式** | `AileEx.exe -a file.txt -txyz` | エラーメッセージ表示 |

### 7.2 回帰テスト

**互換性確保：**

- [ ] `-a` のみでダイアログが表示されることを確認
- [ ] `-x` オプション動作に変化なし
- [ ] `-d` オプション単独でも動作確認
- [ ] 複数ファイルの圧縮が動作確認

---

## 8. ドキュメント更新

### 8.1 追加すべきドキュメント

- [ ] README.md に `-t -m` オプション説明を追加
- [ ] docs/specification.md に新オプション仕様を追加

### 8.2 既存ドキュメント更新

- [ ] `docs/cli-options-comparison.md` を更新（AileEx に `-t -m` 追加後）
- [ ] `docs/b2e-integration-design.md` の「実装の進め方」で `-t -m` を Phase 1 の前提として明記

---

## 9. リスク評価

### 9.1 主要リスク

| リスク | 確度 | 影響 | 対策 |
|--------|------|------|------|
| **既存動作への影響** | 低 | `-a` のみでダイアログが表示されなくなる | `-t` 指定時のみダイアログをスキップ |
| **形式名の不一致** | 中 | `-t7z` と `-tZIP` など大文字小文字混在 | パース時に小文字統一 |
| **無効な方式指定** | 中 | エラーメッセージが分かりにくい | ユーザーフレンドリーなエラーメッセージ |

### 9.2 対策

1. **デフォルト動作を変えない** — `-t` がなければ現在のままダイアログ表示
2. **形式名は小文字で統一** — `typeOverride` をパース時に `wcslwr()` で小文字化
3. **エラーメッセージ** — 有効な形式リストを表示

---

## 10. 成功指標

この実装が成功したと判断する条件：

1. ✅ **バッチ処理対応** — `AileEx.exe -a file.txt -t7z -mdeflate -d C:\out` が自動実行完了
2. ✅ **互換性保持** — `-a file.txt` のままダイアログ表示
3. ✅ **エラーハンドリング** — 無効な形式でエラー表示
4. ✅ **B2E 統合への準備** — 将来 `-tzpaq -mzstd` で新形式に対応可能

---

## 11. 実装の優先順位

**B2E 統合全体スケジュール内での位置付け：**

```
1. [実装中] -t -m オプション追加（このドキュメント）
   ↓ 4-6 時間
2. [未開始] 共有ライブラリ化（kilib, ArcB2e を common/ へ）
   ↓ 8-12 時間
3. [未開始] ArchiveBackend インターフェース設計
   ↓ 6-10 時間
4. [未開始] B2E エンジン統合と zpaq テスト
   ↓ 16-20 時間
```

**推奨順序：** `-t -m` → ArchiveBackend → B2E 統合

---

## 12. 参考資料

### 12.1 関連ドキュメント

- [`b2e-integration-design.md`](b2e-integration-design.md) — B2E 統合の全体設計
- [`cli-options-comparison.md`](cli-options-comparison.md) — AileEx vs AileFlow のオプション比較

### 12.2 ソースコード参照

| ファイル | 用途 |
|---------|------|
| `aileflow/src/main.cpp` L:103-106 | `-t -m` パース処理 |
| `aileflow/src/App.cpp` L:103-164 | ダイアログスキップロジック |
| `aileex/src/main.cpp` L:25-129 | 現在の実装（参考） |
| `aileex/src/App.cpp` L:107-... | 改修対象 |

---

**ドキュメント作成完了**  
2026-06-08 10:03 JST

**関連課題：** B2E 統合（`b2e-integration-design.md`）の前提要件
