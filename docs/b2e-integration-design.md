# B2E エンジン統合設計ドキュメント

**作成日:** 2026-06-08  
**対象プロジェクト:** AileEx (Aile suite)  
**目的:** AileEx に B2E (Batch-to-EXE) エンジンを統合し、スクリプト駆動による形式拡張性を実現

---

## 1. 概要

### 1.1 現状の問題

AileEx は 7z.dll（7-Zip の DLL インターフェース）に依存しており、7z.dll が対応していない新しいアーカイブ形式には対応できません。

**具体例：zpaq**
- zpaq はコマンドラインアーカイバとしてのみ提供
- 7z.dll に zpaq サポートなし
- **現状：** AileEx は zpaq に対応不可 → プログラム改修が必要

### 1.2 提案される解決方法

AileFlow で既に実装済みの **B2E エンジン** を AileEx と共有資産化することで、以下を実現：

1. **スクリプト追加のみで新形式対応** — プログラム改修不要
2. **ユーザーカスタマイズの容易化** — テキストエディタで .b2e 編集
3. **形式依存性の低減** — コマンドラインツール存在 = 対応可能

---

## 2. アーキテクチャ比較

### 2.1 現在の設計

| 項目 | AileEx | AileFlow |
|------|--------|----------|
| **バックエンド** | 7z.dll, unrar.dll | B2E スクリプト + CLI ツール |
| **形式追加方法** | プログラム改修 | .b2e スクリプト追加 |
| **拡張ポイント** | DLL インターフェース（限定） | スクリプト言語（Rythp VM） |
| **保守責任** | ベンダー依存 | 独立管理 |
| **ユーザーカスタマイズ** | 困難（コンパイル必要） | 容易（テキスト編集） |

### 2.2 B2E 統合後の設計

```
┌─ AileEx ─────────────────────────────────────────┐
│                                                   │
│  ┌─ SevenZip Backend ────────────────────────┐  │
│  │ 7z.dll / unrar.dll                        │  │
│  │ (DLL直接、高速、ベンダー依存)             │  │
│  └───────────────────────────────────────────┘  │
│                        ↓                         │
│  ┌─ B2E Backend (新規統合) ──────────────────┐  │
│  │ ArcB2e + kilib + Rythp VM                 │  │
│  │ (CLI駆動、スクリプト駆動、拡張性高)      │  │
│  │ - .b2e スクリプトファイル + CLI tool      │  │
│  │ - zpaq, 新形式対応                        │  │
│  └───────────────────────────────────────────┘  │
│
│  共有 UI 層（browse / extract / compress）
│
└───────────────────────────────────────────────────┘

AileFlow (変更なし)
├─ B2E Backend (既存)
└─ UI Layer
```

### 2.3 優先順位と互換性

**バックエンド選択の優先度（推奨）：**

```
┌─ 拡張子認識 ──────────────────┐
│                               │
├─ SevenZip で対応可能？        │
│   ├─ YES → SevenZip 使用     │
│   │   (高速・信頼性高)        │
│   └─ NO ↓                    │
│                               │
├─ B2E で対応可能？             │
│   ├─ YES → B2E 使用          │
│   │   (CLI駆動・スクリプト)  │
│   └─ NO ↓                    │
│                               │
└─ エラー（未対応形式）         │
```

---

## 3. 技術仕様

### 3.1 B2E エンジンの構成

AileFlow から継承：

| コンポーネント | 用途 | 言語 |
|---|---|---|
| `ArcB2e.{h,cpp}` | B2E スクリプト実行エンジン | C++ (ANSI) |
| `kilib/` | スクリプト実行基盤（Rythp VM） | C (ANSI) |
| `B2eBridge.{h,cpp}` | UNICODE/ANSI ブリッジ | C++ (UNICODE) |
| `.b2e` スクリプト | 形式定義（archive tool → B2E） | Rythp (スクリプト言語) |

### 3.2 B2E スクリプト構造（zpaq 例）

