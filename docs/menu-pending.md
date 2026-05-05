# メニュー実装 — 残タスク

Phase 1 (2026-05-05) でメニューバー骨組みは投入済み。以下は次フェーズ以降の課題。

関連ファイル:

- `res/AileEx.rc` の `IDR_MAIN_MENU`
- `src/resource.h` の `ID_*` / `IDM_*`
- `src/MainWindow.cpp` の `OnCommand`
- `src/App.cpp` の `RunBrowseMode` アクセラレータテーブル

## Phase 2: 削除機能 (ID_DELETE)

操作 ＞ 削除 (Del) を実装する。

- 現状: メニュー項目は GRAYED 固定で表示のみ。
- 7z.dll 経路: `IOutArchive::UpdateItems` を呼び、選択行の index を skip するコールバックを渡す。新規アーカイブを一時ファイルに書いて元と差し替える定石。
- RAR 経路 (rar.exe): `rar.exe d <archive> <path>` を `RarProcess` 経由で起動。
- unrar.dll 経路: 削除はサポートなし (read-only)。動的にグレーアウトする必要あり。
- `m_openedWithUnrar == true` のときは GRAYED、それ以外は ENABLED に切り替え。`WM_INITMENUPOPUP` で動的更新するのが定石。

メニュー項目の動的有効/無効を扱うのはこれが最初なので、汎用的な `UpdateMenuState()` ヘルパを作って `WM_INITMENUPOPUP` で呼ぶ設計にする。閉じる (`ID_CLOSE`) や 関連付けで開く (`ID_OPEN_ASSOC`) も同様に「アーカイブが開いていない時は GRAYED」の対象なので、ここでまとめて整理する。

## MRU (最近使ったアーカイブ)

ファイル ＞ 最近使ったアーカイブ (`IDM_FILE_MRU_PH`)。

- 現状: 「(履歴なし)」プレースホルダのみ GRAYED 表示。
- 永続化: `Settings` に `m_mruPaths` (vector<wstring>, 最大 10 件) を追加。`Settings::Load/Save` で `MRU0..9` キーを読み書き。
- メニュー再構築: `OpenArchive(path)` 成功時に MRU の先頭に追加 (重複は除去、上限超は古いものから削除)。`MainWindow` 起動時とアーカイブ open 時に MRU サブメニューを `DeleteMenu` で全消去 → `AppendMenu` で再構築。
- 動的 ID: MRU 項目ごとに ID を割り当てる。`IDM_FILE_MRU_BASE = 41000` 等の連続レンジを `resource.h` に予約し、`OnCommand` で `id - IDM_FILE_MRU_BASE` をインデックスとして MRU リストを引いて `OpenArchive` を呼ぶ。
- 表示形式: ファイル名 + フルパス、または `"&1 path\to\archive.7z"` 形式 (先頭 9 件は &数字 でアクセラレータ)。

## ツリー表示トグル (IDM_VIEW_TREE)

表示 ＞ ツリー表示。

- 現状: GRAYED 固定。
- 実装: `m_treeVisible` (bool) を `MainWindow` に追加。コマンドハンドラで反転 → `ResizePanes` を呼んで `m_hTreeView` を `ShowWindow(SW_HIDE/SW_SHOW)`、スプリッタ位置を 0 にして ListView をフル幅化。
- メニューチェック: `WM_INITMENUPOPUP` で `CheckMenuItem(MF_CHECKED/MF_UNCHECKED)` を反映。
- 永続化: `Settings::m_treeVisible` を追加して状態を保存。

## 動的なメニュー有効/無効

`WM_INITMENUPOPUP` ハンドラで以下を制御する。MRU/削除を実装する際にまとめて整備する想定。

| 項目 | 条件 |
|---|---|
| 閉じる (ID_CLOSE) | `m_archivePath.empty()` なら GRAYED |
| 展開 (ID_EXTRACT) | `m_archivePath.empty()` なら GRAYED |
| テスト (ID_TEST) | `m_archivePath.empty()` なら GRAYED |
| 関連付けで開く (ID_OPEN_ASSOC) | アーカイブ未オープン or `m_openedWithUnrar` なら GRAYED |
| 削除 (ID_DELETE) | アーカイブ未オープン or `m_openedWithUnrar` なら GRAYED |
| 情報 (ID_INFO) | ListView に選択がない場合 GRAYED |
| ツリー表示 (IDM_VIEW_TREE) | 常時 ENABLED、状態のみ MF_CHECKED で反映 |
| 最近使ったアーカイブ | MRU が空なら子項目「(履歴なし)」を GRAYED で 1 件表示 |
