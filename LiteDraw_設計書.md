# LiteDraw 設計書

手順書添付用の画像編集ソフト。PNG/BMP/JPEGファイルを読み込み、矢印・図形・テキスト・フリーハンド・ぼかし・モザイク・番号スタンプ等の注釈をオブジェクトとして付与し、専用形式(.ldl)で保存・再編集、またはPNG/BMP/JPEGへフラット化して書き出す。

## 目次

1. [概要・要件](#1-概要要件)
2. [技術スタック](#2-技術スタック)
3. [.ldlファイルフォーマット仕様](#3-ldlファイルフォーマット仕様)
4. [クラス/構造体設計](#4-クラス構造体設計)
5. [メッセージハンドリング/状態遷移設計](#5-メッセージハンドリング状態遷移設計)
6. [残タスク・未確定事項](#6-残タスク未確定事項)

---

## 1. 概要・要件

### 1.1 用途

手順書に添付する画像に対して、注釈(矢印・図形・テキスト・番号スタンプ等)やぼかし・モザイクを施すための編集ソフト。Screenpressoの画像編集機能を参考にしているが、画面キャプチャ機能は持たず、既存の画像ファイルの編集に特化する。

### 1.2 スコープ

| 項目 | 内容 |
|---|---|
| 対象範囲 | 既存のPNG/BMP/JPEGファイルの編集のみ(画面キャプチャ機能なし) |
| UI形態 | 通常のウィンドウ + ツールパネル(ペイントソフト型) |
| 対応環境 | シングルモニターのみ |
| プロジェクト名 | LiteDraw |
| 専用拡張子 | `.ldl` (LiteDraw Layer File) |

### 1.3 注釈機能一覧

- 矢印
- 矩形
- 楕円
- テキスト
- フリーハンド手書き
- ぼかし(範囲指定、ブロック平均化)
- モザイク(範囲指定、ブロックサイズはUIで調整可能)
- 番号スタンプ(都度手動入力、自動連番なし)

### 1.4 ファイル入出力

| 項目 | 内容 |
|---|---|
| 入力対応形式 | PNG, BMP, JPEG |
| 出力対応形式 | PNG, BMP, JPEG(3択) |
| JPEG読み込み時 | EXIF Orientationを検出し自動回転補正、ピクセルデータへ焼き込む |
| 出力時 | EXIFメタデータは一切付与しない(手順書の軽量化のため) |
| プロジェクト保存 | `.ldl`形式でオブジェクト状態を保持し、再編集可能 |

### 1.5 注釈の保持方式

ラスター確定ではなく、**オブジェクトベース**で保持する。編集後も個々の注釈を選択・移動・プロパティ変更できる。

### 1.6 モザイクの実装方式

元画像を都度ブロック平均化する方式(縮小拡大による近似ではない)。ブロックサイズはUIで調整可能。

---

## 2. 技術スタック

サードパーティライブラリは使用せず、Windows標準APIのみで構成する。

| 要素 | 使用技術 |
|---|---|
| 画像の読み込み/保存 | WIC (Windows Imaging Component) |
| 描画・合成 | Direct2D 1.1 |
| ぼかし | ID2D1Effect の組み込みGaussianBlurエフェクト(範囲は矩形/自由形状クリップで限定適用) |
| モザイク | カスタム処理(CPU側でブロック平均化、またはピクセルシェーダーの独自エフェクト) |
| UIフレームワーク | Win32 API(メニュー・コモンコントロール) |
| Undo/Redo | コマンドパターン |

---

## 3. .ldlファイルフォーマット仕様

### 3.1 設計判断

入力がJPEGの場合でも、内部的にはEXIF回転補正後のピクセルデータを**PNG(可逆)に変換して保持**する。理由は、Undo/Redoや再エンコード時の実装を単純化するため。

### 3.2 ファイル全体構造

すべて**リトルエンディアン固定**。構造体の直接memcpyではなく、フィールド単位で明示的に読み書きする(アライメント問題回避、将来拡張性確保のため)。

```
┌────────────────────────────┐
│ File Header                 │
├────────────────────────────┤
│ Document Metadata Block     │
├────────────────────────────┤
│ Base Image Block (PNG格納)   │
├────────────────────────────┤
│ Annotation Object Block     │
│  (Object Count + Object[])  │
└────────────────────────────┘
```

### 3.3 File Header

| フィールド | 型 | サイズ | 内容 |
|---|---|---|---|
| Magic | char[4] | 4 | `"LDL\0"` 固定 |
| FormatVersion | uint32 | 4 | 現時点 `1` |
| HeaderSize | uint32 | 4 | ヘッダ全体バイト数(将来拡張用) |

### 3.4 Document Metadata Block

| フィールド | 型 | サイズ | 内容 |
|---|---|---|---|
| ImageWidth | uint32 | 4 | 元画像の幅(px) |
| ImageHeight | uint32 | 4 | 元画像の高さ(px) |
| CreatedTime | uint64 | 8 | FILETIME形式 |
| ModifiedTime | uint64 | 8 | FILETIME形式 |
| OriginalFileNameLen | uint16 | 2 | 元ファイル名の文字数 |
| OriginalFileName | UTF-8 | 可変 | 参照表示用(再読込には使わない) |

### 3.5 Base Image Block

| フィールド | 型 | サイズ | 内容 |
|---|---|---|---|
| ImageDataLength | uint64 | 8 | 後続PNGバイト列の長さ |
| ImageData | byte[] | 可変 | PNGエンコード済みバイト列(WICで再デコード) |

### 3.6 Annotation Object Block

| フィールド | 型 | サイズ | 内容 |
|---|---|---|---|
| ObjectCount | uint32 | 4 | オブジェクト総数 |
| Objects | Object[] | 可変 | 下記フォーマットの繰り返し |

#### 各オブジェクト共通ヘッダ

| フィールド | 型 | サイズ | 内容 |
|---|---|---|---|
| ObjectId | uint32 | 4 | 一意なID(採番はDocument側でカウンタ管理) |
| ObjectType | uint8 | 1 | 下記Enum参照 |
| ZOrder | int32 | 4 | 描画順(大きいほど手前) |
| Visible | uint8 | 1 | 表示/非表示フラグ |
| PayloadLength | uint32 | 4 | 後続の型別データ長(将来バージョン間の互換性のため) |
| Payload | 可変 | 可変 | 型別データ本体 |

```cpp
enum ObjectType : uint8_t {
    Arrow = 1,
    Rectangle = 2,
    Ellipse = 3,
    Text = 4,
    Freehand = 5,
    BlurRegion = 6,
    MosaicRegion = 7,
    NumberStamp = 8,
};
```

#### 型別Payload

| 種別 | フィールド構成 |
|---|---|
| Arrow | StartX,StartY,EndX,EndY(float32×4) / LineWidth(float32) / Color(uint32 RGBA) / ArrowHeadSize(float32) |
| Rectangle | X,Y,W,H(float32×4) / LineWidth(float32) / Color(uint32) / HasFill(uint8) / FillColor(uint32) |
| Ellipse | Rectangleと同一構成(境界矩形として保持) |
| Text | X,Y(float32×2) / StrLen(uint32)+文字列(UTF-8) / FontNameLen(uint16)+フォント名(UTF-8) / FontSize(float32) / Color(uint32) / HasBackground(uint8) / BackgroundColor(uint32) |
| Freehand | PointCount(uint32) / Points(float32 x,y の配列) / LineWidth(float32) / Color(uint32) |
| BlurRegion | RegionKind(uint8: 0=矩形,1=自由形状) / 矩形ならX,Y,W,H(float32×4)、自由形状ならPointCount+Points / BlurStrength(float32) |
| MosaicRegion | BlurRegionと同一のRegion定義 / BlockSize(uint32) |
| NumberStamp | X,Y(float32×2) / NumberTextLen(uint8)+番号文字列(UTF-8、手動入力なので可変長) / Diameter(float32) / Color(uint32) |

### 3.7 設計上の補足事項

1. **PayloadLengthを持たせる理由**: 将来LiteDrawのバージョンアップでフィールドが増えても、古いバージョンのリーダーが「知らないフィールドを読み飛ばして後続オブジェクトへ進む」ことができるようにするため。
2. **座標系**: 元画像のピクセル座標系(左上原点)で統一して保持する。表示側のズーム倍率はドキュメントの状態として扱わず、揮発性(.ldlには保存しない)とする。
3. **色の型**: `uint32`でRGBAに統一する。D2D1の`D2D1_COLOR_F`はfloatのRGBA(0.0〜1.0)なので、ファイル上はuint8×4のRGBAで持ち、読み込み時にfloatへ変換する。

---

## 4. クラス/構造体設計

### 4.1 AnnotationObject 階層

```cpp
class AnnotationObject {
public:
    uint32_t objectId;
    int32_t  zOrder;
    bool     visible;
    bool     selected;      // 実行時のみ、.ldlには保存しない

    virtual ObjectType GetType() const = 0;
    virtual bool       HitTest(D2D1_POINT_2F pt) const = 0;
    virtual D2D1_RECT_F GetBoundingBox() const = 0;
    virtual std::vector<D2D1_POINT_2F> GetHandles() const = 0; // リサイズハンドル座標
    virtual void       MoveBy(float dx, float dy) = 0;
    virtual void       ResizeByHandle(int handleIndex, D2D1_POINT_2F newPos) = 0;
    virtual void       Draw(ID2D1DeviceContext* dc) const = 0;
    virtual void       Serialize(ByteWriter& w) const = 0;
    virtual void       Deserialize(ByteReader& r) = 0;
    virtual std::unique_ptr<AnnotationObject> Clone() const = 0;
};
```

`BlurRegion` / `MosaicRegion` は `Draw()` の中で「元画像から対象範囲を切り出してエフェクト適用 → 合成」という特殊処理になるため、共通の `RegionBasedEffectObject` という中間基底クラスを挟む。

```cpp
class RegionBasedEffectObject : public AnnotationObject {
protected:
    RegionKind regionKind;              // Rect or Freeform
    D2D1_RECT_F rectRegion;             // RegionKind::Rectの場合
    std::vector<D2D1_POINT_2F> freeformPoints; // RegionKind::Freeformの場合
    // 派生: BlurRegionObject(blurStrength), MosaicRegionObject(blockSize)
};
```

### 4.2 Document クラス

```cpp
class Document {
public:
    ComPtr<ID2D1Bitmap> baseImage;          // 元画像(EXIF補正済み)
    UINT imageWidth, imageHeight;
    std::wstring originalFileName;
    FILETIME createdTime, modifiedTime;

    std::vector<std::unique_ptr<AnnotationObject>> objects;
    uint32_t nextObjectId = 1;
    bool isDirty = false;

    CommandManager commandManager;

    AnnotationObject* FindObjectById(uint32_t id);
    AnnotationObject* HitTestTopmost(D2D1_POINT_2F pt); // ZOrder降順で最初にヒットしたもの

    bool SaveToLdl(const std::wstring& path);
    bool LoadFromLdl(const std::wstring& path);
    bool ExportFlattened(const std::wstring& path, ImageFormat fmt); // PNG/BMP/JPEG書き出し
};
```

Undo/Redoは`Document`ではなく`CommandManager`に集約し、`Document`は「今の状態」だけを持つ薄いデータ保持役に留める。

### 4.3 CommandManager / ICommand

```cpp
class ICommand {
public:
    virtual void Execute() = 0;
    virtual void Undo() = 0;
    virtual ~ICommand() = default;
};

class CommandManager {
    std::vector<std::unique_ptr<ICommand>> undoStack;
    std::vector<std::unique_ptr<ICommand>> redoStack;
public:
    void Execute(std::unique_ptr<ICommand> cmd) {
        cmd->Execute();
        undoStack.push_back(std::move(cmd));
        redoStack.clear();
    }
    void Undo();
    void Redo();
};
```

具体的なコマンド:

| コマンド | Execute内容 | Undo内容 |
|---|---|---|
| AddObjectCommand | objects へ追加 | objects から削除 |
| DeleteObjectCommand | objects から削除(所有権はコマンド側が保持) | objects へ再追加 |
| MoveObjectCommand | 差分(dx,dy)を適用 | 逆方向に適用 |
| ResizeCommand | ハンドル操作後の形状を適用 | 操作前の形状(スナップショット)に戻す |
| ModifyPropertyCommand | 新プロパティ値を適用 | 旧プロパティ値(保持しておく)に戻す |
| ReorderCommand | ZOrderを変更 | 元のZOrderに戻す |

### 4.4 Canvas / View クラス

描画・入力処理を担当。`Document`とは分離し、「今どのツールが選択されているか」「編集中の一時状態」など揮発性の情報を持つ。

```cpp
enum class ToolMode {
    Select, Arrow, Rectangle, Ellipse, Text, Freehand,
    Blur, Mosaic, NumberStamp
};

enum class InteractionState {
    Idle, Drawing, Dragging, Resizing, TextEditing, RubberBandSelecting
};

class CanvasView {
    Document* doc;
    ToolMode currentTool = ToolMode::Select;
    InteractionState state = InteractionState::Idle;

    AnnotationObject* selectedObject = nullptr;
    std::unique_ptr<AnnotationObject> objectInProgress; // Drawing中の仮オブジェクト
    D2D1_POINT_2F dragStartPoint;
    D2D1_POINT_2F objectStartPositionSnapshot; // Move/Resize確定時のUndo用
    int activeHandleIndex = -1;

    float zoomFactor = 1.0f;
    D2D1_POINT_2F panOffset = {0, 0};

public:
    void OnMouseDown(D2D1_POINT_2F pt);
    void OnMouseMove(D2D1_POINT_2F pt);
    void OnMouseUp(D2D1_POINT_2F pt);
    void Render(ID2D1DeviceContext* dc);
};
```

ズーム・パンは揮発性(.ldlに保存しない)として`CanvasView`側に保持する。

### 4.5 アプリケーション全体構成

```
MainWindow (HWND)
├─ MenuBar
├─ ToolPanelWindow (子HWND, 左固定)
├─ CanvasHostWindow (子HWND, D3D11+D2Dスワップチェーン保持)
│    └─ CanvasView(上記)
└─ PropertyPanelWindow (子HWND, 下 or 右)

Document ← CanvasView が参照
CommandManager ← Document が保持、UndoボタンやCtrl+Zから呼ばれる
```

シングルモニター前提のため、モニター毎のスワップチェーン管理は不要でシンプルな構成となる。

---

## 5. メッセージハンドリング/状態遷移設計

### 5.1 状態遷移図

```
[Idle] --(ツール=図形系でLBUTTONDOWN)--> [Drawing]
[Idle] --(ツール=Selectでオブジェクト本体をLBUTTONDOWN)--> [Dragging]
[Idle] --(ツール=Selectでハンドルをヒット)--> [Resizing]
[Idle] --(ツール=Selectで何もない場所をLBUTTONDOWN)--> [RubberBandSelecting]
[Idle] --(ツール=Textでキャンバスをクリック)--> [TextEditing]

[Drawing] --(MOUSEMOVE)--> [Drawing] (仮オブジェクトの終点を更新して再描画)
[Drawing] --(LBUTTONUP)--> [Idle] (AddObjectCommandをCommandManagerへExecute)

[Dragging] --(MOUSEMOVE)--> [Dragging] (選択オブジェクトを差分移動、再描画のみ・コマンド発行はまだ)
[Dragging] --(LBUTTONUP)--> [Idle] (MoveObjectCommandをExecute。差分ゼロならコマンド発行しない)

[Resizing] --(MOUSEMOVE)--> [Resizing] (ハンドル追従で形状更新)
[Resizing] --(LBUTTONUP)--> [Idle] (ResizeCommandをExecute)

[TextEditing] --(確定操作 / フォーカスアウト)--> [Idle] (文字列が空ならオブジェクト破棄、非空ならAddObjectCommand)

[RubberBandSelecting] --(MOUSEMOVE)--> [RubberBandSelecting] (矩形内のオブジェクトをハイライト)
[RubberBandSelecting] --(LBUTTONUP)--> [Idle] (矩形内オブジェクトを選択状態に)
```

### 5.2 WM_LBUTTONDOWN の分岐ロジック

```cpp
void CanvasView::OnMouseDown(D2D1_POINT_2F pt) {
    if (currentTool == ToolMode::Select) {
        if (selectedObject) {
            int handle = HitTestHandles(selectedObject, pt);
            if (handle >= 0) {
                activeHandleIndex = handle;
                objectStartPositionSnapshot = /* 現在形状を保存 */;
                state = InteractionState::Resizing;
                return;
            }
        }
        AnnotationObject* hit = doc->HitTestTopmost(pt);
        if (hit) {
            selectedObject = hit;
            dragStartPoint = pt;
            objectStartPositionSnapshot = /* 現在位置を保存 */;
            state = InteractionState::Dragging;
        } else {
            selectedObject = nullptr;
            dragStartPoint = pt;
            state = InteractionState::RubberBandSelecting;
        }
    } else if (currentTool == ToolMode::Text) {
        state = InteractionState::TextEditing;
        // インラインテキストボックス(EDITコントロール等)をpt位置に表示
    } else {
        // 図形系ツール: 新規オブジェクトを仮生成
        objectInProgress = CreateObjectForTool(currentTool, pt);
        state = InteractionState::Drawing;
    }
}
```

### 5.3 キーボード操作

| キー | 動作 |
|---|---|
| Delete | 選択中オブジェクトを `DeleteObjectCommand` でCommandManagerへExecute |
| Ctrl+Z | `CommandManager::Undo()` |
| Ctrl+Y | `CommandManager::Redo()` |
| Ctrl+S | `Document::SaveToLdl()`(未保存パスならファイル選択ダイアログ) |
| Esc | Drawing/TextEditing中なら操作キャンセル、Idleなら選択解除 |
| 矢印キー | 選択オブジェクトを1px単位で微調整移動(Shift併用で10px等) |

### 5.4 重要な設計上の注意点

1. **ドラッグ中は毎フレームコマンドを発行しない**: `Dragging`/`Resizing`状態の間は見た目だけを更新し、`LBUTTONUP`で確定した時に一度だけコマンドを`Execute`する。そうしないとUndoスタックがマウス移動のたびに積み上がってしまう。
2. **RubberBandSelectingの複数選択**: 現仕様には明示的に含まれていない。複数選択が不要であれば矩形選択は「単一オブジェクトのみ選択」に単純化してもよい。必要であれば`selectedObject`を`std::vector<AnnotationObject*>`に拡張する。
3. **座標変換**: マウス座標(スクリーン/クライアント座標)⇔画像ピクセル座標の変換を`zoomFactor`と`panOffset`を使って`CanvasView`に一元化しておくと、ヒットテストや描画のバグを防ぎやすい。

---

## 6. 残タスク・未確定事項

- ツールパネルのアイコン/レイアウトの具体デザイン
- プロパティパネルの項目(選択オブジェクトの種別によって表示内容を切り替えるUI)
- フォント選択の範囲(システムフォント全部か、限定リストか)
- 複数選択・グループ化の要否(5.4節の論点)
- .ldlの拡張子アイコン・エクスプローラー関連付け(任意)

---

## 実装フェーズで使用する主要技術要素(参考)

| 要素 | 使用API/手法 |
|---|---|
| ウィンドウ作成 | Win32 API(`CreateWindowEx`等)、シングルモニター構成 |
| 描画基盤 | Direct3D 11 + DXGI スワップチェーン + Direct2D 1.1 デバイスコンテキスト |
| 画像デコード | WIC (`IWICBitmapDecoder`) |
| JPEG EXIF回転補正 | メタデータクエリ(`/app1/ifd/{ushort=274}`)からOrientation取得 → `IWICBitmapFlipRotator`で回転/反転 |
| ぼかしエフェクト | `ID2D1Effect`(組み込みGaussianBlur) |
| モザイク処理 | 独自実装(ブロック単位の平均化) |
| 出力エンコード | WIC (`IWICBitmapEncoder`)、メタデータライター不使用でEXIF非付与 |
