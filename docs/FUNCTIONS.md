# LiteDraw 関数リファレンス

ソースコードの読み方ガイドです。実装は主に `src/main.cpp`（アプリ本体）と `src/export_codec.cpp`（書き出し圧縮）に分かれています。

## src/export_codec.cpp / export_codec.h

| 関数 | 役割 |
|------|------|
| `Crc32` | PNG チャンク用 CRC32 を計算する（内部ヘルパー） |
| `AppendU32BE` | ビッグエンディアン 32bit 値をバッファ末尾に追加する |
| `WritePngChunk` | PNG チャンク（長さ・タイプ・データ・CRC）を組み立てる |
| `EncodeJpegTurbo` | BGRA ピクセル列を libjpeg-turbo で JPEG エンコード（quality 55, 4:2:0） |
| `PaethPredictor` | PNG フィルタ種別 4（Paeth）の予測値を求める |
| `FilterScanline` | 1 スキャンラインに PNG フィルタを適用する |
| `FilterHeuristicScore` | フィルタ候補のヒューリスティックスコア（小さいほど良） |
| `EncodePngLibdeflate` | BGRA を libdeflate で可逆 PNG エンコード（適応フィルタ・level 12、不透明なら RGB） |

## src/main.cpp — ユーティリティ

| 関数 | 役割 |
|------|------|
| `ShowError` | エラーメッセージボックスを表示する |
| `ToUtf8` / `FromUtf8` | ワイド文字列と UTF-8 バイト列を相互変換する |
| `ColorF` | ARGB uint32 を Direct2D 色に変換する |
| `PointInRect` | 点が矩形内か判定する |
| `GetWindowString` | ウィンドウのテキストを取得する |
| `PumpPendingMessages` | 処理中に保留メッセージをポンプする（UI フリーズ防止） |
| `CopyPixelRgba` | ピクセル 4 バイトをコピーする |
| `ResourceBytes` | 埋め込みリソース（フォント等）のバイト列を取得する |

## 設定（LiteDraw.ini）

| 関数 | 役割 |
|------|------|
| `SettingsPath` | exe 横の `LiteDraw.ini` パスを返す |
| `BuildMdiCodepoints` | MDI コードポイント表（`mdi-codepoints.txt`）を読み込む |
| `MdiNameToGlyph` | MDI アイコン名を Unicode グリフ 1 文字に変換する |
| `LoadSettings` | 色1/色2・書き出し軽量化設定などを ini から読み込む |
| `SaveSettings` | 現在の設定を ini に保存する |

## 画像入出力（WIC）

| 関数 | 役割 |
|------|------|
| `CreateWicBitmapFromPixels` | BGRA ピクセル列から WIC ビットマップを作成する |
| `LooksLikeSupportedImage` | 先頭バイトで PNG/JPEG/BMP か判定する |
| `ShowUnsupportedFormat` | 「未対応のフォーマットです」を表示する |
| `EncodePixels` | WIC で BGRA を JPEG/PNG/BMP 等にエンコードする |
| `DecodeSource` | WIC ソースから BGRA ピクセル列をデコードする |
| `DecodeMemory` | メモリ上の画像バイト列をデコードする |
| `ApplyExifOrientation` | EXIF Orientation に従いピクセルを回転・反転する |
| `DownscaleIfNeeded` | 編集上限（4096px）を超える場合に縮小する |
| `DecodeSourceForEditing` | 編集用デコード（EXIF 反映・必要なら縮小） |
| `LoadImageFile` | ファイルから画像を開きドキュメントを初期化する |
| `EnsureDocPng` | ドキュメントの PNG キャッシュを確保する |
| `EnsureDocBitmap` | Direct2D 用ドキュメントビットマップを作成する |
| `InvalidateDocBitmap` | ドキュメントビットマップを破棄し再作成を促す |

## オブジェクト永続化（.ldl）