```b2e
; zpaq.b2e - zpaq archive handler

(load
  (name    zpaq.exe)
  (version -h)
  (type    zpaq)
)

(list
  (exec zpaq l %1%)
  (from ^\s*(?<num>\d+)\s+(?<size>\d+)\s+(?<date>[\d-]+\s+[\d:]+)\s+(?<name>.+)$)
)

(extract
  (exec zpaq x %1% %2%)
)

(encode
  (type (zstd*))
  (exec zpaq a -m%m% %1% %2%)
)

(test
  (exec zpaq t %1%)
)

(delete
  (exec zpaq d %1% %2%)
)
```

### 3.3 実装上の注意点

#### 3.3.1 バイナリサイズと依存関係

| 追加要素 | サイズ估 | 用途 |
|---------|---------|------|
| kilib + Rythp VM | ~150-200 KB | B2E スクリプト実行 |
| B2eBridge.obj | ~50-80 KB | UNICODE/ANSI 橋渡し |
| **合計増加分** | **~200-250 KB** | 許容範囲内 |

#### 3.3.2 UNICODE/ANSI 境界

**重要：** B2E エンジン（kilib）は ANSI ベース（コマンドラインツール対応）

- **AileEx UI:** UNICODE
- **B2E Engine:** ANSI
- **解決方法:** `B2eBridge.cpp` で変換（AileFlow で既に実装済み）

```cpp
// B2eBridge.h (AileFlow から継承)
HRESULT B2e_List(const wchar_t* archivePath,  // ← UNICODE入力
                 std::vector<ArchiveItem>& items,
                 ...);
// 内部で ANSI に変換してエンジンを呼び出し
```

#### 3.3.3 セキュリティ考慮事項

B2E スクリプトはテキストベースなため、以下に注意：

1. **スクリプト検証**
   - `.b2e` ファイルを信頼できるディレクトリからのみロード
   - コマンドインジェクション対策：入力値のエスケープ

2. **CLI ツールの入力サニタイズ**
   - ファイルパスにスペースや特殊文字を含む場合のクォート処理
   - 既存実装：AileFlow の `SevenZipB2e.cpp` を参考

---

## 4. 実装ロードマップ

### Phase 1: 基盤統合（優先度：高）

#### 1.1 共有資産の再構成

**目標:** `common/` に B2E エンジンを統合可能にする

```
common/
├── B2E/
│   ├── ArcB2e.{h,cpp}
│   ├── B2eBridge.{h,cpp}
│   ├── kilib/           ← 既存の AileFlow/kilib をコピー
│   └── CMakeLists.txt
├── ArchiveItem.h        ← 既存
├── WorkerThread.{h,cpp} ← 既存
└── ...

aileex/
├── src/
│   ├── B2eWrapper.{h,cpp}  ← 新規：SevenZip と B2E の優先度管理
│   └── ...
└── CMakeLists.txt

aileflow/
├── src/
│   ├── B2eBridge.{h,cpp}   ← 既存：common/B2E/ から include
│   └── ...
└── CMakeLists.txt
```

**実装項目：**

- [ ] kilib を `common/B2E/kilib/` に移動
- [ ] ArcB2e.{h,cpp} を `common/B2E/` に移動
- [ ] B2eBridge.{h,cpp} を `common/B2E/` に移動（微調整可能）
- [ ] CMakeLists.txt を更新（両アプリから参照）
- [ ] インクルードパスを統一

#### 1.2 SevenZip との共存インターフェース

**新規ファイル:** `aileex/src/ArchiveBackend.h`

```cpp
class ArchiveBackend {
public:
    enum Type { SEVENZIP, B2E, UNKNOWN };
    
    // 形式検出
    static Type DetectFormat(const wchar_t* ext);
    
    // List (バックエンドに無関係なインターフェース)
    HRESULT List(const wchar_t* path,
                 std::vector<ArchiveItem>& items,
                 IExtractProgressSink* sink);
    
    // Extract
    HRESULT Extract(const wchar_t* path,
                    const std::vector<UINT32>& indices,
                    const wchar_t* destDir,
                    IExtractProgressSink* sink);
    
    // Compress
    HRESULT Compress(const std::vector<std::wstring>& srcPaths,
                     const wchar_t* outPath,
                     const CompressOptions& opts,
                     IExtractProgressSink* sink);
};
```