| 関数 | 役割 |
|------|------|
| `WriteAppearance` / `ReadAppearance` | オブジェクトの見た目属性をバイナリに読み書きする |
| `SerializePayload` / `DeserializePayload` | 1 オブジェクト分のペイロードをシリアライズする |
| `SerializeObjects` / `DeserializeObjects` | 全オブジェクト列をシリアライズする |
| `SaveLdl` / `LoadLdl` | `.ldl` プロジェクトファイルの保存・読み込み |

## 履歴（Undo / Redo）

| 関数 | 役割 |
|------|------|
| `ResetHistory` | 履歴スタックをクリアする |
| `PushHistory` | 現在状態を履歴に積む |
| `RestoreHistoryEntry` | 履歴エントリからピクセル・オブジェクトを復元する |
| `Undo` | Undo または Redo を実行する |

## 描画・ヒットテスト

| 関数 | 役割 |
|------|------|
| `DrawArrowLine` | 矢印付き線分を描画する |
| `SquareLoupeRect` | ルーペ元枠を正方形に補正する |
| `PlaceLoupePreview` | ルーペの拡大プレビュー位置を決める |
| `HitTest` | オブジェクト上に点があるか判定する |
| `HitHandle` | 選択ハンドル（リサイズ用）のヒット判定 |
| `MoveObject` / `ResizeObject` | オブジェクトの移動・リサイズ |
| `HitCropHandle` / `ResizeCrop` | クロップ枠のハンドル操作 |
| `DrawSelectionHandles` | 選択中オブジェクトのハンドルを描画 |
| `DrawLoupeConnectors` | ルーペの補助線（対角 2 本）を描画 |
| `DrawLoupe` | 部分拡大オブジェクト全体を描画 |
| `DrawOutlinedStroke` | 縁取り付きストロークを描画 |
| `DrawTextWithOutline` | 縁取り付きテキストを描画 |
| `ExpandCalloutForText` | 吹き出しテキストに合わせサイズを拡張 |
| `CalloutTipSide` / `AddCalloutPath` | 吹き出しの三角先端と角丸本体のパス |
| `DrawCallout` | 吹き出しオブジェクトを描画 |
| `DrawObject` | 任意タイプのオブジェクトを描画 |
| `DrawDocument` | 背景画像＋全オブジェクトを描画 |
| `RenderFlattened` | 注釈を焼き込んだ BGRA ピクセル列を生成 |

## 書き出し

| 関数 | 役割 |
|------|------|
| `ExportImage` | フラット画像をファイルに書き出す（軽量化 ON 時は libjpeg/libdeflate と WIC を比較） |
| `FileDialog` | 汎用ファイル open/save ダイアログ |
| `ConfigureExportFileTypes` | 書き出しダイアログの形式リストを設定 |
| `ExportTypeAllowsLightweight` | 選択形式が軽量化対応か（BMP 以外） |
| `SyncExportLightweightControl` | 軽量化チェックボックスの有効/無効を同期 |
| `ExportDialogEvents` | 書き出しダイアログのイベント（形式変更・OK 時チェック取得） |
| `ExportFileDialog` | 軽量化チェック付き書き出しダイアログを表示 |

## ズーム・クロップ・リサイズ

| 関数 | 役割 |
|------|------|
| `FitOrigin` | ズームと原点をウィンドウに合わせる |
| `ZoomBy` | 倍率を変更して再レイアウト |
| `ApplyImageResize` | 画像全体を指定倍率で縮小 |
| `ApplyCrop` | クロップ枠内を切り出して確定 |
| `EffectPixels` | ぼかし/モザイク領域のピクセルを生成 |

## UI — プロパティ・ツール

| 関数 | 役割 |
|------|------|
| `LoadIconFont` / `ReleaseIconFont` | MDI フォントをメモリから読み込み・解放 |
| `ApplyButtonGlyph` | ツールバーボタンにグリフとラベルを設定 |
| `UpdateStampPickerSelection` | スタンプ選択状態を UI に反映 |
| `UpdateToolbarSelection` | 選択中ツールボタンをハイライト |
| `ComputeToolbarWidth` | リボン全体幅を計算 |
| `AddTooltip` | コントロールにツールチップを付ける |
| `ShowPropertyControl` | プロパティコントロールの表示切替 |
| `ConfigureComboValues` 等 | コンボ・スライダー操作ヘルパー |
| `HidePropertyExtras` | ツール別の余分なプロパティ UI を隠す |
| `RefreshColorBoxes` | 色1/色2 ボックスを再描画 |
| `ApplyStyle` / `SyncGlobalsFromObject` | オブジェクト ↔ グローバル描画属性の同期 |
| `ApplySelectionToProperties` | 選択オブジェクトの属性をプロパティ帯に反映 |
| `CommitObjectPropertyChange` | プロパティ変更をオブジェクトに確定 |
| `ReadPropertyControls` | プロパティ帯の値を読み取り適用 |
| `UpdateDraft` | ドラッグ中の下書きオブジェクトを更新 |
| `PlaceStampAt` / `AddPointObject` | スタンプ・点配置オブジェクトを追加 |
| `CommitInlineEdit` / `BeginInlineEdit` | インライン文字入力の確定・開始 |
| `DeleteSelected` | 選択オブジェクトを削除 |
| `SetTool` | 現在ツールを切り替え（UI・カーソル含む） |
| `FinishAwaitingCalloutTip` | 吹き出しの先端クリックフェーズを終了 |

## ファイル操作・コマンド

| 関数 | 役割 |
|------|------|
| `ConfirmDiscard` | 未保存破棄の確認 |
| `OpenDocumentFromPath` | パス指定でドキュメントを開く |
| `HandleDropFiles` | ドラッグ&ドロップでファイルを開く |
| `DoOpen` / `DoOverwriteSave` / `DoSave` / `DoExport` | メニュー/ツールバーから各操作を実行 |
| `ChooseColor` | 色1/色2 のカラーピッカーを開く |
| `ExecuteToolbarCommand` | ツールバー ID に応じたコマンドを実行 |

## ダイアログ

| 関数 | 役割 |
|------|------|
| `PromptProc` / `Prompt` | 1 行入力ダイアログ |
| `CloseProc` / `PromptUnsavedClose` | 終了時の未保存確認ダイアログ |
| `HelpProc` / `ShowHelpDialog` | ヘルプ・利用規約ダイアログ |
| `UpdateWindowTitle` | ウィンドウタイトル（ファイル名・変更フラグ）を更新 |

## レイアウト・ウィンドウプロシージャ

| 関数 | 役割 |
|------|------|
| `ApplyUiFont` | UI フォントを子ウィンドウに適用 |
| `Layout` | リボン・プロパティ帯・キャンバスの位置サイズを計算 |
| `CreateControls` | 子ウィンドウ（ボタン・スライダー等）を生成 |
| `EnsureCanvasTarget` | キャンバス用 Direct2D レンダーターゲットを確保 |
| `PaintCanvas` | キャンバスを Direct2D で描画 |
| `CanvasProc` | キャンバスのマウス・キー・ペイント処理 |
| `MainProc` | メインウィンドウのメッセージ処理 |
| `RegisterClasses` | ウィンドウクラスを登録 |
| `wWinMain` | エントリポイント（COM/D2D 初期化・メインループ） |

## 主要な型・グローバル

| 名前 | 役割 |
|------|------|
| `Object` | 注釈オブジェクト 1 件（矩形・点列・テキスト・色等） |
| `Document` | 画像ピクセル・オブジェクト列・パス・dirty フラグ |
| `HistoryEntry` | Undo 用スナップショット |
| `App` (`g`) | アプリ全体の状態（ウィンドウハンドル・ツール・ズーム等） |
| `Tool` / `ObjectType` | ツールとオブジェクト種別の列挙 |

## オブジェクト種別の補助

| 関数 | 役割 |
|------|------|
| `IsTwoPointObject` | 2 点で定義されるオブジェクトか判定 |
| `InvalidateCanvas` | キャンバス再描画を要求 |