**実装項目：**

- [ ] ArchiveBackend クラスを設計・実装
- [ ] SevenZip 既存ロジックを ArchiveBackend に統合
- [ ] B2E バックエンドを統合（フォールバック時に呼び出し）
- [ ] 既存の MainWindow / CompressDlg を ArchiveBackend 経由で呼び出すように変更

### Phase 2: B2E インテグレーション（優先度：高）

#### 2.1 B2E 初期化とスクリプトロード

**実装項目：**

- [ ] `common/B2E/B2eInit.{h,cpp}` 作成
  - B2E エンジンのシングルトン初期化
  - .b2e スクリプトディレクトリ検索
  - セキュリティ検証（スクリプトの信頼性確認）

- [ ] AileEx に .b2e スクリプト配布機構を追加
  - `aileex/Release/b2e/` ディレクトリ作成
  - AileFlow の Release/b2e/ から .b2e ファイルをコピー
  - CMakeLists.txt で自動コピー

#### 2.2 形式優先度管理

**実装項目：**

- [ ] `ArchiveBackend::DetectFormat()` で優先度判定
  ```cpp
  // 優先度：SevenZip > B2E > 未対応
  if (SevenZip::IsArchiveExt(ext)) return Type::SEVENZIP;
  if (B2e_IsArchiveExt(ext))       return Type::B2E;
  return Type::UNKNOWN;
  ```

- [ ] フォールバックロジック実装
  ```cpp
  HRESULT ArchiveBackend::List(...) {
      Type type = DetectFormat(ext);
      
      if (type == Type::SEVENZIP) {
          return SevenZip_List(...);
      } else if (type == Type::B2E) {
          return B2e_List(...);
      }
      return E_NOTIMPL;
  }
  ```

### Phase 3: ユーザーカスタマイズ機構（優先度：中）

#### 3.1 .b2e スクリプト管理 UI

**実装項目：**

- [ ] Settings ダイアログに「Script Manager」タブ追加
  - 有効な .b2e スクリプト一覧表示
  - スクリプト有効/無効切り替え
  - カスタムスクリプト追加ダイアログ

#### 3.2 ユーザースクリプトディレクトリ

**実装項目：**

- [ ] `%APPDATA%/AileEx/scripts/` を検索パスに追加
  - ユーザーがカスタム .b2e スクリプトを置ける
  - セキュリティ：ディレクトリのアクセス権限を確認

---

## 5. 既存コードの流用

### 5.1 AileFlow から直接流用可能

```
aileflow/src/
├── ArcB2e.{h,cpp}       → common/B2E/ へコピー
├── B2eBridge.{h,cpp}    → common/B2E/ へコピー（微調整）
├── Archiver.h           → common/B2E/ へコピー
├── AileFlowKiLib.cpp    → common/B2E/ へコピー
└── kilib/               → common/B2E/ へコピー
```

### 5.2 AileFlow UI から参考にできるパターン

| 機能 | AileFlow ファイル | AileEx への応用 |
|------|---|---|
| B2E 初期化 | `SevenZipB2e.cpp` L:9-26 | B2e_Load 呼び出しパターン |
| 形式選択 | `CompressDlg.cpp` | B2E_GetWritableFormats 統合 |
| テスト実行 | `MainWindow.cpp::OnTest()` | B2E テスト機能を AileEx に公開 |
| 削除機能 | `MainWindow.cpp::OnDelete()` | B2E 削除機能を AileEx に公開 |

### 5.3 既存テストスイートの再利用

AileFlow の単体テスト (if 存在) を B2E 統合テストとして活用。

---

## 6. リスク評価と対策

### 6.1 主要リスク

| リスク | 確度 | 影響 | 対策 |
|--------|------|------|------|
| **バイナリサイズ増加** | 高 | ~250 KB 追加 | Phase 導入で段階的対応 |
| **UNICODE/ANSI 変換バグ** | 中 | 文字化け・クラッシュ | B2eBridge の既存実装を検証再利用 |
| **CLI ツール依存** | 低 | 新形式追加時にツール入手必要 | ユーザー責任として明記 |
| **セキュリティ（CLI注入）** | 中 | スクリプト実行時の攻撃 | 入力値エスケープ、スクリプト署名検討 |
| **パフォーマンス低下** | 低 | CLI 呼び出しオーバーヘッド | 優先度管理で SevenZip 優先 |

### 6.2 テスト戦略

| テスト項目 | 担当 | 優先度 |
|------|------|--------|
| 既存 SevenZip 機能回帰テスト | 自動テスト | **必須** |
| B2E スクリプト動作テスト | 手動テスト | **必須** |
| zpaq 形式統合テスト | 手動テスト | 高 |
| UNICODE/ANSI 境界テスト | 自動テスト | 高 |
| パフォーマンス比較（DLL vs B2E） | ベンチマーク | 中 |

---

## 7. ドキュメント更新予定

### 7.1 追加すべきドキュメント

- [ ] `docs/b2e-development-guide.md` — B2E スクリプト開発ガイド
- [ ] `docs/b2e-format-template.b2e` — 新形式追加の .b2e テンプレート
- [ ] `docs/architecture-integration.md` — AileEx + B2E 統合アーキテクチャ図
- [ ] README.md 更新 — 形式一覧に「カスタマイズ可能」を明記

### 7.2 ユーザードキュメント

- [ ] `docs/custom-scripts.md` — ユーザーカスタムスクリプト作成方法

---

## 8. 実装の進め方（推奨ステップ）

1. **Phase 1.1:** 共有ライブラリ化（kilib, ArcB2e 移動）
2. **Phase 1.2:** ArchiveBackend インターフェース設計
3. **Phase 2.1:** B2E 初期化と .b2e スクリプト配布
4. **Phase 2.2:** 優先度管理とフォールバック
5. **Phase 3.1+:** UI 拡張とユーザーカスタマイズ
6. **テスト・ドキュメント整備**

**推定工数（概算）：** Phase 1-2 で 40-60h、Phase 3 で 20-30h

---

## 9. 成功指標

この統合が成功したと判断する条件：

1. ✅ **zpaq 形式が AileEx で動作** — プログラム改修なし、.b2e スクリプト追加のみ
2. ✅ **既存 SevenZip 機能に影響なし** — 回帰テスト 100% パス
3. ✅ **ユーザーが .b2e スクリプト追加可能** — ドキュメント整備、テンプレート提供
4. ✅ **保守性向上** — 新形式追加時のプログラム改修が不要に

---

## 付録 A: 参考資料

### A.1 スクリプト言語 Rythp について

- Noah 公式：https://www.kmonos.net/lib/noah.ja.html
- B2E スクリプト仕様：Noah に同梱の docs/ を参照

### A.2 関連ファイル一覧

| ファイル | 用途 | 行数 |
|---------|------|------|
| `aileflow/src/ArcB2e.h` | B2E エンジンの C++ ラッパー | 80 |
| `aileflow/src/B2eBridge.h` | UNICODE/ANSI ブリッジ | 96 |
| `aileflow/kilib/kl_carc.h` | 汎用アーカイバインターフェース | 200+ |
| `aileflow/Release/b2e/*.b2e` | 形式定義スクリプト | 各形式で 20-100 行 |

### A.3 既存の B2E スクリプト例

- `7z.b2e` — 7-Zip 形式
- `zip.zipx.b2e` — ZIP / ZIPX 形式
- `rar.b2e` — RAR 形式
- `tar.b2e` — TAR / gzip / bzip2 / xz 形式
- `lzh.b2e` — LZH 形式
- `cab.b2e` — CAB 形式

---

**ドキュメント作成完了**  
2026-06-08 09:10 JST
