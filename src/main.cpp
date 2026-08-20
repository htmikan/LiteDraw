#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <d2d1.h>
#include <dwrite.h>
#include <dwrite_3.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>
#include <cstdint>
#include <cstring>
#include <functional>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "resource.h"
#include "export_codec.h"

using Microsoft::WRL::ComPtr;

namespace {

// 関数一覧・役割は docs/FUNCTIONS.md を参照。

constexpr wchar_t kMainClass[] = L"LiteDraw.Main";
constexpr wchar_t kCanvasClass[] = L"LiteDraw.Canvas";
constexpr wchar_t kPromptClass[] = L"LiteDraw.Prompt";
constexpr wchar_t kCloseClass[] = L"LiteDraw.Close";
constexpr wchar_t kHelpClass[] = L"LiteDraw.Help";
constexpr UINT_PTR kCanvasId = 60001;
constexpr float kPi = 3.14159265358979323846f;
constexpr int kRibbonButton    = 60;   // ツールバーボタン1個のサイズ（px）
constexpr int kRibbonGap       = 2;    // ボタン間の隙間
constexpr int kRibbonTop       = 6;    // リボン上端マージン
constexpr int kRibbonRows      = 2;    // リボン行数
constexpr int kRibbonLabelHeight = 18; // グループラベル高さ
constexpr int kRibbonGroupGap  = 8;    // グループ間の隙間
// ツールバー全体高さ = 上マージン + 2行 * ボタン + 行間 + 下マージン + ラベル + ラベル下マージン
constexpr int kToolbarHeight   = kRibbonTop + kRibbonRows * kRibbonButton + (kRibbonRows - 1) * kRibbonGap + 4 + kRibbonLabelHeight + 4;
constexpr int kStampCount      = 15;
constexpr int kStampPickSize   = 40;   // スタンプ選択ボタンのサイズ（ツールバーアイコンの0.5倍）
// プロパティ行高さ：スタンプボタン(40px)を収めるため上下8pxマージン
constexpr int kPropertyHeight  = kStampPickSize + 16;
// アイコンは Material Design Icons（Apache 2.0）のグリフをフォントから直接描画する
constexpr wchar_t kIconFontFamily[] = L"Material Design Icons";
constexpr int kRibbonGlyphSize = kRibbonButton * 7 / 10;     // ボタン内のグリフ寸法
constexpr int kStampGlyphSize  = kStampPickSize * 7 / 10;
constexpr wchar_t kVersion[] = L"20260820";
constexpr wchar_t kGitHubUrl[] = L"https://github.com/htmikan/LiteDraw";
constexpr UINT kMaxImageEdge = 16384;
constexpr uint64_t kMaxImagePixels = 16384ull * 16384ull;
// 編集用の実効上限（超える場合は読み込み時に自動縮小）
constexpr UINT kMaxWorkingEdge = 4096;

enum class ObjectType : uint8_t {
    Arrow = 1, Rectangle, Ellipse, Text, Freehand, BlurRegion, MosaicRegion, NumberStamp, Loupe, Callout, Line, IconStamp
};

enum class Tool {
    Select, Crop, Line, Arrow, Rectangle, Ellipse, Freehand, Text, Callout, Loupe, Blur, Mosaic, Number, Stamp
};

enum class PropertyKind {
    None, Text, Number, Stroke, Shape, Line, Blur, Mosaic, Loupe, Resize, Callout, Crop, Stamp
};

struct Object {
    uint32_t id = 0;
    ObjectType type = ObjectType::Rectangle;
    int32_t z = 0;
    bool visible = true;
    D2D1_RECT_F rect{};
    std::vector<D2D1_POINT_2F> points;
    std::wstring text;
    std::wstring font = L"Segoe UI";
    float width = 3.0f;
    uint32_t color = 0xE53935FFU;
    uint32_t fillColor = 0xFFFFFFFFU;
    float extra = 16.0f;
    uint32_t blockSize = 12;
    bool fill = false;
    bool hasOutline = false;
    float outlineWidth = 4.0f;
    float outlineOpacity = 1.0f;
    uint8_t dashStyle = 0;
};

struct Document {
    UINT width = 0;
    UINT height = 0;
    std::vector<uint8_t> pixels;
    std::vector<uint8_t> png;
    std::wstring originalName;
    std::wstring sourcePath;
    std::wstring projectPath;
    FILETIME created{};
    FILETIME modified{};
    std::vector<Object> objects;
    uint32_t nextId = 1;
    bool dirty = false;
};

struct HistoryEntry {
    UINT width = 0;
    UINT height = 0;
    std::vector<uint8_t> pixels;
    std::vector<uint8_t> png;
    std::vector<uint8_t> objects;
    uint32_t nextId = 1;
};

struct PromptState {
    std::wstring title;
    std::wstring value;
    bool accepted = false;
    HWND edit{};
};

struct ToolButton {
    int id;
    bool selectable;
    Tool tool;
    const wchar_t* label;
    const wchar_t* glyph;
};

struct App {
    HINSTANCE instance{};
    HWND mainWindow{};
    HWND canvas{};
    HWND tooltip{};
    HWND propLabel[3]{};
    HWND propValue[3]{};
    HWND propWidth{};
    HWND propWidthLabel{};
    HWND propOutline{};
    HWND propOutlineWidth{};
    HWND propOutlineWidthLabel{};
    HWND propOpacity{};
    HWND propOpacityLabel{};
    HWND propDiameter{};
    HWND propDiameterLabel{};
    HWND propFill{};
    HWND propSizeInfo{};
    HWND propApply{};
    HWND color1Box{};
    HWND color2Box{};
    HWND color1Label{};
    HWND color2Label{};
    HWND stampPickButtons[kStampCount]{};
    HFONT uiFont{};
    HFONT propFont{};
    HFONT ribbonIconFont{};
    HFONT stampIconFont{};
    HANDLE iconFontResource{};
    int colorPick = 1;
    bool iconFontLoaded = false;
    uint32_t selectedStampIndex = 0;
    float stampSize = 48.0f;
    ComPtr<IDWriteFontCollection> iconFontCollection;
    ComPtr<ID2D1Factory> d2d;
    ComPtr<IDWriteFactory> dwrite;
    ComPtr<IWICImagingFactory> wic;
    ComPtr<ID2D1HwndRenderTarget> target;
    ComPtr<ID2D1Bitmap> docBitmap;
    UINT docBitmapWidth = 0;
    UINT docBitmapHeight = 0;
    ComPtr<ID2D1StrokeStyle> roundStroke;
    ComPtr<ID2D1StrokeStyle> dashStroke;
    ComPtr<ID2D1StrokeStyle> dashDotStroke;
    ComPtr<ID2D1StrokeStyle> dashDotDotStroke;
    Document doc;
    Tool tool = Tool::Select;
    std::optional<Object> draft;
    int selected = -1;
    bool dragging = false;
    bool resizing = false;
    bool panning = false;
    bool cropActive = false;
    bool movingCrop = false;
    bool resizingCrop = false;
    bool showingResize = false;
    bool updatingUi = false;
    bool textEditing = false;
    bool caretVisible = true;
    bool awaitingCalloutTip = false;
    bool movingLoupePreview = false;
    int activeHandle = -1;
    int activeCropHandle = -1;
    POINT panLast{};
    D2D1_POINT_2F down{};
    D2D1_POINT_2F last{};
    D2D1_RECT_F cropRect{};
    float zoom = 1.0f;
    float dpiScale = 1.0f;   // 物理ピクセル / DIP (PerMonitor DPI対応)
    D2D1_POINT_2F origin{16.0f, 16.0f};
    float lineWidth = 3.0f;
    uint32_t effectSize = 12;
    bool effectCircle = false;
    uint32_t numberDiameter = 42;
    float textSize = 24.0f;
    float loupeZoom = 2.0f;
    bool loupeCircle = false;
    std::wstring textFont = L"Segoe UI";
    uint32_t color1 = 0xE53935FFU;
    uint32_t color2 = 0xFFFFFFFFU;
    bool outlineEnabled = false;
    float outlineWidth = 4.0f;
    float outlineOpacity = 1.0f;
    bool fillEnabled = false;
    bool arrowFlip = false;
    uint8_t dashStyle = 0;
    float pendingResizeScale = 0.0f;
    bool exportLightweight = false;
    std::vector<HistoryEntry> history;
    size_t historyPos = 0;
};

App g;

struct Writer {
    std::vector<uint8_t> data;
    template<class T> void pod(T value) {
        static_assert(std::is_trivially_copyable_v<T>);
        const auto* p = reinterpret_cast<const uint8_t*>(&value);
        data.insert(data.end(), p, p + sizeof(T));
    }
    void bytes(std::span<const uint8_t> value) { data.insert(data.end(), value.begin(), value.end()); }
    void bytes(std::string_view value) {
        const auto* p = reinterpret_cast<const uint8_t*>(value.data());
        data.insert(data.end(), p, p + value.size());
    }
};

struct Reader {
    std::span<const uint8_t> data;
    size_t pos = 0;
    bool ok = true;
    template<class T> T pod() {
        T value{};
        if (pos + sizeof(T) > data.size()) { ok = false; return value; }
        std::memcpy(&value, data.data() + pos, sizeof(T));
        pos += sizeof(T);
        return value;
    }
    std::span<const uint8_t> bytes(size_t count) {
        if (pos + count > data.size()) { ok = false; return {}; }
        auto result = data.subspan(pos, count);
        pos += count;
        return result;
    }
};

struct RibbonGroupDef {
    const wchar_t* label;
    int cols;
    int rows;
    std::initializer_list<int> ids;
};

struct RibbonGroupLayout {
    RECT bounds{};
    const wchar_t* label = nullptr;
};

const std::array<ToolButton, 27> kToolbarButtons{{
    {IDC_CMD_OPEN, false, Tool::Select, L"ファイルを開く", L"\U000F09EE"},
    {IDC_CMD_SAVE, false, Tool::Select, L"上書き保存", L"\U000F0CFC"},
    {IDC_CMD_SAVE_AS, false, Tool::Select, L"レイヤーを保存", L"\U000F145C"},
    {IDC_CMD_EXPORT, false, Tool::Select, L"書き出し", L"\U000F0E28"},
    {IDC_CMD_RESIZE, false, Tool::Select, L"サイズ縮小", L"\U000F0A68"},
    {IDC_TOOL_CROP, true, Tool::Crop, L"サイズ切り取り", L"\U000F0655"},
    {IDC_TOOL_SELECT, true, Tool::Select, L"選択", L"\U000F0CFD"},
    {IDC_TOOL_LINE, true, Tool::Line, L"直線", L"\U000F0FDF"},
    {IDC_TOOL_ARROW, true, Tool::Arrow, L"矢印", L"\U000F19B4"},
    {IDC_TOOL_RECT, true, Tool::Rectangle, L"矩形", L"\U000F0E5F"},
    {IDC_TOOL_ELLIPSE, true, Tool::Ellipse, L"楕円", L"\U000F0EA1"},
    {IDC_TOOL_FREEHAND, true, Tool::Freehand, L"フリーハンド", L"\U000F07CB"},
    {IDC_TOOL_TEXT, true, Tool::Text, L"テキスト", L"\U000F0E32"},
    {IDC_TOOL_CALLOUT, true, Tool::Callout, L"吹き出し", L"\U000F0189"},
    {IDC_TOOL_LOUPE, true, Tool::Loupe, L"部分拡大", L"\U000F0345"},
    {IDC_TOOL_BLUR, true, Tool::Blur, L"ぼかし", L"\U000F0E0A"},
    {IDC_TOOL_MOSAIC, true, Tool::Mosaic, L"モザイク", L"\U000F1854"},
    {IDC_TOOL_NUMBER, true, Tool::Number, L"番号", L"\U000F0CA1"},
    {IDC_TOOL_STAMP, true, Tool::Stamp, L"スタンプ", L"\U000F0D39"},
    {IDC_CMD_UNDO, false, Tool::Select, L"元に戻す", L"\U000F054C"},
    {IDC_CMD_REDO, false, Tool::Select, L"やり直す", L"\U000F044E"},
    {IDC_CMD_DELETE, false, Tool::Select, L"削除", L"\U000F09E7"},
    {IDC_CMD_ZOOM_IN, false, Tool::Select, L"拡大", L"\U000F06ED"},
    {IDC_CMD_ZOOM_100, false, Tool::Select, L"等倍", L"\U000F0349"},
    {IDC_CMD_ZOOM_OUT, false, Tool::Select, L"縮小", L"\U000F06EC"},
    {IDC_CMD_HELP, false, Tool::Select, L"ヘルプ", L"\U000F0625"},
    {IDC_CMD_EXIT, false, Tool::Select, L"終了", L"\U000F0A48"},
}};

const std::array<RibbonGroupDef, 6> kRibbonGroups{{
    {L"ファイル", 2, 2, {IDC_CMD_OPEN, IDC_CMD_SAVE, IDC_CMD_SAVE_AS, IDC_CMD_EXPORT}},
    {L"サイズ縮小", 1, 2, {IDC_TOOL_CROP, IDC_CMD_RESIZE}},
    {L"操作・履歴", 2, 2, {IDC_TOOL_SELECT, IDC_CMD_DELETE, IDC_CMD_UNDO, IDC_CMD_REDO}},
    {L"描画・注釈", 6, 2, {IDC_TOOL_LINE, IDC_TOOL_ARROW, IDC_TOOL_RECT, IDC_TOOL_ELLIPSE, IDC_TOOL_FREEHAND, IDC_TOOL_TEXT,
                            IDC_TOOL_CALLOUT, IDC_TOOL_LOUPE, IDC_TOOL_BLUR, IDC_TOOL_MOSAIC, IDC_TOOL_NUMBER, IDC_TOOL_STAMP}},
    {L"拡大・縮小", 2, 2, {IDC_CMD_ZOOM_100, IDC_CMD_ZOOM_IN, 0, IDC_CMD_ZOOM_OUT}},
    {L"閉じる", 1, 2, {IDC_CMD_EXIT, IDC_CMD_HELP}},
}};

// ini から読み込み後に設定される動的スタンプ定義（初期値はデフォルト15種）
struct StampEntry { std::wstring glyph; std::wstring label; };
std::array<StampEntry, kStampCount> g_stampEntries{{
    {L"\U000F0A54", L"hand-pointing-left"},
    {L"\U000F02C7", L"hand-pointing-right"},
    {L"\U000F182D", L"hand-back-right-outline"},
    {L"\U000F0465", L"rotate-left"},
    {L"\U000F0467", L"rotate-right"},
    {L"\U000F0741", L"gesture-tap"},
    {L"\U000F05D6", L"alert-circle-outline"},
    {L"\U000F0CE5", L"alert-decagram-outline"},
    {L"\U000F05E1", L"check-circle-outline"},
    {L"\U000F015A", L"close-circle-outline"},
    {L"\U000F0625", L"help-circle-outline"},
    {L"\U000F02FD", L"information-outline"},
    {L"\U000F033E", L"lock"},
    {L"\U000F0416", L"plus-box"},
    {L"\U000F0375", L"minus-box"},
}};

// アクセサ（後方互換）
inline const wchar_t* kStampGlyphs(uint32_t i) { return g_stampEntries[i].glyph.c_str(); }
inline const wchar_t* kStampLabels(uint32_t i) { return g_stampEntries[i].label.c_str(); }

std::array<RibbonGroupLayout, kRibbonGroups.size()> g_ribbonGroups{};

// --- ユーティリティ ---

void ShowError(HWND owner, const wchar_t* text) { MessageBoxW(owner, text, L"LiteDraw", MB_OK | MB_ICONERROR); }

std::string ToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), count, nullptr, nullptr);
    return result;
}

std::wstring FromUtf8(std::span<const uint8_t> value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, reinterpret_cast<const char*>(value.data()), static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring result(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, reinterpret_cast<const char*>(value.data()), static_cast<int>(value.size()), result.data(), count);
    return result;
}

D2D1_COLOR_F ColorF(uint32_t rgba) {
    return D2D1::ColorF(static_cast<float>((rgba >> 24) & 255U) / 255.0f,
        static_cast<float>((rgba >> 16) & 255U) / 255.0f,
        static_cast<float>((rgba >> 8) & 255U) / 255.0f,
        static_cast<float>(rgba & 255U) / 255.0f);
}

COLORREF RgbaToColorRef(uint32_t value) { return RGB((value >> 24) & 255U, (value >> 16) & 255U, (value >> 8) & 255U); }
uint32_t ColorRefToRgba(COLORREF value) {
    return (static_cast<uint32_t>(GetRValue(value)) << 24) | (static_cast<uint32_t>(GetGValue(value)) << 16) |
        (static_cast<uint32_t>(GetBValue(value)) << 8) | 0xFFU;
}

float DistanceToSegment(D2D1_POINT_2F p, D2D1_POINT_2F a, D2D1_POINT_2F b) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float length2 = dx * dx + dy * dy;
    const float t = length2 > 0.0f ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / length2, 0.0f, 1.0f) : 0.0f;
    return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

D2D1_RECT_F NormalizeRect(D2D1_RECT_F rect) {
    if (rect.left > rect.right) std::swap(rect.left, rect.right);
    if (rect.top > rect.bottom) std::swap(rect.top, rect.bottom);
    return rect;
}

bool PointInRect(D2D1_RECT_F rect, D2D1_POINT_2F p) {
    rect = NormalizeRect(rect);
    return p.x >= rect.left && p.x <= rect.right && p.y >= rect.top && p.y <= rect.bottom;
}

std::wstring GetWindowString(HWND hwnd) {
    const int len = GetWindowTextLengthW(hwnd);
    std::wstring value(static_cast<size_t>(len), L'\0');
    if (len > 0) GetWindowTextW(hwnd, value.data(), len + 1);
    return value;
}

HRESULT CreateWicBitmapFromPixels(const std::vector<uint8_t>& pixels, UINT width, UINT height, IWICBitmap** bitmap) {
    if (width == 0 || height == 0) return E_INVALIDARG;
    const uint64_t stride64 = static_cast<uint64_t>(width) * 4U;
    const uint64_t bytes64 = stride64 * height;
    if (stride64 > UINT_MAX || bytes64 != pixels.size()) return E_INVALIDARG;
    HRESULT hr = g.wic->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, bitmap);
    if (FAILED(hr)) return hr;
    WICRect lockRect{0, 0, static_cast<INT>(width), static_cast<INT>(height)};
    ComPtr<IWICBitmapLock> lock;
    hr = (*bitmap)->Lock(&lockRect, WICBitmapLockWrite, &lock);
    if (FAILED(hr)) return hr;
    UINT stride = 0, bufferSize = 0;
    BYTE* data = nullptr;
    hr = lock->GetStride(&stride);
    if (SUCCEEDED(hr)) hr = lock->GetDataPointer(&bufferSize, &data);
    if (FAILED(hr) || !data) return FAILED(hr) ? hr : E_FAIL;
    for (UINT y = 0; y < height; ++y) {
        std::memcpy(data + static_cast<size_t>(y) * stride,
                    pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(width) * 4U,
                    static_cast<size_t>(width) * 4U);
    }
    return S_OK;
}

bool LooksLikeSupportedImage(std::span<const uint8_t> bytes) {
    if (bytes.size() >= 8 && bytes[0] == 0x89 && bytes[1] == 0x50 && bytes[2] == 0x4E && bytes[3] == 0x47 &&
        bytes[4] == 0x0D && bytes[5] == 0x0A && bytes[6] == 0x1A && bytes[7] == 0x0A) return true;
    if (bytes.size() >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF) return true;
    if (bytes.size() >= 2 && bytes[0] == 'B' && bytes[1] == 'M') return true;
    return false;
}

void ShowUnsupportedFormat() { ShowError(g.mainWindow ? g.mainWindow : nullptr, L"未対応のフォーマットです"); }

std::span<const uint8_t> ResourceBytes(int resourceId); // 前方宣言

// --- 設定 (LiteDraw.ini) ---

std::wstring SettingsPath() {
    wchar_t modulePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    return (std::filesystem::path(modulePath).parent_path() / L"LiteDraw.ini").wstring();
}

// MDI コードポイント対応表（リソースから一度だけ構築）
// "name codepoint(hex)" 形式の1行ごとのテキスト
static std::unordered_map<std::string, uint32_t> s_mdiCodepoints;

void BuildMdiCodepoints() {
    if (!s_mdiCodepoints.empty()) return;
    const auto bytes = ResourceBytes(IDR_MDI_CODEPOINTS);
    if (bytes.empty()) return;
    std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    // UTF-8 BOM をスキップ
    if (text.size() >= 3 && static_cast<uint8_t>(text[0]) == 0xEF && static_cast<uint8_t>(text[1]) == 0xBB && static_cast<uint8_t>(text[2]) == 0xBF)
        text = text.substr(3);
    size_t pos = 0;
    while (pos < text.size()) {
        const size_t end = text.find('\n', pos);
        const size_t lineEnd = (end == std::string_view::npos) ? text.size() : end;
        std::string_view line = text.substr(pos, lineEnd - pos);
        if (!line.empty() && line.back() == '\r') line = line.substr(0, line.size() - 1);
        const size_t sp = line.find(' ');
        if (sp != std::string_view::npos) {
            const std::string name(line.substr(0, sp));
            const std::string hex(line.substr(sp + 1));
            const uint32_t cp = static_cast<uint32_t>(std::strtoul(hex.c_str(), nullptr, 16));
            if (cp != 0) s_mdiCodepoints[name] = cp;
        }
        pos = (end == std::string_view::npos) ? text.size() : end + 1;
    }
}

// アイコン名（例 "hand-pointing-left"）からサロゲートペアを含むグリフ文字列に変換
// BMP外（U+F0000～）は UTF-16 サロゲートペアになる
std::wstring MdiNameToGlyph(const std::string& name) {
    BuildMdiCodepoints();
    const auto it = s_mdiCodepoints.find(name);
    if (it == s_mdiCodepoints.end()) return L"\U000F02FD"; // 不明時は information-outline
    const uint32_t cp = it->second;
    if (cp <= 0xFFFF) {
        return std::wstring(1, static_cast<wchar_t>(cp));
    }
    // サロゲートペア変換
    const uint32_t c = cp - 0x10000;
    const wchar_t high = static_cast<wchar_t>(0xD800 | (c >> 10));
    const wchar_t low  = static_cast<wchar_t>(0xDC00 | (c & 0x3FF));
    return std::wstring{high, low};
}

void LoadSettings() {
    wchar_t buffer[32]{};
    GetPrivateProfileStringW(L"Colors", L"Color1", L"E53935FF", buffer, 32, SettingsPath().c_str());
    g.color1 = static_cast<uint32_t>(std::wcstoul(buffer, nullptr, 16));
    if (g.color1 <= 0xFFFFFF) g.color1 = (g.color1 << 8) | 0xFFU;
    GetPrivateProfileStringW(L"Colors", L"Color2", L"FFFFFFFF", buffer, 32, SettingsPath().c_str());
    g.color2 = static_cast<uint32_t>(std::wcstoul(buffer, nullptr, 16));
    if (g.color2 <= 0xFFFFFF) g.color2 = (g.color2 << 8) | 0xFFU;

    // [Stamps] セクションからスタンプアイコン名を読み込み
    // stamp01～stamp15 が指定されていれば g_stampEntries を更新する
    BuildMdiCodepoints();
    wchar_t nameBuf[128]{};
    for (int i = 0; i < kStampCount; ++i) {
        wchar_t key[16]{};
        swprintf_s(key, L"stamp%02d", i + 1);
        nameBuf[0] = L'\0';
        GetPrivateProfileStringW(L"Stamps", key, L"", nameBuf, 128, SettingsPath().c_str());
        if (nameBuf[0] != L'\0') {
            // wchar_t → UTF-8 に変換してマップ検索
            const int len = WideCharToMultiByte(CP_UTF8, 0, nameBuf, -1, nullptr, 0, nullptr, nullptr);
            std::string utf8(static_cast<size_t>(len - 1), '\0');
            WideCharToMultiByte(CP_UTF8, 0, nameBuf, -1, utf8.data(), len, nullptr, nullptr);
            g_stampEntries[i].label  = std::wstring(nameBuf);
            g_stampEntries[i].glyph  = MdiNameToGlyph(utf8);
        }
    }

    g.exportLightweight = GetPrivateProfileIntW(L"Export", L"Lightweight", 0, SettingsPath().c_str()) != 0;
}

void SaveSettings() {
    wchar_t buffer[16]{};
    swprintf_s(buffer, L"%08X", g.color1);
    WritePrivateProfileStringW(L"Colors", L"Color1", buffer, SettingsPath().c_str());
    swprintf_s(buffer, L"%08X", g.color2);
    WritePrivateProfileStringW(L"Colors", L"Color2", buffer, SettingsPath().c_str());
    WritePrivateProfileStringW(L"Export", L"Lightweight", g.exportLightweight ? L"1" : L"0", SettingsPath().c_str());
}

void WriteAppearance(Writer& w, const Object& object) {
    w.pod(object.fillColor);
    w.pod<uint8_t>(object.hasOutline ? 1 : 0);
    w.pod(object.outlineWidth);
    w.pod(object.outlineOpacity);
    w.pod<uint8_t>(object.fill ? 1 : 0);
    w.pod(object.dashStyle);
}

void ReadAppearance(Reader& r, Object& object) {
    if (!r.ok || r.pos >= r.data.size()) return;
    object.fillColor = r.pod<uint32_t>();
    object.hasOutline = r.pod<uint8_t>() != 0;
    object.outlineWidth = r.pod<float>();
    object.outlineOpacity = r.pod<float>();
    object.fill = r.pod<uint8_t>() != 0;
    if (r.ok && r.pos < r.data.size()) object.dashStyle = r.pod<uint8_t>();
}

// --- 画像エンコード / デコード (WIC) ---

HRESULT EncodePixels(const std::vector<uint8_t>& pixels, UINT width, UINT height, REFGUID container, std::vector<uint8_t>& output,
                     std::optional<float> jpegQuality = std::nullopt) {
    output.clear();
    ComPtr<IStream> stream;
    HRESULT hr = CreateStreamOnHGlobal(nullptr, TRUE, &stream);
    ComPtr<IWICBitmapEncoder> encoder;
    if (SUCCEEDED(hr)) hr = g.wic->CreateEncoder(container, nullptr, &encoder);
    if (SUCCEEDED(hr)) hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> props;
    if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, &props);
    if (SUCCEEDED(hr) && jpegQuality && IsEqualGUID(container, GUID_ContainerFormatJpeg) && props) {
        PROPBAG2 option{};
        option.pstrName = const_cast<wchar_t*>(L"ImageQuality");
        option.vt = VT_R4;
        VARIANT var{};
        var.vt = VT_R4;
        var.fltVal = *jpegQuality;
        props->Write(1, &option, &var);
    }
    if (SUCCEEDED(hr)) hr = frame->Initialize(props.Get());
    if (SUCCEEDED(hr)) hr = frame->SetSize(width, height);
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (IsEqualGUID(container, GUID_ContainerFormatJpeg)) format = GUID_WICPixelFormat24bppBGR;
    else if (IsEqualGUID(container, GUID_ContainerFormatBmp)) format = GUID_WICPixelFormat32bppBGR;
    if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&format);
    ComPtr<IWICBitmap> source;
    if (SUCCEEDED(hr)) hr = CreateWicBitmapFromPixels(pixels, width, height, &source);
    if (SUCCEEDED(hr)) hr = frame->WriteSource(source.Get(), nullptr);
    if (SUCCEEDED(hr)) hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = encoder->Commit();
    if (FAILED(hr)) return hr;
    HGLOBAL memory{};
    hr = GetHGlobalFromStream(stream.Get(), &memory);
    if (FAILED(hr)) return hr;
    const SIZE_T size = GlobalSize(memory);
    const auto* ptr = static_cast<const uint8_t*>(GlobalLock(memory));
    if (!ptr) return E_OUTOFMEMORY;
    output.assign(ptr, ptr + size);
    GlobalUnlock(memory);
    return S_OK;
}

HRESULT DecodeSource(IWICBitmapSource* source, std::vector<uint8_t>& pixels, UINT& width, UINT& height) {
    ComPtr<IWICFormatConverter> converter;
    HRESULT hr = g.wic->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr)) hr = converter->Initialize(source, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (SUCCEEDED(hr)) hr = converter->GetSize(&width, &height);
    if (FAILED(hr) || width == 0 || height == 0 || width > kMaxImageEdge || height > kMaxImageEdge ||
        static_cast<uint64_t>(width) * height > kMaxImagePixels) return FAILED(hr) ? hr : HRESULT_FROM_WIN32(ERROR_UNSUPPORTED_TYPE);
    const uint64_t stride64 = static_cast<uint64_t>(width) * 4U;
    if (stride64 > UINT_MAX) return HRESULT_FROM_WIN32(ERROR_UNSUPPORTED_TYPE);
    pixels.resize(static_cast<size_t>(width) * height * 4U);
    if (pixels.size() <= UINT_MAX) {
        return converter->CopyPixels(nullptr, static_cast<UINT>(stride64), static_cast<UINT>(pixels.size()), pixels.data());
    }
    ComPtr<IWICBitmap> bitmap;
    hr = g.wic->CreateBitmapFromSource(converter.Get(), WICBitmapCacheOnLoad, &bitmap);
    if (FAILED(hr)) return hr;
    WICRect lockRect{0, 0, static_cast<INT>(width), static_cast<INT>(height)};
    ComPtr<IWICBitmapLock> lock;
    hr = bitmap->Lock(&lockRect, WICBitmapLockRead, &lock);
    UINT stride = 0, bufferSize = 0; BYTE* data = nullptr;
    if (SUCCEEDED(hr)) hr = lock->GetStride(&stride);
    if (SUCCEEDED(hr)) hr = lock->GetDataPointer(&bufferSize, &data);
    if (FAILED(hr) || !data) return FAILED(hr) ? hr : E_FAIL;
    for (UINT y = 0; y < height; ++y) {
        std::memcpy(pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(width) * 4U,
                    data + static_cast<size_t>(y) * stride, static_cast<size_t>(width) * 4U);
    }
    return S_OK;
}

HRESULT DecodeMemory(std::span<const uint8_t> bytes, std::vector<uint8_t>& pixels, UINT& width, UINT& height) {
    ComPtr<IWICStream> stream;
    HRESULT hr = g.wic->CreateStream(&stream);
    if (SUCCEEDED(hr)) hr = stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size()));
    ComPtr<IWICBitmapDecoder> decoder;
    if (SUCCEEDED(hr)) hr = g.wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder);
    ComPtr<IWICBitmapFrameDecode> frame;
    if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);
    return SUCCEEDED(hr) ? DecodeSource(frame.Get(), pixels, width, height) : hr;
}

void PumpPendingMessages() {
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

static void CopyPixelRgba(const uint8_t* src, uint8_t* dst) {
    dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = src[3];
}

// EXIF orientation をピクセルバッファに適用（WIC FlipRotator は縮小後でもJPEG全解像度デコードを誘発するため使わない）
void ApplyExifOrientation(std::vector<uint8_t>& pixels, UINT& width, UINT& height, USHORT orientation) {
    if (orientation <= 1 || pixels.empty() || width == 0 || height == 0) return;
    const UINT w = width, h = height;
    const auto srcAt = [&](UINT x, UINT y) { return pixels.data() + (static_cast<size_t>(y) * w + x) * 4U; };

    if (orientation == 2) {
        for (UINT y = 0; y < h; ++y)
            for (UINT x = 0; x < w / 2; ++x) {
                auto* a = srcAt(x, y);
                auto* b = srcAt(w - 1 - x, y);
                for (int c = 0; c < 4; ++c) std::swap(a[c], b[c]);
            }
        return;
    }
    if (orientation == 3) {
        std::vector<uint8_t> tmp(pixels.size());
        for (UINT y = 0; y < h; ++y)
            for (UINT x = 0; x < w; ++x)
                CopyPixelRgba(srcAt(x, y), tmp.data() + (static_cast<size_t>(h - 1 - y) * w + (w - 1 - x)) * 4U);
        pixels = std::move(tmp);
        return;
    }
    if (orientation == 4) {
        for (UINT y = 0; y < h / 2; ++y)
            for (UINT x = 0; x < w; ++x) {
                auto* a = srcAt(x, y);
                auto* b = srcAt(x, h - 1 - y);
                for (int c = 0; c < 4; ++c) std::swap(a[c], b[c]);
            }
        return;
    }

    std::vector<uint8_t> tmp;
    UINT newW = w, newH = h;
    if (orientation == 5 || orientation == 6 || orientation == 7 || orientation == 8) {
        newW = h; newH = w;
        tmp.resize(static_cast<size_t>(newW) * newH * 4U);
        const auto dstAt = [&](UINT x, UINT y) { return tmp.data() + (static_cast<size_t>(y) * newW + x) * 4U; };
        for (UINT y = 0; y < h; ++y) {
            for (UINT x = 0; x < w; ++x) {
                UINT dx = x, dy = y;
                switch (orientation) {
                case 5: dx = y; dy = x; break;                         // transpose
                case 6: dx = h - 1 - y; dy = x; break;                 // 90° CW
                case 7: dx = h - 1 - y; dy = w - 1 - x; break;         // transpose + flip
                case 8: dx = y; dy = w - 1 - x; break;                 // 270° CW
                default: break;
                }
                CopyPixelRgba(srcAt(x, y), dstAt(dx, dy));
            }
        }
        pixels = std::move(tmp);
        width = newW;
        height = newH;
    }
}

void InvalidateDocBitmap() {
    g.docBitmap.Reset();
    g.docBitmapWidth = 0;
    g.docBitmapHeight = 0;
}

void EnsureDocBitmap(ID2D1RenderTarget* target) {
    if (!target || g.doc.pixels.empty()) {
        InvalidateDocBitmap();
        return;
    }
    if (g.docBitmap && g.docBitmapWidth == g.doc.width && g.docBitmapHeight == g.doc.height) return;
    InvalidateDocBitmap();
    const D2D1_BITMAP_PROPERTIES props{
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f};
    ComPtr<ID2D1Bitmap> bitmap;
    if (SUCCEEDED(target->CreateBitmap(D2D1::SizeU(g.doc.width, g.doc.height), g.doc.pixels.data(), g.doc.width * 4U, props, &bitmap))) {
        g.docBitmap = bitmap;
        g.docBitmapWidth = g.doc.width;
        g.docBitmapHeight = g.doc.height;
    }
}

bool DownscaleIfNeeded(std::vector<uint8_t>& pixels, UINT& width, UINT& height) {
    if (width <= kMaxWorkingEdge && height <= kMaxWorkingEdge) return false;
    const float scale = static_cast<float>(kMaxWorkingEdge) / static_cast<float>((std::max)(width, height));
    const UINT newWidth = std::max(1U, static_cast<UINT>(std::lround(static_cast<double>(width) * scale)));
    const UINT newHeight = std::max(1U, static_cast<UINT>(std::lround(static_cast<double>(height) * scale)));
    ComPtr<IWICBitmap> source;
    if (FAILED(CreateWicBitmapFromPixels(pixels, width, height, &source))) return false;
    ComPtr<IWICBitmapScaler> scaler;
    if (FAILED(g.wic->CreateBitmapScaler(&scaler))) return false;
    HRESULT hr = scaler->Initialize(source.Get(), newWidth, newHeight, WICBitmapInterpolationModeFant);
    if (FAILED(hr)) {
        hr = scaler->Initialize(source.Get(), newWidth, newHeight, WICBitmapInterpolationModeLinear);
        if (FAILED(hr)) return false;
    }
    std::vector<uint8_t> scaled;
    UINT scaledWidth{}, scaledHeight{};
    if (FAILED(DecodeSource(scaler.Get(), scaled, scaledWidth, scaledHeight))) return false;
    pixels = std::move(scaled);
    width = scaledWidth;
    height = scaledHeight;
    return true;
}

HRESULT DecodeSourceForEditing(IWICBitmapSource* source, std::vector<uint8_t>& pixels, UINT& width, UINT& height, bool* downscaled) {
    if (downscaled) *downscaled = false;
    UINT srcWidth{}, srcHeight{};
    HRESULT hr = source->GetSize(&srcWidth, &srcHeight);
    if (FAILED(hr) || srcWidth == 0 || srcHeight == 0 || srcWidth > kMaxImageEdge || srcHeight > kMaxImageEdge ||
        static_cast<uint64_t>(srcWidth) * srcHeight > kMaxImagePixels) {
        return FAILED(hr) ? hr : HRESULT_FROM_WIN32(ERROR_UNSUPPORTED_TYPE);
    }
    IWICBitmapSource* decodeSource = source;
    ComPtr<IWICBitmapScaler> scaler;
    if (srcWidth > kMaxWorkingEdge || srcHeight > kMaxWorkingEdge) {
        const float scale = static_cast<float>(kMaxWorkingEdge) / static_cast<float>((std::max)(srcWidth, srcHeight));
        const UINT newWidth = std::max(1U, static_cast<UINT>(std::lround(static_cast<double>(srcWidth) * scale)));
        const UINT newHeight = std::max(1U, static_cast<UINT>(std::lround(static_cast<double>(srcHeight) * scale)));
        hr = g.wic->CreateBitmapScaler(&scaler);
        if (FAILED(hr)) return hr;
        hr = scaler->Initialize(source, newWidth, newHeight, WICBitmapInterpolationModeFant);
        if (FAILED(hr)) hr = scaler->Initialize(source, newWidth, newHeight, WICBitmapInterpolationModeLinear);
        if (FAILED(hr)) return hr;
        decodeSource = scaler.Get();
        if (downscaled) *downscaled = true;
    }
    return DecodeSource(decodeSource, pixels, width, height);
}

bool IsTwoPointObject(const Object& object) {
    return (object.type == ObjectType::Arrow || object.type == ObjectType::Line) && object.points.size() >= 2;
}

ID2D1StrokeStyle* StrokeStyleFor(const Object& object) {
    switch (object.dashStyle) {
    case 1: return g.dashStroke.Get();
    case 2: return g.dashDotStroke.Get();
    case 3: return g.dashDotDotStroke.Get();
    default: return g.roundStroke.Get();
    }
}

void InvalidateCanvas() { if (g.canvas) InvalidateRect(g.canvas, nullptr, FALSE); }
void FitOrigin();
void ApplySelectionToProperties();
void Layout(HWND hwnd);
void ApplyUiFont(HWND hwnd);
void SetTool(Tool tool);

void ResetHistory();
void UpdateWindowTitle();

bool EnsureDocPng() {
    if (!g.doc.png.empty() || g.doc.pixels.empty()) return !g.doc.png.empty();
    return SUCCEEDED(EncodePixels(g.doc.pixels, g.doc.width, g.doc.height, GUID_ContainerFormatPng, g.doc.png));
}

HRESULT LoadImageFile(const std::wstring& path) {
    PumpPendingMessages();
    HCURSOR waitCursor = LoadCursorW(nullptr, IDC_WAIT);
    HCURSOR prevCursor = SetCursor(waitCursor);
    if (g.mainWindow) SetWindowTextW(g.mainWindow, L"LiteDraw - 読み込み中...");
    PumpPendingMessages();

    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
    if (bytes.empty() || !LooksLikeSupportedImage(bytes)) {
        SetCursor(prevCursor);
        UpdateWindowTitle();
        return HRESULT_FROM_WIN32(ERROR_UNSUPPORTED_TYPE);
    }
    ComPtr<IWICStream> stream;
    HRESULT hr = g.wic->CreateStream(&stream);
    if (SUCCEEDED(hr)) {
        if (bytes.size() > MAXDWORD) {
            SetCursor(prevCursor);
            UpdateWindowTitle();
            return HRESULT_FROM_WIN32(ERROR_UNSUPPORTED_TYPE);
        }
        hr = stream->InitializeFromMemory(bytes.data(), static_cast<DWORD>(bytes.size()));
    }
    ComPtr<IWICBitmapDecoder> decoder;
    if (SUCCEEDED(hr)) hr = g.wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder);
    ComPtr<IWICBitmapFrameDecode> frame;
    if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);

    bool downscaled = false;
    if (SUCCEEDED(hr)) {
        USHORT orientation = 1;
        ComPtr<IWICMetadataQueryReader> metadata;
        PROPVARIANT value{};
        PropVariantInit(&value);
        if (SUCCEEDED(frame->GetMetadataQueryReader(&metadata)) &&
            SUCCEEDED(metadata->GetMetadataByName(L"/app1/ifd/{ushort=274}", &value)) &&
            value.vt == VT_UI2) orientation = value.uiVal;
        PropVariantClear(&value);

        UINT frameW{}, frameH{};
        hr = frame->GetSize(&frameW, &frameH);
        UINT displayW = frameW, displayH = frameH;
        if (orientation >= 5 && orientation <= 8) std::swap(displayW, displayH);

        // JPEG は回転前に縮小しないとフル解像度デコードが走る（縦長写真で顕著）
        IWICBitmapSource* pipeline = frame.Get();
        ComPtr<IWICBitmapScaler> scaler;
        if (SUCCEEDED(hr) && (displayW > kMaxWorkingEdge || displayH > kMaxWorkingEdge)) {
            const float scale = static_cast<float>(kMaxWorkingEdge) / static_cast<float>((std::max)(displayW, displayH));
            const UINT scaledW = std::max(1U, static_cast<UINT>(std::lround(static_cast<double>(frameW) * scale)));
            const UINT scaledH = std::max(1U, static_cast<UINT>(std::lround(static_cast<double>(frameH) * scale)));
            hr = g.wic->CreateBitmapScaler(&scaler);
            if (SUCCEEDED(hr)) hr = scaler->Initialize(frame.Get(), scaledW, scaledH, WICBitmapInterpolationModeLinear);
            if (SUCCEEDED(hr)) {
                pipeline = scaler.Get();
                downscaled = true;
            }
        }

        PumpPendingMessages();

        if (SUCCEEDED(hr)) hr = DecodeSource(pipeline, g.doc.pixels, g.doc.width, g.doc.height);
        if (SUCCEEDED(hr) && orientation > 1) {
            PumpPendingMessages();
            ApplyExifOrientation(g.doc.pixels, g.doc.width, g.doc.height, orientation);
        }
    }

    if (SUCCEEDED(hr)) {
        if (downscaled) g.doc.png.clear(); // 保存時に遅延エンコード
        else g.doc.png = std::move(bytes);
    }
    SetCursor(prevCursor);
    PumpPendingMessages();
    if (SUCCEEDED(hr)) {
        g.doc.originalName = std::filesystem::path(path).filename().wstring();
        g.doc.sourcePath = path;
        g.doc.projectPath.clear();
        GetSystemTimeAsFileTime(&g.doc.created);
        g.doc.modified = g.doc.created;
        g.doc.objects.clear();
        g.doc.nextId = 1;
        g.doc.dirty = false;
        g.selected = -1;
        g.cropActive = false;
        g.zoom = 1.0f;
        InvalidateDocBitmap();
        PumpPendingMessages();
        ResetHistory();
        UpdateWindowTitle();
        if (downscaled && g.mainWindow) {
            wchar_t message[160]{};
            swprintf_s(message, L"画像が大きいため、長辺 %u px に縮小して読み込みました。", kMaxWorkingEdge);
            MessageBoxW(g.mainWindow, message, L"LiteDraw", MB_OK | MB_ICONINFORMATION);
        }
        return S_OK;
    }
    UpdateWindowTitle();
    return FAILED(hr) ? hr : HRESULT_FROM_WIN32(ERROR_UNSUPPORTED_TYPE);
}

std::vector<uint8_t> SerializePayload(const Object& object) {
    Writer w;
    auto rect = [&] {
        w.pod(object.rect.left); w.pod(object.rect.top);
        w.pod(object.rect.right - object.rect.left); w.pod(object.rect.bottom - object.rect.top);
    };
    switch (object.type) {
    case ObjectType::Arrow:
        w.pod(object.points[0].x); w.pod(object.points[0].y);
        w.pod(object.points[1].x); w.pod(object.points[1].y);
        w.pod(object.width); w.pod(object.color); w.pod(object.extra);
        WriteAppearance(w, object);
        break;
    case ObjectType::Line:
        w.pod(object.points[0].x); w.pod(object.points[0].y);
        w.pod(object.points[1].x); w.pod(object.points[1].y);
        w.pod(object.width); w.pod(object.color);
        WriteAppearance(w, object);
        break;
    case ObjectType::Rectangle:
    case ObjectType::Ellipse:
        rect(); w.pod(object.width); w.pod(object.color); WriteAppearance(w, object); break;
    case ObjectType::Text: {
        w.pod(object.rect.left); w.pod(object.rect.top);
        const auto text = ToUtf8(object.text);
        const auto font = ToUtf8(object.font);
        w.pod<uint32_t>(static_cast<uint32_t>(text.size())); w.bytes(std::string_view(text));
        w.pod<uint16_t>(static_cast<uint16_t>(std::min<size_t>(font.size(), 65535U)));
        w.bytes(std::string_view(font).substr(0, std::min<size_t>(font.size(), 65535U)));
        w.pod(object.extra); w.pod(object.color);
        WriteAppearance(w, object);
        break;
    }
    case ObjectType::Freehand:
        w.pod<uint32_t>(static_cast<uint32_t>(object.points.size()));
        for (auto p : object.points) { w.pod(p.x); w.pod(p.y); }
        w.pod(object.width); w.pod(object.color); WriteAppearance(w, object);
        break;
    case ObjectType::BlurRegion:
        rect(); w.pod(object.extra); w.pod(object.color); break;
    case ObjectType::MosaicRegion:
        rect(); w.pod(object.blockSize); w.pod(object.color); break;
    case ObjectType::NumberStamp: {
        const float cx = (object.rect.left + object.rect.right) * 0.5f;
        const float cy = (object.rect.top + object.rect.bottom) * 0.5f;
        const auto text = ToUtf8(object.text);
        const auto len = static_cast<uint8_t>(std::min<size_t>(text.size(), 255U));
        w.pod(cx); w.pod(cy); w.pod(len); w.bytes(std::string_view(text).substr(0, len));
        w.pod(object.extra); w.pod(object.color); WriteAppearance(w, object);
        break;
    }
    case ObjectType::IconStamp: {
        const float cx = (object.rect.left + object.rect.right) * 0.5f;
        const float cy = (object.rect.top + object.rect.bottom) * 0.5f;
        w.pod(cx); w.pod(cy); w.pod(object.extra); w.pod(object.blockSize); w.pod(object.color);
        break;
    }
    case ObjectType::Loupe:
        rect(); w.pod(object.extra); w.pod<uint8_t>(object.fill ? 1 : 0); w.pod(object.color);
        if (object.points.empty()) { w.pod(0.0f); w.pod(0.0f); }
        else { w.pod(object.points[0].x); w.pod(object.points[0].y); }
        w.pod(object.width);
        break;
    case ObjectType::Callout: {
        rect();
        const auto tip = object.points.empty() ? D2D1::Point2F(object.rect.left, object.rect.bottom + 24) : object.points[0];
        w.pod(tip.x); w.pod(tip.y);
        const auto text = ToUtf8(object.text);
        const auto font = ToUtf8(object.font);
        w.pod<uint32_t>(static_cast<uint32_t>(text.size())); w.bytes(std::string_view(text));
        w.pod<uint16_t>(static_cast<uint16_t>(std::min<size_t>(font.size(), 65535U)));
        w.bytes(std::string_view(font).substr(0, std::min<size_t>(font.size(), 65535U)));
        w.pod(object.extra); w.pod(object.width); w.pod(object.color);
        WriteAppearance(w, object);
        break;
    }
    }
    return w.data;
}

bool DeserializePayload(Object& object, std::span<const uint8_t> payload) {
    Reader r{payload};
    auto rect = [&] {
        const float x = r.pod<float>(), y = r.pod<float>(), w = r.pod<float>(), h = r.pod<float>();
        object.rect = D2D1::RectF(x, y, x + w, y + h);
    };
    switch (object.type) {
    case ObjectType::Arrow:
        object.points.resize(2);
        {
            const float x0 = r.pod<float>(), y0 = r.pod<float>();
            const float x1 = r.pod<float>(), y1 = r.pod<float>();
            object.points[0] = D2D1::Point2F(x0, y0);
            object.points[1] = D2D1::Point2F(x1, y1);
        }
        object.width = r.pod<float>(); object.color = r.pod<uint32_t>(); object.extra = r.pod<float>();
        ReadAppearance(r, object);
        break;
    case ObjectType::Line:
        object.points.resize(2);
        {
            const float x0 = r.pod<float>(), y0 = r.pod<float>();
            const float x1 = r.pod<float>(), y1 = r.pod<float>();
            object.points[0] = D2D1::Point2F(x0, y0);
            object.points[1] = D2D1::Point2F(x1, y1);
        }
        object.width = r.pod<float>(); object.color = r.pod<uint32_t>();
        ReadAppearance(r, object);
        break;
    case ObjectType::Rectangle:
    case ObjectType::Ellipse:
        rect(); object.width = r.pod<float>(); object.color = r.pod<uint32_t>(); ReadAppearance(r, object); break;
    case ObjectType::Text: {
        object.rect.left = r.pod<float>(); object.rect.top = r.pod<float>();
        const auto textLen = r.pod<uint32_t>();
        object.text = FromUtf8(r.bytes(textLen));
        const auto fontLen = r.pod<uint16_t>();
        object.font = FromUtf8(r.bytes(fontLen));
        object.extra = r.pod<float>(); object.color = r.pod<uint32_t>();
        ReadAppearance(r, object);
        object.rect.right = object.rect.left + std::max(120.0f, object.extra * static_cast<float>(std::max<size_t>(object.text.size(), 6)));
        object.rect.bottom = object.rect.top + object.extra * 1.5f;
        break;
    }
    case ObjectType::Freehand: {
        const auto count = r.pod<uint32_t>();
        if (count > 1000000U) return false;
        object.points.resize(count);
        for (auto& p : object.points) {
            const float x = r.pod<float>(), y = r.pod<float>();
            p = D2D1::Point2F(x, y);
        }
        object.width = r.pod<float>(); object.color = r.pod<uint32_t>();
        ReadAppearance(r, object);
        break;
    }
    case ObjectType::BlurRegion:
        rect(); object.extra = r.pod<float>(); object.color = r.pod<uint32_t>(); break;
    case ObjectType::MosaicRegion:
        rect(); object.blockSize = r.pod<uint32_t>(); object.color = r.pod<uint32_t>(); break;
    case ObjectType::NumberStamp: {
        const float cx = r.pod<float>(), cy = r.pod<float>();
        const auto len = r.pod<uint8_t>();
        object.text = FromUtf8(r.bytes(len));
        object.extra = r.pod<float>(); object.color = r.pod<uint32_t>();
        ReadAppearance(r, object);
        const float radius = object.extra * 0.5f;
        object.rect = D2D1::RectF(cx - radius, cy - radius, cx + radius, cy + radius);
        break;
    }
    case ObjectType::IconStamp: {
        const float cx = r.pod<float>(), cy = r.pod<float>();
        object.extra = r.pod<float>();
        object.blockSize = r.pod<uint32_t>();
        // color は新フォーマットのみ存在（旧ファイルとの互換）
        if (r.ok && r.pos + sizeof(uint32_t) <= r.data.size()) object.color = r.pod<uint32_t>();
        const float radius = object.extra * 0.5f;
        object.rect = D2D1::RectF(cx - radius, cy - radius, cx + radius, cy + radius);
        break;
    }
    case ObjectType::Loupe: {
        rect(); object.extra = r.pod<float>(); object.fill = r.pod<uint8_t>() != 0; object.color = r.pod<uint32_t>();
        const float px = r.pod<float>(), py = r.pod<float>();
        object.points = {D2D1::Point2F(px, py)};
        if (r.ok && r.pos + sizeof(float) <= r.data.size()) object.width = r.pod<float>();
        break;
    }
    case ObjectType::Callout: {
        rect();
        const float tipX = r.pod<float>(), tipY = r.pod<float>();
        object.points = {D2D1::Point2F(tipX, tipY)};
        const auto textLen = r.pod<uint32_t>();
        object.text = FromUtf8(r.bytes(textLen));
        const auto fontLen = r.pod<uint16_t>();
        object.font = FromUtf8(r.bytes(fontLen));
        object.extra = r.pod<float>(); object.width = r.pod<float>(); object.color = r.pod<uint32_t>();
        ReadAppearance(r, object);
        object.hasOutline = false;
        break;
    }
    default:
        return false;
    }
    return r.ok;
}

std::vector<uint8_t> SerializeObjects() {
    Writer w;
    w.pod<uint32_t>(static_cast<uint32_t>(g.doc.objects.size()));
    for (const auto& object : g.doc.objects) {
        const auto payload = SerializePayload(object);
        w.pod(object.id); w.pod(static_cast<uint8_t>(object.type)); w.pod(object.z);
        w.pod<uint8_t>(object.visible ? 1 : 0); w.pod<uint32_t>(static_cast<uint32_t>(payload.size())); w.bytes(payload);
    }
    return w.data;
}

bool DeserializeObjects(std::span<const uint8_t> bytes) {
    Reader r{bytes};
    const auto count = r.pod<uint32_t>();
    if (count > 100000U) return false;
    std::vector<Object> objects;
    uint32_t nextId = 1;
    for (uint32_t i = 0; i < count; ++i) {
        Object object;
        object.id = r.pod<uint32_t>();
        const auto type = r.pod<uint8_t>();
        object.z = r.pod<int32_t>();
        object.visible = r.pod<uint8_t>() != 0;
        const auto length = r.pod<uint32_t>();
        if (!r.ok || type < 1 || type > 12) return false;
        object.type = static_cast<ObjectType>(type);
        if (!DeserializePayload(object, r.bytes(length))) return false;
        nextId = std::max(nextId, object.id + 1);
        objects.push_back(std::move(object));
    }
    g.doc.objects = std::move(objects);
    g.doc.nextId = nextId;
    g.selected = -1;
    return r.ok;
}

void ResetHistory() {
    g.history.clear();
    HistoryEntry entry;
    entry.width = g.doc.width;
    entry.height = g.doc.height;
    entry.pixels = g.doc.pixels;
    entry.png = g.doc.png;
    entry.objects = SerializeObjects();
    entry.nextId = g.doc.nextId;
    g.history.push_back(std::move(entry));
    g.historyPos = 0;
}

void PushHistory() {
    if (g.updatingUi) return;
    if (g.historyPos + 1 < g.history.size()) g.history.resize(g.historyPos + 1);
    HistoryEntry entry;
    entry.width = g.doc.width;
    entry.height = g.doc.height;
    entry.pixels = g.doc.pixels;
    entry.png = g.doc.png;
    entry.objects = SerializeObjects();
    entry.nextId = g.doc.nextId;
    g.history.push_back(std::move(entry));
    if (g.history.size() > 40) {
        g.history.erase(g.history.begin());
    }
    g.historyPos = g.history.size() - 1;
    g.doc.dirty = true;
}

void RestoreHistoryEntry(const HistoryEntry& entry) {
    const bool sizeChanged = g.doc.width != entry.width || g.doc.height != entry.height;
    const auto previousObjects = g.doc.objects;
    const auto previousNextId = g.doc.nextId;
    g.doc.width = entry.width;
    g.doc.height = entry.height;
    g.doc.pixels = entry.pixels;
    g.doc.png = entry.png;
    if (DeserializeObjects(entry.objects)) {
        g.doc.nextId = entry.nextId;
    } else {
        g.doc.objects = previousObjects;
        g.doc.nextId = previousNextId;
    }
    g.selected = -1;
    g.draft.reset();
    g.textEditing = false;
    g.cropActive = false;
    InvalidateDocBitmap();
    if (sizeChanged) FitOrigin();
}

void Undo(bool redo) {
    if (redo) {
        if (g.historyPos + 1 >= g.history.size()) return;
        ++g.historyPos;
    } else {
        if (g.historyPos == 0) return;
        --g.historyPos;
    }
    g.updatingUi = true;
    RestoreHistoryEntry(g.history[g.historyPos]);
    g.updatingUi = false;
    g.doc.dirty = true;
    ApplySelectionToProperties();
    InvalidateCanvas();
}

// --- .ldl プロジェクト保存 / 読み込み ---

bool SaveLdl(const std::wstring& path) {
    if (g.doc.pixels.empty()) return false;
    if (!EnsureDocPng()) return false;
    Writer w;
    w.bytes(std::span(reinterpret_cast<const uint8_t*>("LDL\0"), 4));
    w.pod<uint32_t>(2); w.pod<uint32_t>(12);
    w.pod<uint32_t>(g.doc.width); w.pod<uint32_t>(g.doc.height);
    ULARGE_INTEGER created{}, modified{};
    created.LowPart = g.doc.created.dwLowDateTime; created.HighPart = g.doc.created.dwHighDateTime;
    GetSystemTimeAsFileTime(&g.doc.modified);
    modified.LowPart = g.doc.modified.dwLowDateTime; modified.HighPart = g.doc.modified.dwHighDateTime;
    w.pod(created.QuadPart); w.pod(modified.QuadPart);
    const auto name = ToUtf8(g.doc.originalName);
    w.pod<uint16_t>(static_cast<uint16_t>(std::min<size_t>(name.size(), 65535U)));
    w.bytes(std::string_view(name).substr(0, std::min<size_t>(name.size(), 65535U)));
    w.pod<uint64_t>(g.doc.png.size()); w.bytes(g.doc.png);
    const auto objects = SerializeObjects(); w.bytes(objects);
    std::ofstream file(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(w.data.data()), static_cast<std::streamsize>(w.data.size()));
    if (!file) return false;
    g.doc.projectPath = path;
    g.doc.dirty = false;
    return true;
}

bool LoadLdl(const std::wstring& path) {
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
    Reader r{bytes};
    const auto magic = r.bytes(4);
    if (magic.size() != 4 || std::memcmp(magic.data(), "LDL\0", 4) != 0) return false;
    const auto version = r.pod<uint32_t>();
    if (version != 1 && version != 2) return false;
    const auto headerSize = r.pod<uint32_t>();
    if (headerSize > 12) r.bytes(headerSize - 12);
    const UINT expectedWidth = r.pod<uint32_t>(), expectedHeight = r.pod<uint32_t>();
    ULARGE_INTEGER created{}, modified{};
    created.QuadPart = r.pod<uint64_t>(); modified.QuadPart = r.pod<uint64_t>();
    const auto nameLength = r.pod<uint16_t>();
    const auto name = FromUtf8(r.bytes(nameLength));
    const auto imageLength = r.pod<uint64_t>();
    if (!r.ok || imageLength > r.data.size() - r.pos || imageLength > MAXDWORD) return false;
    const auto png = r.bytes(static_cast<size_t>(imageLength));
    std::vector<uint8_t> pixels;
    UINT width{}, height{};
    if (FAILED(DecodeMemory(png, pixels, width, height)) || width != expectedWidth || height != expectedHeight) return false;
    const bool downscaled = DownscaleIfNeeded(pixels, width, height);
    g.doc.width = width; g.doc.height = height; g.doc.pixels = std::move(pixels);
    if (downscaled) {
        if (FAILED(EncodePixels(g.doc.pixels, g.doc.width, g.doc.height, GUID_ContainerFormatPng, g.doc.png))) return false;
    } else {
        g.doc.png.assign(png.begin(), png.end());
    }
    g.doc.originalName = name;
    g.doc.sourcePath.clear();
    g.doc.created.dwLowDateTime = created.LowPart; g.doc.created.dwHighDateTime = created.HighPart;
    g.doc.modified.dwLowDateTime = modified.LowPart; g.doc.modified.dwHighDateTime = modified.HighPart;
    if (!DeserializeObjects(r.data.subspan(r.pos))) return false;
    g.doc.projectPath = path; g.doc.dirty = false; g.cropActive = false; g.zoom = 1.0f;
    InvalidateDocBitmap();
    ResetHistory();
    if (downscaled && g.mainWindow) {
        wchar_t message[160]{};
        swprintf_s(message, L"画像が大きいため、長辺 %u px に縮小して読み込みました。", kMaxWorkingEdge);
        MessageBoxW(g.mainWindow, message, L"LiteDraw", MB_OK | MB_ICONINFORMATION);
    }
    return true;
}

D2D1_RECT_F LoupePreviewRect(const Object& object) {
    const auto src = NormalizeRect(object.rect);
    const float zoom = std::clamp(object.extra, 1.5f, 3.0f);
    float width = std::max(8.0f, (src.right - src.left) * zoom);
    float height = std::max(8.0f, (src.bottom - src.top) * zoom);
    if (object.fill) {
        const float side = std::max(8.0f, std::min(src.right - src.left, src.bottom - src.top) * zoom);
        width = height = side;
    }
    const D2D1_POINT_2F origin = object.points.empty()
        ? D2D1::Point2F(src.right + 28.0f, src.top - 16.0f)
        : object.points[0];
    return D2D1::RectF(origin.x, origin.y, origin.x + width, origin.y + height);
}

// --- 描画・ヒットテスト ---

void DrawArrowLine(ID2D1RenderTarget* target, ID2D1Brush* brush, D2D1_POINT_2F from, D2D1_POINT_2F to, float width, float head, ID2D1StrokeStyle* style) {
    target->DrawLine(from, to, brush, width, style);
    const float angle = std::atan2(to.y - from.y, to.x - from.x);
    const D2D1_POINT_2F p1{to.x - head * std::cos(angle - kPi / 6.0f), to.y - head * std::sin(angle - kPi / 6.0f)};
    const D2D1_POINT_2F p2{to.x - head * std::cos(angle + kPi / 6.0f), to.y - head * std::sin(angle + kPi / 6.0f)};
    target->DrawLine(to, p1, brush, width, style);
    target->DrawLine(to, p2, brush, width, style);
}

void SquareLoupeRect(D2D1_RECT_F& rect) {
    auto next = NormalizeRect(rect);
    const float cx = (next.left + next.right) * 0.5f;
    const float cy = (next.top + next.bottom) * 0.5f;
    const float side = std::max(2.0f, std::min(next.right - next.left, next.bottom - next.top));
    rect = D2D1::RectF(cx - side * 0.5f, cy - side * 0.5f, cx + side * 0.5f, cy + side * 0.5f);
}

void PlaceLoupePreview(Object& object) {
    const auto src = NormalizeRect(object.rect);
    object.points = {D2D1::Point2F(src.right + 28.0f, src.top - 16.0f)};
}

D2D1_RECT_F Bounds(const Object& object) {
    if (object.type == ObjectType::Loupe) {
        const auto src = NormalizeRect(object.rect);
        const auto preview = LoupePreviewRect(object);
        return D2D1::RectF(
            std::min(src.left, preview.left), std::min(src.top, preview.top),
            std::max(src.right, preview.right), std::max(src.bottom, preview.bottom));
    }
    if (object.type == ObjectType::Callout) {
        auto box = NormalizeRect(object.rect);
        if (!object.points.empty()) {
            box.left = std::min(box.left, object.points[0].x);
            box.top = std::min(box.top, object.points[0].y);
            box.right = std::max(box.right, object.points[0].x);
            box.bottom = std::max(box.bottom, object.points[0].y);
        }
        return box;
    }
    if (!object.points.empty() && object.type != ObjectType::Loupe) {
        float left = object.points.front().x, right = left, top = object.points.front().y, bottom = top;
        for (auto p : object.points) {
            left = std::min(left, p.x); right = std::max(right, p.x);
            top = std::min(top, p.y); bottom = std::max(bottom, p.y);
        }
        return D2D1::RectF(left, top, right, bottom);
    }
    return NormalizeRect(object.rect);
}

bool HitTest(const Object& object, D2D1_POINT_2F p) {
    if (object.type == ObjectType::Loupe) {
        return PointInRect(object.rect, p) || PointInRect(LoupePreviewRect(object), p);
    }
    if (object.type == ObjectType::Callout) {
        if (PointInRect(object.rect, p)) return true;
        if (!object.points.empty() && std::hypot(p.x - object.points[0].x, p.y - object.points[0].y) <= 12.0f) return true;
        return false;
    }
    if (IsTwoPointObject(object)) return DistanceToSegment(p, object.points[0], object.points[1]) <= object.width + 6.0f;
    if (object.type == ObjectType::Freehand && object.points.size() >= 2) {
        for (size_t i = 1; i < object.points.size(); ++i) {
            if (DistanceToSegment(p, object.points[i - 1], object.points[i]) <= object.width + 5.0f) return true;
        }
        return false;
    }
    const auto r = Bounds(object);
    return p.x >= r.left - 5.0f && p.x <= r.right + 5.0f && p.y >= r.top - 5.0f && p.y <= r.bottom + 5.0f;
}

void MoveObject(Object& object, float dx, float dy) {
    if (object.type == ObjectType::Loupe && g.movingLoupePreview) {
        if (!object.points.empty()) { object.points[0].x += dx; object.points[0].y += dy; }
        return;
    }
    object.rect.left += dx; object.rect.right += dx; object.rect.top += dy; object.rect.bottom += dy;
    if (object.type != ObjectType::Loupe) {
        for (auto& p : object.points) { p.x += dx; p.y += dy; }
    }
}

int HitHandle(const Object& object, D2D1_POINT_2F p) {
    const float radius = 8.0f / g.zoom;
    if (object.type == ObjectType::Loupe) {
        const auto src = NormalizeRect(object.rect);
        const auto preview = LoupePreviewRect(object);
        const std::array<D2D1_POINT_2F, 4> sourceHandles{
            D2D1::Point2F(src.left, src.top), D2D1::Point2F(src.right, src.top),
            D2D1::Point2F(src.left, src.bottom), D2D1::Point2F(src.right, src.bottom)
        };
        for (size_t i = 0; i < sourceHandles.size(); ++i) {
            if (std::abs(sourceHandles[i].x - p.x) <= radius && std::abs(sourceHandles[i].y - p.y) <= radius) return static_cast<int>(i);
        }
        if (PointInRect(preview, p)) return 10;
        return -1;
    }
    if (object.type == ObjectType::Callout) {
        if (!object.points.empty() && std::abs(object.points[0].x - p.x) <= radius && std::abs(object.points[0].y - p.y) <= radius) return 4;
        const auto b = NormalizeRect(object.rect);
        const std::array<D2D1_POINT_2F, 4> handles{
            D2D1::Point2F(b.left, b.top), D2D1::Point2F(b.right, b.top), D2D1::Point2F(b.left, b.bottom), D2D1::Point2F(b.right, b.bottom)
        };
        for (size_t i = 0; i < handles.size(); ++i) {
            if (std::abs(handles[i].x - p.x) <= radius && std::abs(handles[i].y - p.y) <= radius) return static_cast<int>(i);
        }
        return -1;
    }
    if (IsTwoPointObject(object)) {
        for (int i = 0; i < 2; ++i) {
            if (std::abs(object.points[i].x - p.x) <= radius && std::abs(object.points[i].y - p.y) <= radius) return i;
        }
        return -1;
    }
    const auto b = Bounds(object);
    const std::array<D2D1_POINT_2F, 4> handles{
        D2D1::Point2F(b.left, b.top), D2D1::Point2F(b.right, b.top), D2D1::Point2F(b.left, b.bottom), D2D1::Point2F(b.right, b.bottom)
    };
    for (size_t i = 0; i < handles.size(); ++i) {
        if (std::abs(handles[i].x - p.x) <= radius && std::abs(handles[i].y - p.y) <= radius) return static_cast<int>(i);
    }
    return -1;
}

void ResizeRectCorners(D2D1_RECT_F& rect, int handle, D2D1_POINT_2F p) {
    auto next = NormalizeRect(rect);
    if (handle == 0 || handle == 2) next.left = p.x; else next.right = p.x;
    if (handle == 0 || handle == 1) next.top = p.y; else next.bottom = p.y;
    next = NormalizeRect(next);
    if (next.right - next.left >= 2.0f && next.bottom - next.top >= 2.0f) rect = next;
}

void ResizeObject(Object& object, int handle, D2D1_POINT_2F p) {
    if (object.type == ObjectType::Loupe && handle == 10) return;
    if (object.type == ObjectType::Loupe) {
        ResizeRectCorners(object.rect, handle, p);
        if (object.fill) SquareLoupeRect(object.rect);
        return;
    }
    if (object.type == ObjectType::Callout && handle == 4) {
        if (object.points.empty()) object.points.push_back(p);
        else object.points[0] = p;
        return;
    }
    if (object.type == ObjectType::Callout) { ResizeRectCorners(object.rect, handle, p); return; }
    if (IsTwoPointObject(object)) { object.points[handle] = p; return; }
    const auto old = Bounds(object);
    auto next = old;
    if (handle == 0 || handle == 2) next.left = p.x; else next.right = p.x;
    if (handle == 0 || handle == 1) next.top = p.y; else next.bottom = p.y;
    next = NormalizeRect(next);
    if (next.right - next.left < 2.0f || next.bottom - next.top < 2.0f) return;
    if (object.type == ObjectType::Text) {
        object.rect = next; object.extra = std::clamp(next.bottom - next.top, 10.0f, 256.0f); return;
    }
    if (object.type == ObjectType::NumberStamp || object.type == ObjectType::IconStamp) {
        const float d = std::max(next.right - next.left, next.bottom - next.top);
        const float cx = (next.left + next.right) * 0.5f, cy = (next.top + next.bottom) * 0.5f;
        object.extra = d; object.rect = D2D1::RectF(cx - d * 0.5f, cy - d * 0.5f, cx + d * 0.5f, cy + d * 0.5f); return;
    }
    if (object.type == ObjectType::Freehand) {
        const float ow = std::max(1.0f, old.right - old.left), oh = std::max(1.0f, old.bottom - old.top);
        for (auto& point : object.points) {
            point.x = next.left + (point.x - old.left) * (next.right - next.left) / ow;
            point.y = next.top + (point.y - old.top) * (next.bottom - next.top) / oh;
        }
        return;
    }
    object.rect = next;
}

int HitCropHandle(D2D1_POINT_2F p) {
    const auto r = NormalizeRect(g.cropRect);
    const float radius = 8.0f / g.zoom;
    const std::array<D2D1_POINT_2F, 8> handles{
        D2D1::Point2F(r.left, r.top), D2D1::Point2F((r.left + r.right) * 0.5f, r.top), D2D1::Point2F(r.right, r.top), D2D1::Point2F(r.right, (r.top + r.bottom) * 0.5f),
        D2D1::Point2F(r.right, r.bottom), D2D1::Point2F((r.left + r.right) * 0.5f, r.bottom), D2D1::Point2F(r.left, r.bottom), D2D1::Point2F(r.left, (r.top + r.bottom) * 0.5f)
    };
    for (size_t i = 0; i < handles.size(); ++i) {
        if (std::abs(handles[i].x - p.x) <= radius && std::abs(handles[i].y - p.y) <= radius) return static_cast<int>(i);
    }
    return -1;
}

void ResizeCrop(int handle, D2D1_POINT_2F p) {
    auto r = NormalizeRect(g.cropRect);
    switch (handle) {
    case 0: r.left = p.x; r.top = p.y; break;
    case 1: r.top = p.y; break;
    case 2: r.right = p.x; r.top = p.y; break;
    case 3: r.right = p.x; break;
    case 4: r.right = p.x; r.bottom = p.y; break;
    case 5: r.bottom = p.y; break;
    case 6: r.left = p.x; r.bottom = p.y; break;
    case 7: r.left = p.x; break;
    }
    r = NormalizeRect(r);
    r.left = std::clamp(r.left, 0.0f, static_cast<float>(g.doc.width - 1));
    r.top = std::clamp(r.top, 0.0f, static_cast<float>(g.doc.height - 1));
    r.right = std::clamp(r.right, r.left + 1.0f, static_cast<float>(g.doc.width));
    r.bottom = std::clamp(r.bottom, r.top + 1.0f, static_cast<float>(g.doc.height));
    g.cropRect = r;
}

HCURSOR CropResizeCursor(int handle) {
    switch (handle) {
    case 0: case 4: return LoadCursorW(nullptr, IDC_SIZENWSE);
    case 2: case 6: return LoadCursorW(nullptr, IDC_SIZENESW);
    case 1: case 5: return LoadCursorW(nullptr, IDC_SIZENS);
    case 3: case 7: return LoadCursorW(nullptr, IDC_SIZEWE);
    default: return LoadCursorW(nullptr, IDC_CROSS);
    }
}

HCURSOR LoupeResizeCursor(int handle) {
    switch (handle) {
    case 0: case 3: return LoadCursorW(nullptr, IDC_SIZENWSE);
    case 1: case 2: return LoadCursorW(nullptr, IDC_SIZENESW);
    default: return LoadCursorW(nullptr, IDC_CROSS);
    }
}

HCURSOR CanvasCursorAt(D2D1_POINT_2F p) {
    if (g.doc.pixels.empty()) return nullptr;
    if (g.tool == Tool::Crop) {
        if (g.cropActive) {
            const int handle = HitCropHandle(p);
            if (handle >= 0) return CropResizeCursor(handle);
            if (PointInRect(g.cropRect, p)) return LoadCursorW(nullptr, IDC_SIZEALL);
        }
        return LoadCursorW(nullptr, IDC_CROSS);
    }
    if (g.tool == Tool::Loupe) {
        int hitLoupe = -1;
        int32_t topZ = INT32_MIN;
        for (size_t i = 0; i < g.doc.objects.size(); ++i) {
            const auto& object = g.doc.objects[i];
            if (object.type != ObjectType::Loupe || !object.visible) continue;
            if (object.z >= topZ && HitTest(object, p)) {
                topZ = object.z;
                hitLoupe = static_cast<int>(i);
            }
        }
        if (hitLoupe >= 0) {
            const auto& object = g.doc.objects[hitLoupe];
            const int handle = HitHandle(object, p);
            if (handle >= 0 && handle <= 3) return LoupeResizeCursor(handle);
            if (handle == 10 || PointInRect(object.rect, p)) return LoadCursorW(nullptr, IDC_SIZEALL);
        }
        return LoadCursorW(nullptr, IDC_CROSS);
    }
    return nullptr;
}

void FitOrigin() {
    RECT rect{}; GetClientRect(g.canvas, &rect);
    // GetClientRect は物理ピクセル → DIPに変換
    const float dipW = static_cast<float>(rect.right)  / g.dpiScale;
    const float dipH = static_cast<float>(rect.bottom) / g.dpiScale;
    g.origin.x = std::max(16.0f, (dipW - g.doc.width  * g.zoom) / 2.0f);
    g.origin.y = std::max(16.0f, (dipH - g.doc.height * g.zoom) / 2.0f);
}

void ZoomBy(float factor) { g.zoom = std::clamp(g.zoom * factor, 0.1f, 8.0f); FitOrigin(); InvalidateCanvas(); }

// マウス物理ピクセル座標 → 画像座標（DIP経由）
D2D1_POINT_2F CanvasToImage(LPARAM value) {
    const float dipX = static_cast<float>(GET_X_LPARAM(value)) / g.dpiScale;
    const float dipY = static_cast<float>(GET_Y_LPARAM(value)) / g.dpiScale;
    return D2D1::Point2F((dipX - g.origin.x) / g.zoom, (dipY - g.origin.y) / g.zoom);
}

bool ApplyImageResize(float scale) {
    if (g.doc.pixels.empty()) return false;
    const UINT oldWidth = g.doc.width, oldHeight = g.doc.height;
    const UINT newWidth = std::max(1U, static_cast<UINT>(std::lround(static_cast<double>(oldWidth) * scale)));
    const UINT newHeight = std::max(1U, static_cast<UINT>(std::lround(static_cast<double>(oldHeight) * scale)));
    ComPtr<IWICBitmap> source;
    HRESULT hr = CreateWicBitmapFromPixels(g.doc.pixels, oldWidth, oldHeight, &source);
    ComPtr<IWICBitmapScaler> scaler;
    if (SUCCEEDED(hr)) hr = g.wic->CreateBitmapScaler(&scaler);
    HRESULT scaleHr = E_FAIL;
    if (SUCCEEDED(hr)) scaleHr = scaler->Initialize(source.Get(), newWidth, newHeight, WICBitmapInterpolationModeFant);
    if (FAILED(scaleHr)) {
        scaler.Reset();
        hr = g.wic->CreateBitmapScaler(&scaler);
        if (SUCCEEDED(hr)) scaleHr = scaler->Initialize(source.Get(), newWidth, newHeight, WICBitmapInterpolationModeLinear);
    }
    hr = scaleHr;
    std::vector<uint8_t> pixels;
    UINT width{}, height{};
    if (SUCCEEDED(hr)) hr = DecodeSource(scaler.Get(), pixels, width, height);
    if (FAILED(hr)) { ShowError(g.mainWindow, L"画像の縮小に失敗しました。"); return false; }
    g.doc.width = width; g.doc.height = height; g.doc.pixels = std::move(pixels);
    for (auto& object : g.doc.objects) {
        object.rect.left *= scale; object.rect.right *= scale; object.rect.top *= scale; object.rect.bottom *= scale;
        for (auto& p : object.points) { p.x *= scale; p.y *= scale; }
        if (object.type == ObjectType::Text || object.type == ObjectType::NumberStamp || object.type == ObjectType::IconStamp || object.type == ObjectType::Callout) object.extra *= scale;
    }
    EncodePixels(g.doc.pixels, g.doc.width, g.doc.height, GUID_ContainerFormatPng, g.doc.png);
    g.cropActive = false;
    InvalidateDocBitmap();
    PushHistory();
    FitOrigin();
    ApplySelectionToProperties();
    InvalidateCanvas();
    return true;
}

bool ApplyCrop() {
    if (!g.cropActive || g.doc.pixels.empty()) return false;
    const auto r = NormalizeRect(g.cropRect);
    const UINT left = static_cast<UINT>(std::clamp(std::floor(r.left), 0.0f, static_cast<float>(g.doc.width - 1)));
    const UINT top = static_cast<UINT>(std::clamp(std::floor(r.top), 0.0f, static_cast<float>(g.doc.height - 1)));
    const UINT right = static_cast<UINT>(std::clamp(std::ceil(r.right), 1.0f, static_cast<float>(g.doc.width)));
    const UINT bottom = static_cast<UINT>(std::clamp(std::ceil(r.bottom), 1.0f, static_cast<float>(g.doc.height)));
    if (right <= left || bottom <= top) return false;
    const UINT width = right - left, height = bottom - top;
    std::vector<uint8_t> next(static_cast<size_t>(width) * height * 4U);
    for (UINT y = 0; y < height; ++y) {
        const auto* src = g.doc.pixels.data() + (static_cast<size_t>(top + y) * g.doc.width + left) * 4U;
        auto* dst = next.data() + static_cast<size_t>(y) * width * 4U;
        std::memcpy(dst, src, width * 4U);
    }
    g.doc.width = width; g.doc.height = height; g.doc.pixels = std::move(next);
    for (auto& object : g.doc.objects) MoveObject(object, -static_cast<float>(left), -static_cast<float>(top));
    EncodePixels(g.doc.pixels, g.doc.width, g.doc.height, GUID_ContainerFormatPng, g.doc.png);
    g.cropActive = false; g.selected = -1;
    InvalidateDocBitmap();
    PushHistory();
    FitOrigin();
    ApplySelectionToProperties();
    InvalidateCanvas();
    return true;
}

std::vector<uint8_t> EffectPixels(const Object& object) {
    auto result = g.doc.pixels;
    const auto r = NormalizeRect(object.rect);
    const int left = std::clamp(static_cast<int>(std::floor(r.left)), 0, static_cast<int>(g.doc.width));
    const int top = std::clamp(static_cast<int>(std::floor(r.top)), 0, static_cast<int>(g.doc.height));
    const int right = std::clamp(static_cast<int>(std::ceil(r.right)), 0, static_cast<int>(g.doc.width));
    const int bottom = std::clamp(static_cast<int>(std::ceil(r.bottom)), 0, static_cast<int>(g.doc.height));
    if (left >= right || top >= bottom) return result;
    auto pixel = [&](int x, int y) { return (static_cast<size_t>(y) * g.doc.width + x) * 4U; };
    if (object.type == ObjectType::MosaicRegion) {
        const int block = std::clamp(static_cast<int>(object.blockSize), 2, 128);
        for (int by = top; by < bottom; by += block) {
            for (int bx = left; bx < right; bx += block) {
                std::array<uint64_t, 4> sum{};
                const int ex = std::min(bx + block, right), ey = std::min(by + block, bottom);
                uint64_t count = 0;
                for (int y = by; y < ey; ++y) for (int x = bx; x < ex; ++x) {
                    const auto i = pixel(x, y); for (int c = 0; c < 4; ++c) sum[c] += g.doc.pixels[i + c]; ++count;
                }
                for (int y = by; y < ey; ++y) for (int x = bx; x < ex; ++x) {
                    const auto i = pixel(x, y); for (int c = 0; c < 4; ++c) result[i + c] = static_cast<uint8_t>(sum[c] / count);
                }
            }
        }
    } else {
        const int radius = std::clamp(static_cast<int>(object.extra), 1, 32);
        auto horizontal = result;
        for (int y = top; y < bottom; ++y) {
            std::array<uint64_t, 4> sum{}; int count = 0;
            for (int sx = left; sx <= std::min(right - 1, left + radius); ++sx) {
                const auto i = pixel(sx, y); for (int c = 0; c < 4; ++c) sum[c] += g.doc.pixels[i + c]; ++count;
            }
            for (int x = left; x < right; ++x) {
                const auto i = pixel(x, y); for (int c = 0; c < 4; ++c) horizontal[i + c] = static_cast<uint8_t>(sum[c] / static_cast<uint64_t>(count));
                const int removeX = x - radius;
                if (removeX >= left) { const auto ri = pixel(removeX, y); for (int c = 0; c < 4; ++c) sum[c] -= g.doc.pixels[ri + c]; --count; }
                const int addX = x + radius + 1;
                if (addX < right) { const auto ai = pixel(addX, y); for (int c = 0; c < 4; ++c) sum[c] += g.doc.pixels[ai + c]; ++count; }
            }
        }
        for (int x = left; x < right; ++x) {
            std::array<uint64_t, 4> sum{}; int count = 0;
            for (int sy = top; sy <= std::min(bottom - 1, top + radius); ++sy) {
                const auto i = pixel(x, sy); for (int c = 0; c < 4; ++c) sum[c] += horizontal[i + c]; ++count;
            }
            for (int y = top; y < bottom; ++y) {
                const auto i = pixel(x, y); for (int c = 0; c < 4; ++c) result[i + c] = static_cast<uint8_t>(sum[c] / static_cast<uint64_t>(count));
                const int removeY = y - radius;
                if (removeY >= top) { const auto ri = pixel(x, removeY); for (int c = 0; c < 4; ++c) sum[c] -= horizontal[ri + c]; --count; }
                const int addY = y + radius + 1;
                if (addY < bottom) { const auto ai = pixel(x, addY); for (int c = 0; c < 4; ++c) sum[c] += horizontal[ai + c]; ++count; }
            }
        }
    }
    return result;
}

void DrawSelectionHandles(ID2D1RenderTarget* target, const Object& object) {
    ComPtr<ID2D1SolidColorBrush> brush;
    target->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::DodgerBlue), &brush);
    if (IsTwoPointObject(object)) {
        for (auto p : object.points) target->FillRectangle(D2D1::RectF(p.x - 4, p.y - 4, p.x + 4, p.y + 4), brush.Get());
        return;
    }
    if (object.type == ObjectType::Loupe) {
        const auto src = NormalizeRect(object.rect);
        const auto preview = LoupePreviewRect(object);
        target->DrawRectangle(src, brush.Get(), 1.0f);
        target->DrawRectangle(preview, brush.Get(), 1.0f);
        for (auto box : {src, preview}) {
            for (auto p : {D2D1::Point2F(box.left, box.top), D2D1::Point2F(box.right, box.top), D2D1::Point2F(box.left, box.bottom), D2D1::Point2F(box.right, box.bottom)}) {
                target->FillRectangle(D2D1::RectF(p.x - 3, p.y - 3, p.x + 3, p.y + 3), brush.Get());
            }
        }
        return;
    }
    if (object.type == ObjectType::Callout) {
        const auto b = NormalizeRect(object.rect);
        target->DrawRectangle(b, brush.Get(), 1.0f);
        for (auto p : {D2D1::Point2F(b.left, b.top), D2D1::Point2F(b.right, b.top), D2D1::Point2F(b.left, b.bottom), D2D1::Point2F(b.right, b.bottom)}) {
            target->FillRectangle(D2D1::RectF(p.x - 3, p.y - 3, p.x + 3, p.y + 3), brush.Get());
        }
        if (!object.points.empty()) {
            const auto t = object.points[0];
            target->FillRectangle(D2D1::RectF(t.x - 4, t.y - 4, t.x + 4, t.y + 4), brush.Get());
        }
        return;
    }
    const auto b = Bounds(object);
    target->DrawRectangle(b, brush.Get(), 1.0f);
    for (auto p : {D2D1::Point2F(b.left, b.top), D2D1::Point2F(b.right, b.top), D2D1::Point2F(b.left, b.bottom), D2D1::Point2F(b.right, b.bottom)}) {
        target->FillRectangle(D2D1::RectF(p.x - 3, p.y - 3, p.x + 3, p.y + 3), brush.Get());
    }
}

D2D1_ELLIPSE InscribedCircle(const D2D1_RECT_F& r) {
    const float cx = (r.left + r.right) * 0.5f, cy = (r.top + r.bottom) * 0.5f;
    const float rad = std::max(1.0f, std::min(r.right - r.left, r.bottom - r.top) * 0.5f);
    return D2D1_ELLIPSE{{cx, cy}, rad, rad};
}

D2D1_RECT_F CircleBounds(const D2D1_ELLIPSE& e) {
    return D2D1::RectF(e.point.x - e.radiusX, e.point.y - e.radiusY, e.point.x + e.radiusX, e.point.y + e.radiusY);
}

void DrawLoupeConnectors(ID2D1RenderTarget* target, ID2D1Brush* brush, const D2D1_RECT_F& src, const D2D1_RECT_F& preview, bool circle, float stroke) {
    if (circle) {
        const auto c1 = InscribedCircle(src);
        const auto c2 = InscribedCircle(preview);
        const float dx = c2.point.x - c1.point.x, dy = c2.point.y - c1.point.y;
        const float d = std::hypot(dx, dy);
        if (d < 1.0f) return;
        const float vx = dx / d, vy = dy / d;
        const float c = std::clamp((c1.radiusX - c2.radiusX) / d, -1.0f, 1.0f);
        const float h = std::sqrt(std::max(0.0f, 1.0f - c * c));
        for (float sign : {1.0f, -1.0f}) {
            const float nx = vx * c - sign * h * vy;
            const float ny = vy * c + sign * h * vx;
            target->DrawLine(D2D1::Point2F(c1.point.x + c1.radiusX * nx, c1.point.y + c1.radiusX * ny),
                             D2D1::Point2F(c2.point.x + c2.radiusX * nx, c2.point.y + c2.radiusX * ny), brush, stroke);
        }
        return;
    }
    const std::array<D2D1_POINT_2F, 4> a{
        D2D1::Point2F(src.left, src.top), D2D1::Point2F(src.right, src.top),
        D2D1::Point2F(src.right, src.bottom), D2D1::Point2F(src.left, src.bottom)
    };
    const std::array<D2D1_POINT_2F, 4> b{
        D2D1::Point2F(preview.left, preview.top), D2D1::Point2F(preview.right, preview.top),
        D2D1::Point2F(preview.right, preview.bottom), D2D1::Point2F(preview.left, preview.bottom)
    };
    std::array<D2D1_POINT_2F, 8> pts{};
    for (int i = 0; i < 4; ++i) { pts[i] = a[i]; pts[i + 4] = b[i]; }
    int start = 0;
    for (int i = 1; i < 8; ++i) if (pts[i].x < pts[start].x || (pts[i].x == pts[start].x && pts[i].y < pts[start].y)) start = i;
    std::vector<int> hull;
    int current = start;
    do {
        hull.push_back(current);
        int next = (current + 1) % 8;
        for (int i = 0; i < 8; ++i) {
            const float cross = (pts[next].x - pts[current].x) * (pts[i].y - pts[current].y) -
                                (pts[next].y - pts[current].y) * (pts[i].x - pts[current].x);
            if (cross < 0.0f) next = i;
        }
        current = next;
    } while (current != start && hull.size() < 8);
    for (size_t i = 0; i < hull.size(); ++i) {
        const int i0 = hull[i], i1 = hull[(i + 1) % hull.size()];
        if ((i0 < 4) != (i1 < 4)) target->DrawLine(pts[i0], pts[i1], brush, stroke);
    }
}

void DrawLoupe(ID2D1RenderTarget* target, const Object& object) {
    if (g.doc.pixels.empty()) return;
    const auto src = NormalizeRect(object.rect);
    const auto preview = LoupePreviewRect(object);
    const float srcW = src.right - src.left, srcH = src.bottom - src.top;
    if (srcW < 2.0f || srcH < 2.0f) return;
    ComPtr<ID2D1Bitmap> bitmap;
    const D2D1_BITMAP_PROPERTIES props{D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f};
    if (FAILED(target->CreateBitmap(D2D1::SizeU(g.doc.width, g.doc.height), g.doc.pixels.data(), g.doc.width * 4U, props, &bitmap))) return;
    ComPtr<ID2D1SolidColorBrush> outline;
    target->CreateSolidColorBrush(ColorF(object.color ? object.color : g.color1), &outline);
    const float stroke = std::max(1.0f, object.width);
    if (object.fill) {
        const auto srcCircle = InscribedCircle(src);
        const auto previewCircle = InscribedCircle(preview);
        const auto srcSample = CircleBounds(srcCircle);
        ComPtr<ID2D1EllipseGeometry> geometry;
        if (SUCCEEDED(g.d2d->CreateEllipseGeometry(previewCircle, &geometry))) {
            ComPtr<ID2D1Layer> layer;
            if (SUCCEEDED(target->CreateLayer(&layer))) {
                target->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), geometry.Get()), layer.Get());
                target->DrawBitmap(bitmap.Get(), CircleBounds(previewCircle), 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, srcSample);
                target->PopLayer();
            }
        }
        target->DrawEllipse(srcCircle, outline.Get(), stroke);
        target->DrawEllipse(previewCircle, outline.Get(), stroke);
    } else {
        target->DrawBitmap(bitmap.Get(), preview, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, src);
        target->DrawRectangle(src, outline.Get(), stroke);
        target->DrawRectangle(preview, outline.Get(), stroke);
    }
    DrawLoupeConnectors(target, outline.Get(), src, preview, object.fill, stroke);
}

void DrawOutlinedStroke(ID2D1RenderTarget* target, const Object& object,
    const std::function<void(ID2D1Brush*, float, ID2D1StrokeStyle*)>& stroke) {
    if (object.hasOutline) {
        ComPtr<ID2D1SolidColorBrush> outline;
        auto color = ColorF(object.fillColor);
        color.a = std::clamp(object.outlineOpacity, 0.10f, 1.0f);
        target->CreateSolidColorBrush(color, &outline);
        stroke(outline.Get(), object.width + object.outlineWidth * 2.0f, g.roundStroke.Get());
    }
    ComPtr<ID2D1SolidColorBrush> brush;
    target->CreateSolidColorBrush(ColorF(object.color), &brush);
    stroke(brush.Get(), object.width, StrokeStyleFor(object));
}

struct OutlineTextRenderer : IDWriteTextRenderer {
    ID2D1RenderTarget* target{};
    ID2D1Brush* fill{};
    ID2D1Brush* outline{};
    float outlineWidth = 0.0f;
    ULONG refs = 1;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IDWritePixelSnapping) || iid == __uuidof(IDWriteTextRenderer)) {
            *object = static_cast<IDWriteTextRenderer*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG value = --refs;
        if (value == 0) delete this;
        return value;
    }
    HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void*, BOOL* isDisabled) override { *isDisabled = FALSE; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetCurrentTransform(void*, DWRITE_MATRIX* transform) override {
        D2D1_MATRIX_3X2_F matrix{};
        target->GetTransform(&matrix);
        *transform = *reinterpret_cast<DWRITE_MATRIX*>(&matrix);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void*, FLOAT* pixelsPerDip) override {
        FLOAT dpiX = 96.0f, dpiY = 96.0f;
        target->GetDpi(&dpiX, &dpiY);
        *pixelsPerDip = dpiX / 96.0f;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawGlyphRun(void*, FLOAT baselineX, FLOAT baselineY, DWRITE_MEASURING_MODE,
        const DWRITE_GLYPH_RUN* glyphRun, const DWRITE_GLYPH_RUN_DESCRIPTION*, IUnknown*) override {
        if (!glyphRun || !glyphRun->fontFace || glyphRun->glyphCount == 0) return S_OK;
        ComPtr<ID2D1PathGeometry> path;
        ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(g.d2d->CreatePathGeometry(&path)) || FAILED(path->Open(&sink))) return S_OK;
        const HRESULT hr = glyphRun->fontFace->GetGlyphRunOutline(
            glyphRun->fontEmSize, glyphRun->glyphIndices, glyphRun->glyphAdvances, glyphRun->glyphOffsets,
            glyphRun->glyphCount, glyphRun->isSideways, glyphRun->bidiLevel % 2 == 1, sink.Get());
        sink->Close();
        if (FAILED(hr)) return S_OK;
        const auto translate = D2D1::Matrix3x2F::Translation(baselineX, baselineY);
        ComPtr<ID2D1TransformedGeometry> transformed;
        if (FAILED(g.d2d->CreateTransformedGeometry(path.Get(), translate, &transformed))) return S_OK;
        if (outline && outlineWidth > 0.0f) target->DrawGeometry(transformed.Get(), outline, outlineWidth * 2.0f, g.roundStroke.Get());
        if (fill) target->FillGeometry(transformed.Get(), fill);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawUnderline(void*, FLOAT, FLOAT, const DWRITE_UNDERLINE*, IUnknown*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawStrikethrough(void*, FLOAT, FLOAT, const DWRITE_STRIKETHROUGH*, IUnknown*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawInlineObject(void*, FLOAT, FLOAT, IDWriteInlineObject*, BOOL, BOOL, IUnknown*) override { return S_OK; }
};

void DrawTextWithOutline(ID2D1RenderTarget* target, const Object& object, IDWriteTextFormat* format, const D2D1_RECT_F& r) {
    if (object.text.empty()) return;
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(g.dwrite->CreateTextLayout(object.text.c_str(), static_cast<UINT32>(object.text.size()), format,
                                          std::max(1.0f, r.right - r.left), std::max(1.0f, r.bottom - r.top), &layout))) {
        return;
    }
    ComPtr<ID2D1SolidColorBrush> fill;
    target->CreateSolidColorBrush(ColorF(object.color), &fill);
    ComPtr<ID2D1SolidColorBrush> outline;
    if (object.hasOutline) {
        auto color = ColorF(object.fillColor);
        color.a = std::clamp(object.outlineOpacity, 0.10f, 1.0f);
        target->CreateSolidColorBrush(color, &outline);
    }
    auto* renderer = new OutlineTextRenderer();
    renderer->target = target;
    renderer->fill = fill.Get();
    renderer->outline = outline.Get();
    renderer->outlineWidth = object.hasOutline ? object.outlineWidth : 0.0f;
    layout->Draw(nullptr, renderer, r.left, r.top);
    renderer->Release();
}

void ExpandCalloutForText(Object& object) {
    if (object.text.empty() || !g.dwrite) return;
    ComPtr<IDWriteTextFormat> format;
    if (FAILED(g.dwrite->CreateTextFormat(object.font.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                          DWRITE_FONT_STRETCH_NORMAL, object.extra, L"ja-JP", &format))) return;
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    auto r = NormalizeRect(object.rect);
    const float wrap = std::max(40.0f, r.right - r.left - 24.0f);
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(g.dwrite->CreateTextLayout(object.text.c_str(), static_cast<UINT32>(object.text.size()), format.Get(), wrap, 2000.0f, &layout))) return;
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    const float needW = metrics.width + 28.0f;
    const float needH = metrics.height + 20.0f;
    if (needW > r.right - r.left) r.right = r.left + needW;
    if (needH > r.bottom - r.top) r.bottom = r.top + needH;
    object.rect = r;
}

int CalloutTipSide(const D2D1_RECT_F& r, D2D1_POINT_2F tip) {
    const float cx = (r.left + r.right) * 0.5f, cy = (r.top + r.bottom) * 0.5f;
    const float dx = tip.x - cx, dy = tip.y - cy;
    if (std::abs(dx) > std::abs(dy)) return dx < 0.0f ? 2 : 3;
    return dy < 0.0f ? 1 : 0;
}

void AddCalloutPath(ID2D1GeometrySink* sink, const D2D1_RECT_F& r, D2D1_POINT_2F tip, float radius) {
    const int side = CalloutTipSide(r, tip);
    const float cx = (r.left + r.right) * 0.5f, cy = (r.top + r.bottom) * 0.5f;
    const float base = 16.0f;
    auto arc = [&](D2D1_POINT_2F end) {
        sink->AddArc(D2D1::ArcSegment(end, D2D1::SizeF(radius, radius), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
    };
    sink->BeginFigure(D2D1::Point2F(r.left + radius, r.top), D2D1_FIGURE_BEGIN_FILLED);
    if (side == 1) {
        sink->AddLine(D2D1::Point2F(cx - base, r.top));
        sink->AddLine(tip);
        sink->AddLine(D2D1::Point2F(cx + base, r.top));
        sink->AddLine(D2D1::Point2F(r.right - radius, r.top));
    } else {
        sink->AddLine(D2D1::Point2F(r.right - radius, r.top));
    }
    arc(D2D1::Point2F(r.right, r.top + radius));
    if (side == 3) {
        sink->AddLine(D2D1::Point2F(r.right, cy - base));
        sink->AddLine(tip);
        sink->AddLine(D2D1::Point2F(r.right, cy + base));
        sink->AddLine(D2D1::Point2F(r.right, r.bottom - radius));
    } else {
        sink->AddLine(D2D1::Point2F(r.right, r.bottom - radius));
    }
    arc(D2D1::Point2F(r.right - radius, r.bottom));
    if (side == 0) {
        sink->AddLine(D2D1::Point2F(cx + base, r.bottom));
        sink->AddLine(tip);
        sink->AddLine(D2D1::Point2F(cx - base, r.bottom));
        sink->AddLine(D2D1::Point2F(r.left + radius, r.bottom));
    } else {
        sink->AddLine(D2D1::Point2F(r.left + radius, r.bottom));
    }
    arc(D2D1::Point2F(r.left, r.bottom - radius));
    if (side == 2) {
        sink->AddLine(D2D1::Point2F(r.left, cy + base));
        sink->AddLine(tip);
        sink->AddLine(D2D1::Point2F(r.left, cy - base));
        sink->AddLine(D2D1::Point2F(r.left, r.top + radius));
    } else {
        sink->AddLine(D2D1::Point2F(r.left, r.top + radius));
    }
    arc(D2D1::Point2F(r.left + radius, r.top));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
}

void DrawCallout(ID2D1RenderTarget* target, const Object& object, bool selected) {
    const auto r = NormalizeRect(object.rect);
    const D2D1_POINT_2F tip = object.points.empty() ? D2D1::Point2F((r.left + r.right) * 0.5f, r.bottom + 28.0f) : object.points[0];
    const float radius = std::clamp(std::min(r.right - r.left, r.bottom - r.top) * 0.2f, 8.0f, 18.0f);
    ComPtr<ID2D1PathGeometry> path;
    ComPtr<ID2D1GeometrySink> sink;
    if (SUCCEEDED(g.d2d->CreatePathGeometry(&path)) && SUCCEEDED(path->Open(&sink))) {
        AddCalloutPath(sink.Get(), r, tip, radius);
        sink->Close();
        if (object.fill) {
            auto fill = ColorF(object.fillColor);
            fill.a = std::clamp(object.outlineOpacity, 0.10f, 1.0f);
            ComPtr<ID2D1SolidColorBrush> fillBrush;
            target->CreateSolidColorBrush(fill, &fillBrush);
            target->FillGeometry(path.Get(), fillBrush.Get());
        }
        ComPtr<ID2D1SolidColorBrush> stroke;
        target->CreateSolidColorBrush(ColorF(object.color), &stroke);
        target->DrawGeometry(path.Get(), stroke.Get(), object.width, g.roundStroke.Get());
    }
    ComPtr<IDWriteTextFormat> format;
    if (SUCCEEDED(g.dwrite->CreateTextFormat(object.font.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                             DWRITE_FONT_STRETCH_NORMAL, object.extra, L"ja-JP", &format))) {
        format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        ComPtr<ID2D1SolidColorBrush> textBrush;
        target->CreateSolidColorBrush(ColorF(object.color), &textBrush);
        target->DrawTextW(object.text.c_str(), static_cast<UINT32>(object.text.size()), format.Get(), r, textBrush.Get());
        if (selected && g.textEditing && g.caretVisible) {
            ComPtr<ID2D1SolidColorBrush> caret;
            target->CreateSolidColorBrush(ColorF(object.color), &caret);
            target->DrawLine(D2D1::Point2F((r.left + r.right) * 0.5f, r.top + 8.0f),
                             D2D1::Point2F((r.left + r.right) * 0.5f, r.bottom - 8.0f), caret.Get(), 1.5f);
        }
    }
}

void DrawObject(ID2D1RenderTarget* target, const Object& object, bool selected, bool exportMode);
IDWriteFontCollection* IconFontCollection();

void DrawObject(ID2D1RenderTarget* target, const Object& object, bool selected, bool exportMode) {
    if (!object.visible) return;
    ComPtr<ID2D1SolidColorBrush> brush;
    target->CreateSolidColorBrush(ColorF(object.color), &brush);
    const auto r = NormalizeRect(object.rect);
    switch (object.type) {
    case ObjectType::Arrow:
        if (object.points.size() >= 2) {
            DrawOutlinedStroke(target, object, [&](ID2D1Brush* b, float w, ID2D1StrokeStyle* style) {
                // fill フラグが true のとき向き反転（後ろ→先が通常方向）
                DrawArrowLine(target, b,
                    object.fill ? object.points[0] : object.points[1],
                    object.fill ? object.points[1] : object.points[0],
                    w, object.extra, style);
            });
        }
        break;
    case ObjectType::Line:
        if (object.points.size() >= 2) {
            DrawOutlinedStroke(target, object, [&](ID2D1Brush* b, float w, ID2D1StrokeStyle* style) {
                target->DrawLine(object.points[1], object.points[0], b, w, style);
            });
        }
        break;
    case ObjectType::Rectangle:
        DrawOutlinedStroke(target, object, [&](ID2D1Brush* b, float w, ID2D1StrokeStyle* style) { target->DrawRectangle(r, b, w, style); });
        break;
    case ObjectType::Ellipse: {
        const D2D1_ELLIPSE ellipse{{(r.left + r.right) * 0.5f, (r.top + r.bottom) * 0.5f}, (r.right - r.left) * 0.5f, (r.bottom - r.top) * 0.5f};
        DrawOutlinedStroke(target, object, [&](ID2D1Brush* b, float w, ID2D1StrokeStyle* style) { target->DrawEllipse(ellipse, b, w, style); });
        break;
    }
    case ObjectType::Text: {
        ComPtr<IDWriteTextFormat> format;
        if (SUCCEEDED(g.dwrite->CreateTextFormat(object.font.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, object.extra, L"ja-JP", &format))) {
            format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            DrawTextWithOutline(target, object, format.Get(), r);
            if (selected && g.textEditing && g.caretVisible) {
                ComPtr<IDWriteTextLayout> layout;
                const auto* text = object.text.empty() ? L" " : object.text.c_str();
                const UINT32 count = object.text.empty() ? 1 : static_cast<UINT32>(object.text.size());
                if (SUCCEEDED(g.dwrite->CreateTextLayout(text, count, format.Get(), 4096.0f, object.extra * 2.0f, &layout))) {
                    DWRITE_TEXT_METRICS metrics{};
                    layout->GetMetrics(&metrics);
                    const float caretX = r.left + (object.text.empty() ? 0.0f : metrics.widthIncludingTrailingWhitespace);
                    target->DrawLine(D2D1::Point2F(caretX, r.top), D2D1::Point2F(caretX, r.bottom), brush.Get(), 1.5f);
                }
            }
        }
        break;
    }
    case ObjectType::Freehand:
        DrawOutlinedStroke(target, object, [&](ID2D1Brush* b, float w, ID2D1StrokeStyle* style) {
            if (object.points.size() < 2) return;
            ComPtr<ID2D1PathGeometry> path;
            ComPtr<ID2D1GeometrySink> sink;
            if (FAILED(g.d2d->CreatePathGeometry(&path)) || FAILED(path->Open(&sink))) return;
            sink->BeginFigure(object.points[0], D2D1_FIGURE_BEGIN_HOLLOW);
            for (size_t i = 1; i < object.points.size(); ++i) sink->AddLine(object.points[i]);
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            sink->Close();
            target->DrawGeometry(path.Get(), b, w, style);
        });
        break;
    case ObjectType::BlurRegion:
    case ObjectType::MosaicRegion: {
        auto pixels = EffectPixels(object);
        ComPtr<ID2D1Bitmap> bitmap;
        const D2D1_BITMAP_PROPERTIES bmpProps{D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f};
        if (SUCCEEDED(target->CreateBitmap(D2D1::SizeU(g.doc.width, g.doc.height), pixels.data(), g.doc.width * 4U, bmpProps, &bitmap))) {
            if (object.fill) {
                // 丸形状: 楕円ジオメトリでレイヤークリップ
                const D2D1_ELLIPSE ellipse{{(r.left + r.right) * 0.5f, (r.top + r.bottom) * 0.5f},
                    (r.right - r.left) * 0.5f, (r.bottom - r.top) * 0.5f};
                ComPtr<ID2D1EllipseGeometry> geo;
                ComPtr<ID2D1Layer> layer;
                if (SUCCEEDED(g.d2d->CreateEllipseGeometry(ellipse, &geo)) && SUCCEEDED(target->CreateLayer(nullptr, &layer))) {
                    target->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), geo.Get()), layer.Get());
                    target->DrawBitmap(bitmap.Get());
                    target->PopLayer();
                }
            } else {
                target->PushAxisAlignedClip(r, D2D1_ANTIALIAS_MODE_ALIASED);
                target->DrawBitmap(bitmap.Get());
                target->PopAxisAlignedClip();
            }
        }
        break;
    }
    case ObjectType::NumberStamp: {
        const D2D1_ELLIPSE ellipse{{(r.left + r.right) * 0.5f, (r.top + r.bottom) * 0.5f}, (r.right - r.left) * 0.5f, (r.bottom - r.top) * 0.5f};
        target->FillEllipse(ellipse, brush.Get());
        ComPtr<ID2D1SolidColorBrush> textBrush;
        target->CreateSolidColorBrush(object.fill ? ColorF(object.fillColor) : D2D1::ColorF(D2D1::ColorF::White), &textBrush);
        ComPtr<IDWriteTextFormat> format;
        if (SUCCEEDED(g.dwrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, object.extra * 0.55f, L"ja-JP", &format))) {
            format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            target->DrawTextW(object.text.c_str(), static_cast<UINT32>(object.text.size()), format.Get(), r, textBrush.Get());
            if (selected && g.textEditing && g.caretVisible) {
                const float cx = (r.left + r.right) * 0.5f;
                target->DrawLine(D2D1::Point2F(cx + 8.0f, r.top + 8.0f), D2D1::Point2F(cx + 8.0f, r.bottom - 8.0f), textBrush.Get(), 1.5f);
            }
        }
        break;
    }
    case ObjectType::IconStamp: {
        IDWriteFontCollection* collection = IconFontCollection();
        if (!collection) break;
        const uint32_t index = (std::min)(object.blockSize, static_cast<uint32_t>(kStampCount - 1));
        const float glyphSize = (std::min)(r.right - r.left, r.bottom - r.top);
        if (glyphSize <= 0.0f) break;
        ComPtr<IDWriteTextFormat> format;
        if (SUCCEEDED(g.dwrite->CreateTextFormat(kIconFontFamily, collection, DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, glyphSize, L"ja-JP", &format))) {
            format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            const std::wstring_view glyph = kStampGlyphs(index);
            target->DrawTextW(glyph.data(), static_cast<UINT32>(glyph.size()), format.Get(), r, brush.Get());
        }
        break;
    }
    case ObjectType::Loupe:
        DrawLoupe(target, object); break;
    case ObjectType::Callout:
        DrawCallout(target, object, selected); break;
    }
    if (selected && !exportMode) DrawSelectionHandles(target, object);
}

void DrawDocument(ID2D1RenderTarget* target, bool exportMode) {
    if (g.doc.pixels.empty()) return;
    if (exportMode) {
        ComPtr<ID2D1Bitmap> bitmap;
        const D2D1_BITMAP_PROPERTIES props{D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f};
        if (SUCCEEDED(target->CreateBitmap(D2D1::SizeU(g.doc.width, g.doc.height), g.doc.pixels.data(), g.doc.width * 4U, props, &bitmap))) {
            target->DrawBitmap(bitmap.Get());
        }
    } else {
        EnsureDocBitmap(target);
        if (g.docBitmap) target->DrawBitmap(g.docBitmap.Get());
    }
    std::vector<size_t> order(g.doc.objects.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [](size_t a, size_t b) { return g.doc.objects[a].z < g.doc.objects[b].z; });
    for (auto i : order) DrawObject(target, g.doc.objects[i], static_cast<int>(i) == g.selected, exportMode);
    if (g.draft) DrawObject(target, *g.draft, false, exportMode);
}

HRESULT RenderFlattened(std::vector<uint8_t>& pixels) {
    ComPtr<IWICBitmap> wicBitmap;
    HRESULT hr = g.wic->CreateBitmap(g.doc.width, g.doc.height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &wicBitmap);
    ComPtr<ID2D1RenderTarget> target;
    if (SUCCEEDED(hr)) {
        const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        hr = g.d2d->CreateWicBitmapRenderTarget(wicBitmap.Get(), props, &target);
    }
    if (SUCCEEDED(hr)) {
        target->BeginDraw();
        target->Clear(D2D1::ColorF(D2D1::ColorF::White));
        DrawDocument(target.Get(), true);
        hr = target->EndDraw();
    }
    WICRect rect{0, 0, static_cast<INT>(g.doc.width), static_cast<INT>(g.doc.height)};
    ComPtr<IWICBitmapLock> lock;
    if (SUCCEEDED(hr)) hr = wicBitmap->Lock(&rect, WICBitmapLockRead, &lock);
    UINT size{}, stride{}; BYTE* data{};
    if (SUCCEEDED(hr)) hr = lock->GetStride(&stride);
    if (SUCCEEDED(hr)) hr = lock->GetDataPointer(&size, &data);
    if (SUCCEEDED(hr)) {
        pixels.resize(static_cast<size_t>(g.doc.width) * g.doc.height * 4U);
        for (UINT y = 0; y < g.doc.height; ++y) std::memcpy(pixels.data() + static_cast<size_t>(y) * g.doc.width * 4U, data + static_cast<size_t>(y) * stride, g.doc.width * 4U);
    }
    return hr;
}

// --- 書き出し（WIC + オプション libjpeg/libdeflate） ---

bool ExportImage(const std::wstring& path, bool lightweight = false) {
    struct ExportBusy {
        HWND hwnd{};
        std::wstring prevTitle;
        HCURSOR prevCursor{};
        explicit ExportBusy(HWND h) : hwnd(h) {
            if (hwnd) {
                wchar_t buf[256]{};
                GetWindowTextW(hwnd, buf, 256);
                prevTitle = buf;
                SetWindowTextW(hwnd, L"LiteDraw - 書き出し中...");
            }
            prevCursor = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
        }
        ~ExportBusy() {
            if (hwnd) SetWindowTextW(hwnd, prevTitle.c_str());
            SetCursor(prevCursor);
        }
    } busy(g.mainWindow);

    std::vector<uint8_t> pixels, encoded;
    if (FAILED(RenderFlattened(pixels))) return false;
    const auto ext = std::filesystem::path(path).extension().wstring();
    const bool isJpeg = _wcsicmp(ext.c_str(), L".jpg") == 0 || _wcsicmp(ext.c_str(), L".jpeg") == 0;
    const bool isPng = _wcsicmp(ext.c_str(), L".png") == 0;
    const GUID* format = &GUID_ContainerFormatPng;
    if (_wcsicmp(ext.c_str(), L".bmp") == 0) format = &GUID_ContainerFormatBmp;
    else if (isJpeg) format = &GUID_ContainerFormatJpeg;

    bool ok = false;
    if (lightweight) {
        if (isJpeg) {
            std::vector<uint8_t> wicLow, turbo;
            const bool wicOk = SUCCEEDED(EncodePixels(pixels, g.doc.width, g.doc.height, GUID_ContainerFormatJpeg, wicLow, 0.65f));
            const bool turboOk = EncodeJpegTurbo(pixels, g.doc.width, g.doc.height, turbo);
            if (wicOk && turboOk) {
                encoded = wicLow.size() <= turbo.size() ? std::move(wicLow) : std::move(turbo);
                ok = true;
            } else if (wicOk) {
                encoded = std::move(wicLow);
                ok = true;
            } else if (turboOk) {
                encoded = std::move(turbo);
                ok = true;
            }
        } else if (isPng) {
            std::vector<uint8_t> compact, wic;
            const bool compactOk = EncodePngLibdeflate(pixels, g.doc.width, g.doc.height, compact);
            const bool wicOk = SUCCEEDED(EncodePixels(pixels, g.doc.width, g.doc.height, GUID_ContainerFormatPng, wic));
            if (compactOk && wicOk) {
                encoded = compact.size() <= wic.size() ? std::move(compact) : std::move(wic);
                ok = true;
            } else if (compactOk) {
                encoded = std::move(compact);
                ok = true;
            }
        }
    }
    if (!ok) {
        encoded.clear();
        if (FAILED(EncodePixels(pixels, g.doc.width, g.doc.height, *format, encoded))) return false;
    }
    std::ofstream file(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
    return !!file;
}

std::optional<std::wstring> FileDialog(HWND owner, bool save, const wchar_t* filter, const wchar_t* defaultExt, const wchar_t* initialName = nullptr) {
    std::array<wchar_t, 32768> path{};
    if (initialName) wcsncpy_s(path.data(), path.size(), initialName, _TRUNCATE);
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner = owner; dialog.lpstrFilter = filter; dialog.lpstrFile = path.data(); dialog.nMaxFile = static_cast<DWORD>(path.size()); dialog.lpstrDefExt = defaultExt;
    dialog.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog)) return std::wstring(path.data());
    return std::nullopt;
}

struct ExportDialogChoice {
    std::wstring path;
    bool lightweight = false;
};

enum class ExportSourceKind { Png, Bmp, Jpeg };

ExportSourceKind DetectExportSourceKind() {
    const auto ext = std::filesystem::path(g.doc.sourcePath).extension().wstring();
    if (_wcsicmp(ext.c_str(), L".bmp") == 0) return ExportSourceKind::Bmp;
    if (_wcsicmp(ext.c_str(), L".jpg") == 0 || _wcsicmp(ext.c_str(), L".jpeg") == 0) return ExportSourceKind::Jpeg;
    return ExportSourceKind::Png;
}

void ConfigureExportFileTypes(IFileSaveDialog* dialog, ExportSourceKind sourceKind, UINT& typeIndex, const wchar_t*& defaultExt) {
    typeIndex = 1;
    defaultExt = L"png";
    switch (sourceKind) {
    case ExportSourceKind::Bmp:
        typeIndex = 2;
        defaultExt = L"bmp";
        break;
    case ExportSourceKind::Jpeg:
        typeIndex = 3;
        defaultExt = L"jpg";
        break;
    case ExportSourceKind::Png:
        break;
    }
    dialog->SetFileTypeIndex(typeIndex);
    dialog->SetDefaultExtension(defaultExt);
}

constexpr UINT kExportTypePng = 1;
constexpr UINT kExportTypeBmp = 2;
constexpr UINT kExportTypeJpeg = 3;

struct ExportDialogState {
    bool lightweight = false;
    bool lightweightKnown = false;
    bool allowLightweight = true;
};

bool ExportTypeAllowsLightweight(UINT typeIndex) {
    return typeIndex != kExportTypeBmp;
}

void SyncExportLightweightControl(IFileDialogCustomize* customize, UINT typeIndex, ExportDialogState* state) {
    if (!customize || !state) return;
    state->allowLightweight = ExportTypeAllowsLightweight(typeIndex);
    if (state->allowLightweight) {
        customize->SetControlState(IDC_EXPORT_LIGHTWEIGHT, CDCS_ENABLEDVISIBLE);
        customize->SetCheckButtonState(IDC_EXPORT_LIGHTWEIGHT, g.exportLightweight ? TRUE : FALSE);
    } else {
        customize->SetCheckButtonState(IDC_EXPORT_LIGHTWEIGHT, FALSE);
        customize->SetControlState(IDC_EXPORT_LIGHTWEIGHT, CDCS_VISIBLE);
    }
}

class ExportDialogEvents final : public IFileDialogEvents {
public:
    explicit ExportDialogEvents(ExportDialogState* state) : state_(state) {}

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppvObject) override {
        if (!ppvObject) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IFileDialogEvents) {
            *ppvObject = static_cast<IFileDialogEvents*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&refCount_)); }
    IFACEMETHODIMP_(ULONG) Release() override {
        const ULONG count = static_cast<ULONG>(InterlockedDecrement(&refCount_));
        if (count == 0) delete this;
        return count;
    }

    IFACEMETHODIMP OnFileOk(IFileDialog* dialog) override {
        if (!state_) return S_OK;
        ComPtr<IFileSaveDialog> saveDialog;
        ComPtr<IFileDialogCustomize> customize;
        if (FAILED(dialog->QueryInterface(IID_PPV_ARGS(&saveDialog)))) return S_OK;
        if (FAILED(dialog->QueryInterface(IID_PPV_ARGS(&customize)))) return S_OK;
        UINT typeIndex = 0;
        if (SUCCEEDED(saveDialog->GetFileTypeIndex(&typeIndex))) {
            state_->allowLightweight = ExportTypeAllowsLightweight(typeIndex);
        }
        if (state_->allowLightweight) {
            BOOL checked = FALSE;
            if (SUCCEEDED(customize->GetCheckButtonState(IDC_EXPORT_LIGHTWEIGHT, &checked))) {
                state_->lightweight = checked == TRUE;
                state_->lightweightKnown = true;
            }
        } else {
            state_->lightweight = false;
            state_->lightweightKnown = true;
        }
        return S_OK;
    }
    IFACEMETHODIMP OnFolderChanging(IFileDialog*, IShellItem*) override { return S_OK; }
    IFACEMETHODIMP OnFolderChange(IFileDialog*) override { return S_OK; }
    IFACEMETHODIMP OnSelectionChange(IFileDialog*) override { return S_OK; }
    IFACEMETHODIMP OnShareViolation(IFileDialog*, IShellItem*, FDE_SHAREVIOLATION_RESPONSE*) override { return E_NOTIMPL; }
    IFACEMETHODIMP OnTypeChange(IFileDialog* dialog) override {
        if (!state_) return S_OK;
        ComPtr<IFileSaveDialog> saveDialog;
        ComPtr<IFileDialogCustomize> customize;
        if (FAILED(dialog->QueryInterface(IID_PPV_ARGS(&saveDialog)))) return S_OK;
        if (FAILED(dialog->QueryInterface(IID_PPV_ARGS(&customize)))) return S_OK;
        UINT typeIndex = 0;
        if (FAILED(saveDialog->GetFileTypeIndex(&typeIndex))) return S_OK;
        SyncExportLightweightControl(customize.Get(), typeIndex, state_);
        return S_OK;
    }
    IFACEMETHODIMP OnOverwrite(IFileDialog*, IShellItem*, FDE_OVERWRITE_RESPONSE*) override { return E_NOTIMPL; }

private:
    LONG refCount_ = 1;
    ExportDialogState* state_ = nullptr;
};

std::optional<ExportDialogChoice> ExportFileDialog(HWND owner, const wchar_t* initialName) {
    const ExportSourceKind sourceKind = DetectExportSourceKind();
    UINT defaultTypeIndex = 1;
    const wchar_t* defaultExt = L"png";

    ComPtr<IFileSaveDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        switch (sourceKind) {
        case ExportSourceKind::Bmp: defaultExt = L"bmp"; break;
        case ExportSourceKind::Jpeg: defaultExt = L"jpg"; break;
        default: defaultExt = L"png"; break;
        }
        auto path = FileDialog(owner, true,
            L"PNG (*.png)\0*.png\0Bitmap (*.bmp)\0*.bmp\0JPEG (*.jpg;*.jpeg)\0*.jpg;*.jpeg\0\0",
            defaultExt, initialName);
        if (!path) return std::nullopt;
        const auto ext = std::filesystem::path(*path).extension().wstring();
        const bool lightweight = g.exportLightweight && _wcsicmp(ext.c_str(), L".bmp") != 0;
        return ExportDialogChoice{*path, lightweight};
    }

    static const COMDLG_FILTERSPEC types[] = {
        {L"PNG (*.png)", L"*.png"},
        {L"Bitmap (*.bmp)", L"*.bmp"},
        {L"JPEG (*.jpg;*.jpeg)", L"*.jpg;*.jpeg"},
    };
    dialog->SetFileTypes(static_cast<UINT>(std::size(types)), types);
    ConfigureExportFileTypes(dialog.Get(), sourceKind, defaultTypeIndex, defaultExt);
    if (initialName && initialName[0]) dialog->SetFileName(initialName);

    DWORD options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        dialog->SetOptions(options | FOS_OVERWRITEPROMPT | FOS_FORCEFILESYSTEM);
    }

    ExportDialogState dialogState{};
    dialogState.lightweight = g.exportLightweight;
    dialogState.allowLightweight = ExportTypeAllowsLightweight(defaultTypeIndex);

    ComPtr<IFileDialogCustomize> customize;
    if (SUCCEEDED(dialog.As(&customize))) {
        customize->AddCheckButton(IDC_EXPORT_LIGHTWEIGHT, L"ファイルサイズを抑える", FALSE);
        SyncExportLightweightControl(customize.Get(), defaultTypeIndex, &dialogState);
    }

    ExportDialogEvents* events = new (std::nothrow) ExportDialogEvents(&dialogState);
    ComPtr<IFileDialog> fileDialog;
    DWORD adviseCookie = 0;
    if (events && SUCCEEDED(dialog.As(&fileDialog))) fileDialog->Advise(events, &adviseCookie);

    const HRESULT showResult = dialog->Show(owner);

    if (fileDialog && adviseCookie) fileDialog->Unadvise(adviseCookie);
    if (events) events->Release();

    if (showResult == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return std::nullopt;
    if (FAILED(showResult)) return std::nullopt;

    const bool lightweight = dialogState.allowLightweight
        ? (dialogState.lightweightKnown ? dialogState.lightweight : g.exportLightweight)
        : false;
    if (dialogState.allowLightweight && dialogState.lightweightKnown) {
        g.exportLightweight = lightweight;
        SaveSettings();
    }

    ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(&item))) return std::nullopt;
    PWSTR path = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) return std::nullopt;
    ExportDialogChoice choice;
    choice.path = path;
    choice.lightweight = lightweight;
    CoTaskMemFree(path);
    return choice;
}

LRESULT CALLBACK PromptProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<PromptState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_CREATE) {
        state = static_cast<PromptState*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        CreateWindowW(L"STATIC", L"入力:", WS_CHILD | WS_VISIBLE, 12, 14, 60, 20, hwnd, nullptr, g.instance, nullptr);
        state->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", state->value.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 12, 36, 300, 24, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(1)), g.instance, nullptr);
        CreateWindowW(L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 152, 72, 76, 28, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)), g.instance, nullptr);
        CreateWindowW(L"BUTTON", L"キャンセル", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 236, 72, 76, 28, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)), g.instance, nullptr);
        SetFocus(state->edit);
        return 0;
    }
    if (message == WM_COMMAND && state) {
        if (LOWORD(wParam) == IDOK) {
            state->value = GetWindowString(state->edit);
            state->accepted = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (LOWORD(wParam) == IDCANCEL) { DestroyWindow(hwnd); return 0; }
    }
    if (message == WM_CLOSE) { DestroyWindow(hwnd); return 0; }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

std::optional<std::wstring> Prompt(HWND owner, const wchar_t* title, const wchar_t* initial) {
    PromptState state{title, initial};
    RECT ownerRect{}; GetWindowRect(owner, &ownerRect);
    HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME, kPromptClass, title, WS_POPUP | WS_CAPTION | WS_SYSMENU, ownerRect.left + 80, ownerRect.top + 80, 340, 140, owner, nullptr, g.instance, &state);
    if (!window) return std::nullopt;
    EnableWindow(owner, FALSE);
    ShowWindow(window, SW_SHOW);
    MSG message{};
    while (IsWindow(window) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    if (state.accepted) return state.value;
    return std::nullopt;
}

void UpdateWindowTitle() {
    std::wstring title = L"LiteDraw";
    if (!g.doc.originalName.empty()) title += L" - " + g.doc.originalName;
    SetWindowTextW(g.mainWindow, title.c_str());
}

// 埋め込みリソースの生バイト列を返す（メモリはモジュール寿命の間有効）
std::span<const uint8_t> ResourceBytes(int resourceId) {
    const HRSRC resource = FindResourceW(g.instance, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!resource) return {};
    const DWORD size = SizeofResource(g.instance, resource);
    const HGLOBAL handle = LoadResource(g.instance, resource);
    if (!handle || size == 0) return {};
    const void* data = LockResource(handle);
    if (!data) return {};
    return {static_cast<const uint8_t*>(data), size};
}

// --- UI（フォント・ツールバー・プロパティ帯） ---

// 埋め込みの Material Design Icons を GDI に登録し、ボタン用のフォントを作る
void LoadIconFont() {
    const auto bytes = ResourceBytes(IDR_FONT_MDI);
    if (bytes.empty()) return;
    DWORD installed = 0;
    g.iconFontResource = AddFontMemResourceEx(const_cast<uint8_t*>(bytes.data()),
                                              static_cast<DWORD>(bytes.size()), nullptr, &installed);
    if (!g.iconFontResource || installed == 0) return;
    auto makeFont = [](int pixelSize) {
        return CreateFontW(-pixelSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, kIconFontFamily);
    };
    g.ribbonIconFont = makeFont(kRibbonGlyphSize);
    g.stampIconFont = makeFont(kStampGlyphSize);
    g.iconFontLoaded = g.ribbonIconFont != nullptr && g.stampIconFont != nullptr;
}

void ReleaseIconFont() {
    if (g.ribbonIconFont) { DeleteObject(g.ribbonIconFont); g.ribbonIconFont = nullptr; }
    if (g.stampIconFont) { DeleteObject(g.stampIconFont); g.stampIconFont = nullptr; }
    if (g.iconFontResource) { RemoveFontMemResourceEx(g.iconFontResource); g.iconFontResource = nullptr; }
    g.iconFontLoaded = false;
}

// キャンバス描画用に、埋め込みフォントだけを含む DirectWrite フォントコレクションを作る
IDWriteFontCollection* IconFontCollection() {
    if (g.iconFontCollection) return g.iconFontCollection.Get();
    ComPtr<IDWriteFactory5> factory;
    if (FAILED(g.dwrite.As(&factory))) return nullptr;
    const auto bytes = ResourceBytes(IDR_FONT_MDI);
    if (bytes.empty()) return nullptr;
    ComPtr<IDWriteInMemoryFontFileLoader> loader;
    if (FAILED(factory->CreateInMemoryFontFileLoader(&loader))) return nullptr;
    if (FAILED(factory->RegisterFontFileLoader(loader.Get()))) return nullptr;
    ComPtr<IDWriteFontFile> file;
    if (FAILED(loader->CreateInMemoryFontFileReference(factory.Get(), bytes.data(),
            static_cast<UINT32>(bytes.size()), nullptr, &file))) return nullptr;
    ComPtr<IDWriteFontSetBuilder1> builder;
    if (FAILED(factory->CreateFontSetBuilder(&builder))) return nullptr;
    if (FAILED(builder->AddFontFile(file.Get()))) return nullptr;
    ComPtr<IDWriteFontSet> set;
    if (FAILED(builder->CreateFontSet(&set))) return nullptr;
    ComPtr<IDWriteFontCollection1> collection;
    if (FAILED(factory->CreateFontCollectionFromFontSet(set.Get(), &collection))) return nullptr;
    g.iconFontCollection = collection;
    return g.iconFontCollection.Get();
}

// ボタン面をグリフで描く。フォントが使えない場合は日本語ラベルにフォールバックする
void ApplyButtonGlyph(HWND button, const wchar_t* glyph, const wchar_t* label, HFONT glyphFont) {
    if (g.iconFontLoaded && glyphFont) {
        SetWindowTextW(button, glyph);
        SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(glyphFont), TRUE);
        return;
    }
    SetWindowTextW(button, label);
    SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(g.uiFont), TRUE);
}

void UpdateStampPickerSelection() {
    for (uint32_t i = 0; i < kStampCount; ++i) {
        if (!g.stampPickButtons[i]) continue;
        SendMessageW(g.stampPickButtons[i], BM_SETCHECK, i == g.selectedStampIndex ? BST_CHECKED : BST_UNCHECKED, 0);
    }
}

void UpdateToolbarSelection() {
    if (!g.mainWindow) return;
    for (const auto& entry : kToolbarButtons) {
        HWND button = GetDlgItem(g.mainWindow, entry.id);
        if (!button) continue;
        bool checked = false;
        if (entry.id == IDC_CMD_RESIZE) checked = g.showingResize;
        else if (entry.selectable) checked = entry.tool == g.tool && !g.showingResize;
        if (entry.selectable || entry.id == IDC_CMD_RESIZE) {
            SendMessageW(button, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
        }
    }
    InvalidateRect(g.mainWindow, nullptr, FALSE);
}

int ComputeToolbarWidth() {
    int width = 12;
    for (const auto& group : kRibbonGroups) {
        width += group.cols * kRibbonButton + (group.cols - 1) * kRibbonGap + kRibbonGroupGap + 1;
    }
    return width + 12;
}

void AddTooltip(HWND owner, HWND target, const wchar_t* text) {
    TOOLINFOW info{};
    info.cbSize = sizeof(info); info.uFlags = TTF_IDISHWND | TTF_SUBCLASS; info.hwnd = owner; info.uId = reinterpret_cast<UINT_PTR>(target);
    info.lpszText = const_cast<wchar_t*>(text);
    SendMessageW(g.tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
}

PropertyKind CurrentPropertyKind() {
    if (g.showingResize) return PropertyKind::Resize;
    if (g.selected >= 0 && g.selected < static_cast<int>(g.doc.objects.size())) {
        switch (g.doc.objects[g.selected].type) {
        case ObjectType::Text: return PropertyKind::Text;
        case ObjectType::NumberStamp: return PropertyKind::Number;
        case ObjectType::IconStamp: return PropertyKind::Stamp;
        case ObjectType::Arrow: return PropertyKind::Stroke;
        case ObjectType::Line: return PropertyKind::Line;
        case ObjectType::Rectangle:
        case ObjectType::Ellipse:
        case ObjectType::Freehand: return PropertyKind::Shape;
        case ObjectType::BlurRegion: return PropertyKind::Blur;
        case ObjectType::MosaicRegion: return PropertyKind::Mosaic;
        case ObjectType::Loupe: return PropertyKind::Loupe;
        case ObjectType::Callout: return PropertyKind::Callout;
        }
    }
    switch (g.tool) {
    case Tool::Text: return PropertyKind::Text;
    case Tool::Number: return PropertyKind::Number;
    case Tool::Stamp: return PropertyKind::Stamp;
    case Tool::Arrow: return PropertyKind::Stroke;
    case Tool::Line: return PropertyKind::Line;
    case Tool::Rectangle:
    case Tool::Ellipse:
    case Tool::Freehand: return PropertyKind::Shape;
    case Tool::Blur: return PropertyKind::Blur;
    case Tool::Mosaic: return PropertyKind::Mosaic;
    case Tool::Loupe: return PropertyKind::Loupe;
    case Tool::Callout: return PropertyKind::Callout;
    case Tool::Crop: return PropertyKind::Crop;
    default: return PropertyKind::None;
    }
}

void ShowPropertyControl(HWND hwnd, bool visible) {
    if (!hwnd) return;
    ShowWindow(hwnd, visible ? SW_SHOW : SW_HIDE);
    EnableWindow(hwnd, visible);
}

void ConfigureComboValues(HWND combo, std::span<const wchar_t* const> items) {
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (const auto* item : items) SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
}

void SetComboSelection(HWND combo, const wchar_t* text) {
    const int index = static_cast<int>(SendMessageW(combo, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(text)));
    if (index >= 0) SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(index), 0);
    else SetWindowTextW(combo, text);
}

std::wstring ComboSelectedText(HWND combo) {
    const int index = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    if (index < 0) return GetWindowString(combo);
    const int len = static_cast<int>(SendMessageW(combo, CB_GETLBTEXTLEN, static_cast<WPARAM>(index), 0));
    if (len <= 0) return GetWindowString(combo);
    std::wstring value(static_cast<size_t>(len), L'\0');
    SendMessageW(combo, CB_GETLBTEXT, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(value.data()));
    return value;
}

void SetTrackbar(HWND hwnd, int minValue, int maxValue, int value) {
    SendMessageW(hwnd, TBM_SETRANGEMIN, FALSE, minValue);
    SendMessageW(hwnd, TBM_SETRANGEMAX, TRUE, maxValue);
    SendMessageW(hwnd, TBM_SETPOS, TRUE, std::clamp(value, minValue, maxValue));
}

int TrackPos(HWND hwnd) { return static_cast<int>(SendMessageW(hwnd, TBM_GETPOS, 0, 0)); }

void HidePropertyExtras() {
    for (int i = 0; i < 3; ++i) {
        ShowPropertyControl(g.propLabel[i], false);
        ShowPropertyControl(g.propValue[i], false);
    }
    ShowPropertyControl(g.propWidthLabel, false);
    ShowPropertyControl(g.propWidth, false);
    ShowPropertyControl(g.propOutline, false);
    ShowPropertyControl(g.propOutlineWidthLabel, false);
    ShowPropertyControl(g.propOutlineWidth, false);
    ShowPropertyControl(g.propOpacityLabel, false);
    ShowPropertyControl(g.propOpacity, false);
    ShowPropertyControl(g.propDiameterLabel, false);
    ShowPropertyControl(g.propDiameter, false);
    ShowPropertyControl(g.propFill, false);
    ShowPropertyControl(g.propSizeInfo, false);
    ShowPropertyControl(g.propApply, false);
    for (uint32_t i = 0; i < kStampCount; ++i) ShowPropertyControl(g.stampPickButtons[i], false);
}

void RefreshColorBoxes() {
    if (g.color1Box) InvalidateRect(g.color1Box, nullptr, TRUE);
    if (g.color2Box) InvalidateRect(g.color2Box, nullptr, TRUE);
}

void ApplyStyle(Object& object) {
    object.color = g.color1;
    object.fillColor = g.color2;
    object.hasOutline = g.outlineEnabled;
    object.outlineWidth = g.outlineWidth;
    object.outlineOpacity = std::clamp(g.outlineOpacity, 0.10f, 1.0f);
    object.dashStyle = g.dashStyle;
}

void SyncGlobalsFromObject(const Object& object) {
    g.color1 = object.color;
    g.color2 = object.fillColor;
    g.lineWidth = object.width;
    g.outlineEnabled = object.hasOutline;
    g.outlineWidth = object.outlineWidth;
    g.outlineOpacity = object.outlineOpacity;
    g.dashStyle = object.dashStyle;
    g.arrowFlip = false;
    switch (object.type) {
    case ObjectType::Arrow:
        g.arrowFlip = object.fill;
        break;
    case ObjectType::Text:
    case ObjectType::Callout:
        g.textFont = object.font;
        g.textSize = object.extra;
        if (object.type == ObjectType::Callout) g.fillEnabled = object.fill;
        break;
    case ObjectType::NumberStamp:
        g.numberDiameter = static_cast<uint32_t>(object.extra);
        g.fillEnabled = object.fill;
        break;
    case ObjectType::IconStamp:
        g.stampSize = object.extra;
        g.selectedStampIndex = std::min(object.blockSize, static_cast<uint32_t>(kStampCount - 1));
        break;
    case ObjectType::BlurRegion:
        g.effectSize = static_cast<uint32_t>(object.extra);
        g.effectCircle = object.fill;
        break;
    case ObjectType::MosaicRegion:
        g.effectSize = object.blockSize;
        g.effectCircle = object.fill;
        break;
    case ObjectType::Loupe:
        g.loupeZoom = object.extra;
        g.loupeCircle = object.fill;
        break;
    default:
        break;
    }
}

void UpdateCropSizeInfo() {
    if (!g.propSizeInfo) return;
    wchar_t sizeText[160]{};
    if (g.doc.width == 0 || g.doc.height == 0) {
        SetWindowTextW(g.propSizeInfo, L"現在の画像サイズ: (未読込)");
        return;
    }
    if (g.cropActive) {
        const auto r = NormalizeRect(g.cropRect);
        const int w = (std::max)(0, static_cast<int>(r.right - r.left + 0.5f));
        const int h = (std::max)(0, static_cast<int>(r.bottom - r.top + 0.5f));
        swprintf_s(sizeText, L"現在: %u x %u px    選択: %d x %d px", g.doc.width, g.doc.height, w, h);
    } else {
        swprintf_s(sizeText, L"現在の画像サイズ: %u x %u px", g.doc.width, g.doc.height);
    }
    SetWindowTextW(g.propSizeInfo, sizeText);
}

void UpdateResizeSizeInfo() {
    if (!g.propSizeInfo) return;
    wchar_t sizeText[192]{};
    if (g.doc.width == 0 || g.doc.height == 0) {
        SetWindowTextW(g.propSizeInfo, L"現在の画像サイズ: (未読込)");
        return;
    }
    if (g.pendingResizeScale > 0.0f) {
        const UINT w = std::max(1U, static_cast<UINT>(std::lround(static_cast<double>(g.doc.width) * g.pendingResizeScale)));
        const UINT h = std::max(1U, static_cast<UINT>(std::lround(static_cast<double>(g.doc.height) * g.pendingResizeScale)));
        swprintf_s(sizeText, L"現在: %u x %u px    変更後: %u x %u px", g.doc.width, g.doc.height, w, h);
    } else {
        swprintf_s(sizeText, L"現在: %u x %u px", g.doc.width, g.doc.height);
    }
    SetWindowTextW(g.propSizeInfo, sizeText);
}

void ApplySelectionToProperties() {
    static constexpr const wchar_t* kFonts[] = {L"Segoe UI", L"Yu Gothic UI", L"Meiryo", L"Arial"};
    static constexpr const wchar_t* kLoupeZooms[] = {L"1.5x", L"2x", L"3x"};
    static constexpr const wchar_t* kLoupeShapes[] = {L"丸", L"四角"};
    static constexpr const wchar_t* kEffectShapes[] = {L"丸", L"四角"};
    static constexpr const wchar_t* kResizeScales[] = {L"75%", L"50%", L"25%"};
    static constexpr const wchar_t* kLineStyles[] = {L"実線", L"破線", L"一点鎖線", L"二点鎖線"};
    g.updatingUi = true;
    if (g.selected >= 0 && g.selected < static_cast<int>(g.doc.objects.size())) {
        SyncGlobalsFromObject(g.doc.objects[g.selected]);
    }
    HidePropertyExtras();
    const auto kind = CurrentPropertyKind();
    switch (kind) {
    case PropertyKind::Text:
        ConfigureComboValues(g.propValue[0], kFonts);
        SetComboSelection(g.propValue[0], g.textFont.c_str());
        ShowPropertyControl(g.propValue[0], true);
        SetWindowTextW(g.propDiameterLabel, L"サイズ");
        SetTrackbar(g.propDiameter, 8, 144, static_cast<int>(g.textSize));
        ShowPropertyControl(g.propDiameterLabel, true); ShowPropertyControl(g.propDiameter, true);
        SetWindowTextW(g.propWidthLabel, L"線幅");
        SetTrackbar(g.propWidth, 1, 32, static_cast<int>(g.outlineWidth));
        ShowPropertyControl(g.propWidthLabel, true); ShowPropertyControl(g.propWidth, true);
        SetWindowTextW(g.propOutline, L"周囲色");
        SendMessageW(g.propOutline, BM_SETCHECK, g.outlineEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
        ShowPropertyControl(g.propOutline, true);
        SetWindowTextW(g.propOpacityLabel, L"透明度");
        SetTrackbar(g.propOpacity, 10, 100, static_cast<int>(g.outlineOpacity * 100.0f + 0.5f));
        ShowPropertyControl(g.propOpacityLabel, true); ShowPropertyControl(g.propOpacity, true);
        break;
    case PropertyKind::Callout:
        ConfigureComboValues(g.propValue[0], kFonts);
        SetComboSelection(g.propValue[0], g.textFont.c_str());
        ShowPropertyControl(g.propValue[0], true);
        SetWindowTextW(g.propDiameterLabel, L"サイズ");
        SetTrackbar(g.propDiameter, 8, 144, static_cast<int>(g.textSize));
        ShowPropertyControl(g.propDiameterLabel, true); ShowPropertyControl(g.propDiameter, true);
        SetWindowTextW(g.propWidthLabel, L"線幅");
        SetTrackbar(g.propWidth, 1, 32, static_cast<int>(g.lineWidth));
        ShowPropertyControl(g.propWidthLabel, true); ShowPropertyControl(g.propWidth, true);
        SetWindowTextW(g.propFill, L"背景色");
        SendMessageW(g.propFill, BM_SETCHECK, g.fillEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
        ShowPropertyControl(g.propFill, true);
        SetWindowTextW(g.propOpacityLabel, L"透明度");
        SetTrackbar(g.propOpacity, 10, 100, static_cast<int>(g.outlineOpacity * 100.0f + 0.5f));
        ShowPropertyControl(g.propOpacityLabel, true); ShowPropertyControl(g.propOpacity, true);
        break;
    case PropertyKind::Number:
        SetWindowTextW(g.propDiameterLabel, L"直径");
        SetTrackbar(g.propDiameter, 18, 256, static_cast<int>(g.numberDiameter));
        ShowPropertyControl(g.propDiameterLabel, true); ShowPropertyControl(g.propDiameter, true);
        SetWindowTextW(g.propFill, L"文字色");
        SendMessageW(g.propFill, BM_SETCHECK, g.fillEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
        ShowPropertyControl(g.propFill, true);
        break;
    case PropertyKind::Stamp:
        for (uint32_t i = 0; i < kStampCount; ++i) ShowPropertyControl(g.stampPickButtons[i], true);
        UpdateStampPickerSelection();
        break;
    case PropertyKind::Stroke:
        SetWindowTextW(g.propFill, L"向きを反転");
        SendMessageW(g.propFill, BM_SETCHECK, g.arrowFlip ? BST_CHECKED : BST_UNCHECKED, 0);
        ShowPropertyControl(g.propFill, true);
        SetWindowTextW(g.propWidthLabel, L"線幅");
        SetTrackbar(g.propWidth, 1, 32, static_cast<int>(g.lineWidth));
        ShowPropertyControl(g.propWidthLabel, true); ShowPropertyControl(g.propWidth, true);
        SetWindowTextW(g.propOutline, L"周囲色");
        SendMessageW(g.propOutline, BM_SETCHECK, g.outlineEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
        ShowPropertyControl(g.propOutline, true);
        SetWindowTextW(g.propOutlineWidthLabel, L"縁取り");
        SetTrackbar(g.propOutlineWidth, 1, 32, static_cast<int>(g.outlineWidth));
        ShowPropertyControl(g.propOutlineWidthLabel, true); ShowPropertyControl(g.propOutlineWidth, true);
        SetWindowTextW(g.propOpacityLabel, L"透明度");
        SetTrackbar(g.propOpacity, 10, 100, static_cast<int>(g.outlineOpacity * 100.0f + 0.5f));
        ShowPropertyControl(g.propOpacityLabel, true); ShowPropertyControl(g.propOpacity, true);
        break;
    case PropertyKind::Shape:
        SetWindowTextW(g.propWidthLabel, L"線幅");
        SetTrackbar(g.propWidth, 1, 32, static_cast<int>(g.lineWidth));
        ShowPropertyControl(g.propWidthLabel, true); ShowPropertyControl(g.propWidth, true);
        SetWindowTextW(g.propFill, L"破線");
        SendMessageW(g.propFill, BM_SETCHECK, g.dashStyle != 0 ? BST_CHECKED : BST_UNCHECKED, 0);
        ShowPropertyControl(g.propFill, true);
        SetWindowTextW(g.propOutline, L"周囲色");
        SendMessageW(g.propOutline, BM_SETCHECK, g.outlineEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
        ShowPropertyControl(g.propOutline, true);
        SetWindowTextW(g.propOutlineWidthLabel, L"縁取り");
        SetTrackbar(g.propOutlineWidth, 1, 32, static_cast<int>(g.outlineWidth));
        ShowPropertyControl(g.propOutlineWidthLabel, true); ShowPropertyControl(g.propOutlineWidth, true);
        SetWindowTextW(g.propOpacityLabel, L"透明度");
        SetTrackbar(g.propOpacity, 10, 100, static_cast<int>(g.outlineOpacity * 100.0f + 0.5f));
        ShowPropertyControl(g.propOpacityLabel, true); ShowPropertyControl(g.propOpacity, true);
        break;
    case PropertyKind::Line: {
        const wchar_t* styleName = g.dashStyle == 1 ? L"破線" : g.dashStyle == 2 ? L"一点鎖線" : g.dashStyle == 3 ? L"二点鎖線" : L"実線";
        ConfigureComboValues(g.propValue[0], kLineStyles);
        SetWindowTextW(g.propLabel[0], L"線種");
        SetComboSelection(g.propValue[0], styleName);
        ShowPropertyControl(g.propLabel[0], true); ShowPropertyControl(g.propValue[0], true);
        SetWindowTextW(g.propWidthLabel, L"線幅");
        SetTrackbar(g.propWidth, 1, 32, static_cast<int>(g.lineWidth));
        ShowPropertyControl(g.propWidthLabel, true); ShowPropertyControl(g.propWidth, true);
        SetWindowTextW(g.propOutline, L"周囲色");
        SendMessageW(g.propOutline, BM_SETCHECK, g.outlineEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
        ShowPropertyControl(g.propOutline, true);
        SetWindowTextW(g.propOutlineWidthLabel, L"縁取り");
        SetTrackbar(g.propOutlineWidth, 1, 32, static_cast<int>(g.outlineWidth));
        ShowPropertyControl(g.propOutlineWidthLabel, true); ShowPropertyControl(g.propOutlineWidth, true);
        SetWindowTextW(g.propOpacityLabel, L"透明度");
        SetTrackbar(g.propOpacity, 10, 100, static_cast<int>(g.outlineOpacity * 100.0f + 0.5f));
        ShowPropertyControl(g.propOpacityLabel, true); ShowPropertyControl(g.propOpacity, true);
        break;
    }
    case PropertyKind::Blur:
        ConfigureComboValues(g.propValue[0], kEffectShapes);
        SetWindowTextW(g.propLabel[0], L"形状"); SetComboSelection(g.propValue[0], g.effectCircle ? L"丸" : L"四角");
        ShowPropertyControl(g.propLabel[0], true); ShowPropertyControl(g.propValue[0], true);
        SetWindowTextW(g.propWidthLabel, L"ぼかし");
        SetTrackbar(g.propWidth, 2, 128, static_cast<int>(g.effectSize));
        ShowPropertyControl(g.propWidthLabel, true); ShowPropertyControl(g.propWidth, true);
        break;
    case PropertyKind::Mosaic:
        ConfigureComboValues(g.propValue[0], kEffectShapes);
        SetWindowTextW(g.propLabel[0], L"形状"); SetComboSelection(g.propValue[0], g.effectCircle ? L"丸" : L"四角");
        ShowPropertyControl(g.propLabel[0], true); ShowPropertyControl(g.propValue[0], true);
        SetWindowTextW(g.propWidthLabel, L"ブロック");
        SetTrackbar(g.propWidth, 2, 128, static_cast<int>(g.effectSize));
        ShowPropertyControl(g.propWidthLabel, true); ShowPropertyControl(g.propWidth, true);
        break;
    case PropertyKind::Loupe:
        ConfigureComboValues(g.propValue[0], kLoupeShapes);
        ConfigureComboValues(g.propValue[1], kLoupeZooms);
        SetWindowTextW(g.propLabel[0], L"形状"); SetComboSelection(g.propValue[0], g.loupeCircle ? L"丸" : L"四角");
        SetWindowTextW(g.propLabel[1], L"倍率"); SetComboSelection(g.propValue[1], g.loupeZoom == 1.5f ? L"1.5x" : g.loupeZoom == 3.0f ? L"3x" : L"2x");
        ShowPropertyControl(g.propLabel[0], true); ShowPropertyControl(g.propValue[0], true);
        ShowPropertyControl(g.propLabel[1], true); ShowPropertyControl(g.propValue[1], true);
        SetWindowTextW(g.propWidthLabel, L"線幅");
        SetTrackbar(g.propWidth, 1, 32, static_cast<int>(g.lineWidth));
        ShowPropertyControl(g.propWidthLabel, true); ShowPropertyControl(g.propWidth, true);
        break;
    case PropertyKind::Resize:
        ConfigureComboValues(g.propValue[0], kResizeScales);
        SetWindowTextW(g.propLabel[0], L"縮小率");
        if (g.pendingResizeScale == 0.75f) SetComboSelection(g.propValue[0], L"75%");
        else if (g.pendingResizeScale == 0.50f) SetComboSelection(g.propValue[0], L"50%");
        else if (g.pendingResizeScale == 0.25f) SetComboSelection(g.propValue[0], L"25%");
        else SetWindowTextW(g.propValue[0], L"");
        ShowPropertyControl(g.propLabel[0], true); ShowPropertyControl(g.propValue[0], true);
        ShowPropertyControl(g.propApply, true);
        EnableWindow(g.propApply, g.pendingResizeScale > 0.0f);
        UpdateResizeSizeInfo();
        ShowPropertyControl(g.propSizeInfo, true);
        break;
    case PropertyKind::Crop:
        ShowPropertyControl(g.propApply, true);
        EnableWindow(g.propApply, TRUE);
        UpdateCropSizeInfo();
        ShowPropertyControl(g.propSizeInfo, true);
        break;
    case PropertyKind::None:
        break;
    }
    const bool showColors = kind != PropertyKind::Crop && kind != PropertyKind::Resize;
    ShowPropertyControl(g.color1Label, showColors);
    ShowPropertyControl(g.color1Box, showColors);
    ShowPropertyControl(g.color2Label, showColors);
    ShowPropertyControl(g.color2Box, showColors);
    RefreshColorBoxes();
    g.updatingUi = false;
    if (g.mainWindow) Layout(g.mainWindow);
}

void CommitObjectPropertyChange(bool recordHistory) {
    if (g.updatingUi) return;
    if (g.selected < 0 || g.selected >= static_cast<int>(g.doc.objects.size())) return;
    auto& object = g.doc.objects[g.selected];
    switch (object.type) {
    case ObjectType::Text:
        object.font = g.textFont; object.extra = g.textSize;
        ApplyStyle(object);
        object.outlineWidth = g.outlineWidth;
        object.rect.right = object.rect.left + std::max(120.0f, object.extra * static_cast<float>(std::max<size_t>(object.text.size(), 6)));
        object.rect.bottom = object.rect.top + object.extra * 1.5f;
        break;
    case ObjectType::Callout:
        object.font = g.textFont; object.extra = g.textSize;
        object.color = g.color1; object.fillColor = g.color2;
        object.width = g.lineWidth; object.fill = g.fillEnabled;
        object.hasOutline = false;
        object.outlineOpacity = std::clamp(g.outlineOpacity, 0.10f, 1.0f);
        break;
    case ObjectType::NumberStamp: {
        object.extra = static_cast<float>(g.numberDiameter); object.color = g.color1; object.fillColor = g.color2; object.fill = g.fillEnabled;
        const float cx = (object.rect.left + object.rect.right) * 0.5f, cy = (object.rect.top + object.rect.bottom) * 0.5f;
        const float r = object.extra * 0.5f;
        object.rect = D2D1::RectF(cx - r, cy - r, cx + r, cy + r);
        break;
    }
    case ObjectType::IconStamp:
        // 色のみ更新。アイコン種別はスタンプピッカー、サイズはリサイズ操作でそれぞれ管理する
        object.color = g.color1;
        break;
    case ObjectType::Arrow:
        object.width = g.lineWidth; ApplyStyle(object); object.fill = g.arrowFlip; break;
    case ObjectType::Line:
    case ObjectType::Rectangle:
    case ObjectType::Ellipse:
    case ObjectType::Freehand:
        object.width = g.lineWidth; ApplyStyle(object); break;
    case ObjectType::BlurRegion:
        object.extra = static_cast<float>(g.effectSize); object.fill = g.effectCircle; object.color = g.color1; break;
    case ObjectType::MosaicRegion:
        object.blockSize = g.effectSize; object.fill = g.effectCircle; object.color = g.color1; break;
    case ObjectType::Loupe:
        object.extra = g.loupeZoom; object.fill = g.loupeCircle; object.color = g.color1; object.width = g.lineWidth;
        if (object.fill) SquareLoupeRect(object.rect);
        break;
    }
    if (recordHistory) PushHistory();
    InvalidateCanvas();
}

void ReadPropertyControls(bool recordHistory) {
    if (g.updatingUi) return;
    switch (CurrentPropertyKind()) {
    case PropertyKind::Text:
        g.textFont = ComboSelectedText(g.propValue[0]);
        if (g.textFont.empty()) g.textFont = GetWindowString(g.propValue[0]);
        g.textSize = std::clamp(static_cast<float>(TrackPos(g.propDiameter)), 8.0f, 144.0f);
        g.outlineWidth = static_cast<float>(TrackPos(g.propWidth));
        g.outlineEnabled = SendMessageW(g.propOutline, BM_GETCHECK, 0, 0) == BST_CHECKED;
        g.outlineOpacity = static_cast<float>(TrackPos(g.propOpacity)) / 100.0f;
        break;
    case PropertyKind::Callout:
        g.textFont = ComboSelectedText(g.propValue[0]);
        if (g.textFont.empty()) g.textFont = GetWindowString(g.propValue[0]);
        g.textSize = std::clamp(static_cast<float>(TrackPos(g.propDiameter)), 8.0f, 144.0f);
        g.lineWidth = static_cast<float>(TrackPos(g.propWidth));
        g.fillEnabled = SendMessageW(g.propFill, BM_GETCHECK, 0, 0) == BST_CHECKED;
        g.outlineOpacity = static_cast<float>(TrackPos(g.propOpacity)) / 100.0f;
        break;
    case PropertyKind::Number:
        g.numberDiameter = static_cast<uint32_t>(TrackPos(g.propDiameter));
        g.fillEnabled = SendMessageW(g.propFill, BM_GETCHECK, 0, 0) == BST_CHECKED;
        break;
    case PropertyKind::Stamp:
        break;
    case PropertyKind::Stroke:
        g.arrowFlip = SendMessageW(g.propFill, BM_GETCHECK, 0, 0) == BST_CHECKED;
        g.lineWidth = static_cast<float>(TrackPos(g.propWidth));
        g.outlineEnabled = SendMessageW(g.propOutline, BM_GETCHECK, 0, 0) == BST_CHECKED;
        g.outlineWidth = static_cast<float>(TrackPos(g.propOutlineWidth));
        g.outlineOpacity = static_cast<float>(TrackPos(g.propOpacity)) / 100.0f;
        break;
    case PropertyKind::Shape:
        g.lineWidth = static_cast<float>(TrackPos(g.propWidth));
        g.dashStyle = SendMessageW(g.propFill, BM_GETCHECK, 0, 0) == BST_CHECKED ? 1 : 0;
        g.outlineEnabled = SendMessageW(g.propOutline, BM_GETCHECK, 0, 0) == BST_CHECKED;
        g.outlineWidth = static_cast<float>(TrackPos(g.propOutlineWidth));
        g.outlineOpacity = static_cast<float>(TrackPos(g.propOpacity)) / 100.0f;
        break;
    case PropertyKind::Line: {
        const auto selected = ComboSelectedText(g.propValue[0]);
        const auto value = selected.empty() ? GetWindowString(g.propValue[0]) : selected;
        g.dashStyle = value == L"破線" ? 1 : value == L"一点鎖線" ? 2 : value == L"二点鎖線" ? 3 : 0;
        g.lineWidth = static_cast<float>(TrackPos(g.propWidth));
        g.outlineEnabled = SendMessageW(g.propOutline, BM_GETCHECK, 0, 0) == BST_CHECKED;
        g.outlineWidth = static_cast<float>(TrackPos(g.propOutlineWidth));
        g.outlineOpacity = static_cast<float>(TrackPos(g.propOpacity)) / 100.0f;
        break;
    }
    case PropertyKind::Blur:
    case PropertyKind::Mosaic:
        g.effectCircle = ComboSelectedText(g.propValue[0]) != L"四角";
        g.effectSize = static_cast<uint32_t>(TrackPos(g.propWidth));
        break;
    case PropertyKind::Loupe: {
        g.loupeCircle = ComboSelectedText(g.propValue[0]) != L"四角";
        const auto zoom = ComboSelectedText(g.propValue[1]);
        g.loupeZoom = zoom == L"1.5x" ? 1.5f : zoom == L"3x" ? 3.0f : 2.0f;
        g.lineWidth = static_cast<float>(TrackPos(g.propWidth));
        break;
    }
    case PropertyKind::Resize: {
        const auto selected = ComboSelectedText(g.propValue[0]);
        const auto value = selected.empty() ? GetWindowString(g.propValue[0]) : selected;
        if (value == L"75%") g.pendingResizeScale = 0.75f;
        else if (value == L"50%") g.pendingResizeScale = 0.50f;
        else if (value == L"25%") g.pendingResizeScale = 0.25f;
        else g.pendingResizeScale = 0.0f;
        UpdateResizeSizeInfo();
        if (g.propApply) EnableWindow(g.propApply, g.pendingResizeScale > 0.0f);
        return;
    }
    case PropertyKind::None:
        return;
    }
    CommitObjectPropertyChange(recordHistory);
}

Object NewObject(D2D1_POINT_2F p) {
    Object object;
    object.id = g.doc.nextId++;
    object.z = static_cast<int32_t>(g.doc.objects.size());
    object.width = g.lineWidth;
    ApplyStyle(object);
    object.rect = D2D1::RectF(p.x, p.y, p.x, p.y);
    switch (g.tool) {
    case Tool::Line: object.type = ObjectType::Line; object.points = {p, p}; break;
    case Tool::Arrow: object.type = ObjectType::Arrow; object.points = {p, p}; object.extra = 18.0f; object.dashStyle = 0; object.fill = g.arrowFlip; break;
    case Tool::Rectangle: object.type = ObjectType::Rectangle; break;
    case Tool::Ellipse: object.type = ObjectType::Ellipse; break;
    case Tool::Freehand: object.type = ObjectType::Freehand; object.points = {p}; break;
    case Tool::Blur: object.type = ObjectType::BlurRegion; object.extra = static_cast<float>(g.effectSize); object.fill = g.effectCircle; break;
    case Tool::Mosaic: object.type = ObjectType::MosaicRegion; object.blockSize = g.effectSize; object.fill = g.effectCircle; break;
    case Tool::Callout:
        object.type = ObjectType::Callout;
        object.font = g.textFont;
        object.extra = g.textSize;
        object.fill = g.fillEnabled;
        object.hasOutline = false;
        object.outlineOpacity = std::clamp(g.outlineOpacity, 0.10f, 1.0f);
        break;
    case Tool::Loupe: {
        object.type = ObjectType::Loupe;
        object.extra = g.loupeZoom;
        object.fill = g.loupeCircle;
        object.hasOutline = false;
        const float size = 72.0f;
        object.rect = D2D1::RectF(p.x - size * 0.5f, p.y - size * 0.5f, p.x + size * 0.5f, p.y + size * 0.5f);
        PlaceLoupePreview(object);
        break;
    }
    default: break;
    }
    return object;
}

void UpdateDraft(D2D1_POINT_2F p) {
    if (!g.draft) return;
    if (g.draft->type == ObjectType::Arrow || g.draft->type == ObjectType::Line) g.draft->points[0] = p;
    else if (g.draft->type == ObjectType::Freehand) g.draft->points.push_back(p);
    else if (g.draft->type == ObjectType::Loupe) {
        g.draft->rect.right = p.x;
        g.draft->rect.bottom = p.y;
        if (g.draft->fill) SquareLoupeRect(g.draft->rect);
        PlaceLoupePreview(*g.draft);
    } else { g.draft->rect.right = p.x; g.draft->rect.bottom = p.y; }
}

void PlaceStampAt(D2D1_POINT_2F p) {
    Object object;
    object.id = g.doc.nextId++;
    object.z = static_cast<int32_t>(g.doc.objects.size());
    object.type = ObjectType::IconStamp;
    object.extra = g.stampSize;
    object.blockSize = g.selectedStampIndex;
    object.color = g.color1;
    const float half = object.extra * 0.5f;
    object.rect = D2D1::RectF(p.x - half, p.y - half, p.x + half, p.y + half);
    g.doc.objects.push_back(std::move(object));
    g.selected = static_cast<int>(g.doc.objects.size()) - 1;
    PushHistory();
    ApplySelectionToProperties();
    InvalidateCanvas();
}

void AddPointObject(D2D1_POINT_2F p, ObjectType type, std::wstring text, bool beginEdit) {
    Object object;
    object.id = g.doc.nextId++;
    object.z = static_cast<int32_t>(g.doc.objects.size());
    object.type = type;
    ApplyStyle(object);
    object.text = std::move(text);
    if (type == ObjectType::Text) {
        object.font = g.textFont;
        object.extra = g.textSize;
        object.rect = D2D1::RectF(p.x, p.y, p.x + std::max(120.0f, object.extra * 8.0f), p.y + object.extra * 1.5f);
    } else {
        object.extra = static_cast<float>(g.numberDiameter);
        object.fill = g.fillEnabled;
        const float r = object.extra * 0.5f;
        object.rect = D2D1::RectF(p.x - r, p.y - r, p.x + r, p.y + r);
    }
    g.doc.objects.push_back(std::move(object));
    g.selected = static_cast<int>(g.doc.objects.size()) - 1;
    g.textEditing = beginEdit;
    g.caretVisible = true;
    if (beginEdit && g.canvas) SetTimer(g.canvas, 1, 500, nullptr);
    if (!beginEdit) PushHistory();
    ApplySelectionToProperties();
    InvalidateCanvas();
}

void CommitInlineEdit(bool keepEmpty) {
    if (!g.textEditing) return;
    g.textEditing = false;
    g.caretVisible = false;
    if (g.canvas) KillTimer(g.canvas, 1);
    if (g.selected >= 0 && g.selected < static_cast<int>(g.doc.objects.size())) {
        auto& object = g.doc.objects[g.selected];
        if (!keepEmpty && object.text.empty() && object.type != ObjectType::Callout) {
            g.doc.objects.erase(g.doc.objects.begin() + g.selected);
            g.selected = -1;
        } else {
            if (object.type == ObjectType::Callout) ExpandCalloutForText(object);
            PushHistory();
        }
    }
    ApplySelectionToProperties();
    InvalidateCanvas();
}

void BeginInlineEdit(D2D1_POINT_2F p, ObjectType type) {
    CommitInlineEdit(false);
    AddPointObject(p, type, L"", true);
    SetFocus(g.canvas);
}

void DeleteSelected() {
    g.textEditing = false;
    g.caretVisible = false;
    if (g.canvas) KillTimer(g.canvas, 1);
    if (g.selected < 0 || g.selected >= static_cast<int>(g.doc.objects.size())) {
        SetTool(Tool::Select);
        return;
    }
    g.doc.objects.erase(g.doc.objects.begin() + g.selected);
    g.selected = -1;
    PushHistory();
    SetTool(Tool::Select);
    InvalidateCanvas();
}

bool ConfirmDiscard() {
    if (!g.doc.dirty) return true;
    return MessageBoxW(g.mainWindow, L"未保存の変更があります。破棄しますか？", L"LiteDraw", MB_YESNO | MB_ICONWARNING) == IDYES;
}

bool OpenDocumentFromPath(const std::wstring& path) {
    const bool ldl = _wcsicmp(std::filesystem::path(path).extension().c_str(), L".ldl") == 0;
    if (ldl) {
        if (!LoadLdl(path)) { ShowError(g.mainWindow, L"ファイルを開けませんでした。"); return false; }
    } else {
        const HRESULT hr = LoadImageFile(path);
        if (FAILED(hr)) {
            if (hr == HRESULT_FROM_WIN32(ERROR_UNSUPPORTED_TYPE)) ShowUnsupportedFormat();
            else ShowError(g.mainWindow, L"ファイルを開けませんでした。");
            return false;
        }
    }
    UpdateWindowTitle();
    FitOrigin();
    InvalidateCanvas();
    return true;
}

void HandleDropFiles(HDROP drop) {
    wchar_t path[MAX_PATH]{};
    if (DragQueryFileW(drop, 0, path, MAX_PATH) > 0) {
        if (ConfirmDiscard()) OpenDocumentFromPath(path);
    }
    DragFinish(drop);
}

void DoOpen() {
    if (!ConfirmDiscard()) return;
    auto path = FileDialog(g.mainWindow, false,
        L"Supported (*.png;*.bmp;*.jpg;*.jpeg;*.ldl)\0*.png;*.bmp;*.jpg;*.jpeg;*.ldl\0"
        L"Image (*.png;*.bmp;*.jpg;*.jpeg)\0*.png;*.bmp;*.jpg;*.jpeg\0LiteDraw (*.ldl)\0*.ldl\0\0", nullptr);
    if (path) OpenDocumentFromPath(*path);
}

bool DoOverwriteSave() {
    if (g.doc.pixels.empty()) { ShowError(g.mainWindow, L"先に画像を開いてください。"); return false; }
    if (g.doc.sourcePath.empty()) {
        ShowError(g.mainWindow, L"上書きする元画像がありません。画像ファイルを開いてから実行してください。");
        return false;
    }
    if (MessageBoxW(g.mainWindow, L"上書き保存しますか？", L"LiteDraw", MB_YESNO | MB_ICONQUESTION) != IDYES) return false;
    if (!ExportImage(g.doc.sourcePath)) { ShowError(g.mainWindow, L"上書き保存できませんでした。"); return false; }
    g.doc.dirty = false;
    UpdateWindowTitle();
    return true;
}

bool DoSave(bool saveAs) {
    if (g.doc.pixels.empty()) { ShowError(g.mainWindow, L"先に画像を開いてください。"); return false; }
    std::wstring path = g.doc.projectPath;
    if (saveAs || path.empty()) {
        // 初期ファイル名: 元データのファイル名（拡張子なし）
        std::wstring initName = std::filesystem::path(g.doc.sourcePath).stem().wstring();
        auto selected = FileDialog(g.mainWindow, true, L"LiteDraw Layer (*.ldl)\0*.ldl\0\0", L"ldl", initName.empty() ? nullptr : initName.c_str());
        if (!selected) return false;
        path = *selected;
    }
    if (!SaveLdl(path)) { ShowError(g.mainWindow, L"保存できませんでした。"); return false; }
    UpdateWindowTitle();
    return true;
}

void DoExport() {
    if (g.doc.pixels.empty()) { ShowError(g.mainWindow, L"先に画像を開いてください。"); return; }
    // 初期ファイル名: 元データのファイル名（拡張子なし）+ "Edit"
    std::wstring initName = std::filesystem::path(g.doc.sourcePath).stem().wstring();
    if (!initName.empty()) initName += L"Edit";
    auto choice = ExportFileDialog(g.mainWindow, initName.empty() ? nullptr : initName.c_str());
    if (choice && !ExportImage(choice->path, choice->lightweight)) ShowError(g.mainWindow, L"画像を書き出せませんでした。");
}

void ChooseColor(int which) {
    static COLORREF custom[16]{};
    uint32_t& target = (which == 2) ? g.color2 : g.color1;
    CHOOSECOLORW picker{sizeof(picker)};
    picker.hwndOwner = g.mainWindow; picker.rgbResult = RgbaToColorRef(target);
    picker.lpCustColors = custom; picker.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (ChooseColorW(&picker)) {
        target = ColorRefToRgba(picker.rgbResult);
        SaveSettings();
        RefreshColorBoxes();
        CommitObjectPropertyChange(true);
    }
}

void FinishAwaitingCalloutTip(bool keep) {
    if (!g.awaitingCalloutTip) return;
    g.awaitingCalloutTip = false;
    if (!keep && g.selected >= 0 && g.selected < static_cast<int>(g.doc.objects.size()) &&
        g.doc.objects[g.selected].type == ObjectType::Callout) {
        g.doc.objects.erase(g.doc.objects.begin() + g.selected);
        g.selected = -1;
        InvalidateCanvas();
    } else if (keep) {
        PushHistory();
    }
}

void SetTool(Tool tool) {
    CommitInlineEdit(false);
    FinishAwaitingCalloutTip(true);
    g.showingResize = false;
    g.movingLoupePreview = false;
    const bool keepSelection = tool == Tool::Select ||
        (g.selected >= 0 && g.selected < static_cast<int>(g.doc.objects.size()) && CurrentPropertyKind() != PropertyKind::None &&
         ((tool == Tool::Loupe && g.doc.objects[g.selected].type == ObjectType::Loupe) ||
          (tool == Tool::Callout && g.doc.objects[g.selected].type == ObjectType::Callout) ||
          (tool == Tool::Text && g.doc.objects[g.selected].type == ObjectType::Text) ||
          (tool == Tool::Line && g.doc.objects[g.selected].type == ObjectType::Line) ||
          (tool == Tool::Arrow && g.doc.objects[g.selected].type == ObjectType::Arrow) ||
          (tool == Tool::Rectangle && g.doc.objects[g.selected].type == ObjectType::Rectangle) ||
          (tool == Tool::Ellipse && g.doc.objects[g.selected].type == ObjectType::Ellipse) ||
          (tool == Tool::Freehand && g.doc.objects[g.selected].type == ObjectType::Freehand) ||
          (tool == Tool::Blur && g.doc.objects[g.selected].type == ObjectType::BlurRegion) ||
          (tool == Tool::Mosaic && g.doc.objects[g.selected].type == ObjectType::MosaicRegion) ||
          (tool == Tool::Mosaic && g.doc.objects[g.selected].type == ObjectType::MosaicRegion) ||
          (tool == Tool::Number && g.doc.objects[g.selected].type == ObjectType::NumberStamp) ||
          (tool == Tool::Stamp && g.doc.objects[g.selected].type == ObjectType::IconStamp)));
    g.tool = tool;
    g.draft.reset();
    g.dragging = false;
    g.resizing = false;
    g.activeHandle = -1;
    if (!keepSelection) g.selected = -1;
    if (tool == Tool::Crop) {
        if (!g.cropActive && g.doc.width > 0 && g.doc.height > 0) {
            const float mx = static_cast<float>(g.doc.width) * 0.1f;
            const float my = static_cast<float>(g.doc.height) * 0.1f;
            g.cropRect = D2D1::RectF(mx, my, static_cast<float>(g.doc.width) - mx, static_cast<float>(g.doc.height) - my);
            g.cropActive = true;
        }
    } else {
        g.cropActive = false;
    }
    ApplySelectionToProperties();
    UpdateToolbarSelection();
    InvalidateCanvas();
}

struct CloseDialogState { int result = IDCANCEL; };
struct HelpDialogState { HWND terms{}; };

LRESULT CALLBACK CloseProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<CloseDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_CREATE) {
        state = static_cast<CloseDialogState*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        CreateWindowW(L"STATIC", L"未保存の変更があります。", WS_CHILD | WS_VISIBLE, 16, 16, 360, 22, hwnd, nullptr, g.instance, nullptr);
        CreateWindowW(L"BUTTON", L"変更破棄して終了", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 16, 52, 160, 28, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CLOSE_DISCARD)), g.instance, nullptr);
        CreateWindowW(L"BUTTON", L"レイヤーを保存", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 184, 52, 140, 28, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CLOSE_SAVE_AS)), g.instance, nullptr);
        CreateWindowW(L"BUTTON", L"上書き保存", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 16, 88, 160, 28, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CLOSE_SAVE)), g.instance, nullptr);
        CreateWindowW(L"BUTTON", L"キャンセル", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 184, 88, 140, 28, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CLOSE_CANCEL)), g.instance, nullptr);
        for (HWND child = GetWindow(hwnd, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) ApplyUiFont(child);
        return 0;
    }
    if (message == WM_COMMAND && state) {
        const int id = LOWORD(wParam);
        if (id == IDC_CLOSE_DISCARD || id == IDC_CLOSE_SAVE_AS || id == IDC_CLOSE_SAVE || id == IDC_CLOSE_CANCEL) {
            state->result = id;
            DestroyWindow(hwnd);
            return 0;
        }
    }
    if (message == WM_CLOSE) { if (state) state->result = IDC_CLOSE_CANCEL; DestroyWindow(hwnd); return 0; }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

int PromptUnsavedClose() {
    CloseDialogState state;
    RECT ownerRect{}; GetWindowRect(g.mainWindow, &ownerRect);
    HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME, kCloseClass, L"LiteDraw", WS_POPUP | WS_CAPTION | WS_SYSMENU,
        ownerRect.left + 80, ownerRect.top + 80, 360, 170, g.mainWindow, nullptr, g.instance, &state);
    if (!window) return IDC_CLOSE_CANCEL;
    EnableWindow(g.mainWindow, FALSE);
    ShowWindow(window, SW_SHOW);
    MSG message{};
    while (IsWindow(window) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    EnableWindow(g.mainWindow, TRUE);
    SetForegroundWindow(g.mainWindow);
    return state.result;
}

LRESULT CALLBACK HelpProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<HelpDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_CREATE) {
        state = static_cast<HelpDialogState*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        wchar_t header[128]{};
        swprintf_s(header, L"LiteDraw  バージョン %s", kVersion);
        CreateWindowW(L"STATIC", header, WS_CHILD | WS_VISIBLE, 16, 12, 460, 22, hwnd, nullptr, g.instance, nullptr);
        CreateWindowW(L"STATIC", L"ツールバーアイコン: Material Design Icons 7.x (Pictogrammers / Apache License 2.0)",
            WS_CHILD | WS_VISIBLE, 16, 36, 540, 20, hwnd, nullptr, g.instance, nullptr);
        CreateWindowW(L"STATIC", L"GitHub:", WS_CHILD | WS_VISIBLE, 16, 60, 60, 20, hwnd, nullptr, g.instance, nullptr);
        HWND url = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", kGitHubUrl, WS_CHILD | WS_VISIBLE | ES_READONLY | ES_AUTOHSCROLL, 76, 56, 400, 24, hwnd, nullptr, g.instance, nullptr);
        SendMessageW(url, EM_SETREADONLY, TRUE, 0);
        CreateWindowW(L"STATIC", L"利用規約:", WS_CHILD | WS_VISIBLE, 16, 90, 80, 20, hwnd, nullptr, g.instance, nullptr);
        state->terms = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
            L"LiteDraw はフリーウェアです。\r\n"
            L"本ソフトウェアの開発には Cursor (AI) を使用しています。\r\n"
            L"本ソフトウェアは現状有姿 (AS IS) で提供され、明示または黙示を問わずいかなる保証もありません。\r\n"
            L"利用に伴う損害について、作者は責任を負いません。利用は自己責任でお願いします。\r\n"
            L"ソースコードと配布物は GitHub で公開しています。",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            16, 112, 540, 110, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDD_HELP_TEXT)), g.instance, nullptr);
        CreateWindowW(L"STATIC", L"スタンプアイコンのカスタマイズ (LiteDraw.ini):",
            WS_CHILD | WS_VISIBLE, 16, 230, 300, 20, hwnd, nullptr, g.instance, nullptr);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
            L"LiteDraw.ini の [Stamps] セクションに stamp01～stamp15 でアイコン名を指定します。\r\n"
            L"アイコン名は Material Design Icons 7.x のアイコン名（英数字・ハイフン）を使用してください。\r\n"
            L"使用可能なアイコン名は https://pictogrammers.com/library/mdi/ で確認できます。\r\n"
            L"例: stamp01=hand-pointing-left  stamp13=lock  stamp14=plus-box  stamp15=minus-box",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            16, 252, 540, 82, hwnd, nullptr, g.instance, nullptr);
        CreateWindowW(L"BUTTON", L"閉じる", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 470, 346, 86, 28, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)), g.instance, nullptr);
        for (HWND child = GetWindow(hwnd, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) ApplyUiFont(child);
        return 0;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == IDOK) { DestroyWindow(hwnd); return 0; }
    if (message == WM_CLOSE) { DestroyWindow(hwnd); return 0; }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void ShowHelpDialog() {
    HelpDialogState state;
    RECT ownerRect{}; GetWindowRect(g.mainWindow, &ownerRect);
    HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME, kHelpClass, L"ヘルプ", WS_POPUP | WS_CAPTION | WS_SYSMENU,
        ownerRect.left + 40, ownerRect.top + 40, 590, 420, g.mainWindow, nullptr, g.instance, &state);
    if (!window) return;
    EnableWindow(g.mainWindow, FALSE);
    ShowWindow(window, SW_SHOW);
    MSG message{};
    while (IsWindow(window) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    EnableWindow(g.mainWindow, TRUE);
    SetForegroundWindow(g.mainWindow);
}

void ExecuteToolbarCommand(int id) {
    switch (id) {
    case IDC_CMD_OPEN: DoOpen(); break;
    case IDC_CMD_SAVE: DoOverwriteSave(); break;
    case IDC_CMD_SAVE_AS: DoSave(true); break;
    case IDC_CMD_EXPORT: DoExport(); break;
    case IDC_CMD_RESIZE:
        CommitInlineEdit(false);
        FinishAwaitingCalloutTip(true);
        g.tool = Tool::Select;
        g.cropActive = false;
        g.showingResize = true;
        g.pendingResizeScale = 0.0f;
        g.selected = -1;
        g.draft.reset();
        g.dragging = false;
        g.resizing = false;
        ApplySelectionToProperties();
        InvalidateCanvas();
        UpdateToolbarSelection();
        break;
    case IDC_TOOL_SELECT: SetTool(Tool::Select); break;
    case IDC_TOOL_CROP: SetTool(Tool::Crop); break;
    case IDC_TOOL_LINE: SetTool(Tool::Line); break;
    case IDC_TOOL_ARROW: SetTool(Tool::Arrow); break;
    case IDC_TOOL_RECT: SetTool(Tool::Rectangle); break;
    case IDC_TOOL_ELLIPSE: SetTool(Tool::Ellipse); break;
    case IDC_TOOL_FREEHAND: SetTool(Tool::Freehand); break;
    case IDC_TOOL_TEXT: SetTool(Tool::Text); break;
    case IDC_TOOL_CALLOUT: SetTool(Tool::Callout); break;
    case IDC_TOOL_LOUPE: SetTool(Tool::Loupe); break;
    case IDC_TOOL_BLUR: SetTool(Tool::Blur); break;
    case IDC_TOOL_MOSAIC: SetTool(Tool::Mosaic); break;
    case IDC_TOOL_NUMBER: SetTool(Tool::Number); break;
    case IDC_TOOL_STAMP: SetTool(Tool::Stamp); break;
    case IDC_CMD_UNDO: Undo(false); break;
    case IDC_CMD_REDO: Undo(true); break;
    case IDC_CMD_DELETE: DeleteSelected(); break;
    case IDC_CMD_ZOOM_IN: ZoomBy(1.25f); break;
    case IDC_CMD_ZOOM_100: g.zoom = 1.0f; FitOrigin(); InvalidateCanvas(); break;
    case IDC_CMD_ZOOM_OUT: ZoomBy(0.8f); break;
    case IDC_CMD_HELP: ShowHelpDialog(); break;
    case IDC_CMD_EXIT: SendMessageW(g.mainWindow, WM_CLOSE, 0, 0); break;
    }
}

void ApplyUiFont(HWND hwnd) {
    if (g.propFont) SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(g.propFont), TRUE);
}

bool ControlShown(HWND hwnd) {
    return hwnd && (GetWindowLongW(hwnd, GWL_STYLE) & WS_VISIBLE);
}

void Layout(HWND hwnd) {
    RECT client{}; GetClientRect(hwnd, &client);
    const int canvasWidth = static_cast<int>(client.right);
    const int canvasHeight = (std::max)(1, static_cast<int>(client.bottom) - (kToolbarHeight + kPropertyHeight));
    MoveWindow(g.canvas, 0, kToolbarHeight + kPropertyHeight, canvasWidth, canvasHeight, TRUE);
    int groupX = 6;
    for (size_t gi = 0; gi < kRibbonGroups.size(); ++gi) {
        const auto& group = kRibbonGroups[gi];
        const int groupWidth = group.cols * kRibbonButton + (group.cols - 1) * kRibbonGap;
        const int groupHeight = group.rows * kRibbonButton + (group.rows - 1) * kRibbonGap;
        g_ribbonGroups[gi].label = group.label;
        g_ribbonGroups[gi].bounds = RECT{groupX, kRibbonTop, groupX + groupWidth, kRibbonTop + groupHeight};
        size_t slot = 0;
        for (const int id : group.ids) {
            if (id != 0) {
                const int col = static_cast<int>(slot % static_cast<size_t>(group.cols));
                const int row = static_cast<int>(slot / static_cast<size_t>(group.cols));
                if (HWND button = GetDlgItem(hwnd, id)) {
                    MoveWindow(button, groupX + col * (kRibbonButton + kRibbonGap), kRibbonTop + row * (kRibbonButton + kRibbonGap),
                        kRibbonButton, kRibbonButton, TRUE);
                }
            }
            ++slot;
        }
        groupX += groupWidth + kRibbonGroupGap + 1;
    }
    // ---- プロパティ行レイアウト ----
    // 各数値はプロパティ帯の中心から計算する。
    constexpr int kPropMargin = 6;           // 上下マージン
    constexpr int kLabelH     = 18;          // ラベル高さ
    constexpr int kComboH     = 22;          // コンボボックス高さ
    constexpr int kSliderH    = 28;          // スライダー高さ
    constexpr int kCheckH     = 24;          // チェックボックス高さ
    constexpr int kColorBoxW  = 28;          // 色ボックス幅
    constexpr int kColorBoxH  = 24;          // 色ボックス高さ

    const int propTop  = kToolbarHeight;
    const int propMid  = propTop + kPropertyHeight / 2;  // 帯の垂直中心

    // 各コントロールを垂直中心に揃えるY座標
    const int labelY  = propMid - kLabelH  / 2;
    const int comboY  = propMid - kComboH  / 2;
    const int sliderY = propMid - kSliderH / 2;
    const int checkY  = propMid - kCheckH  / 2;
    const int stampY  = propTop + kPropMargin;            // スタンプボタンは上寄せ（帯に収まる）
    const int colorBoxY = propMid - kColorBoxH / 2;

    int px = 10;

    // コンボボックス（ラベル + コンボ）を横並びで配置
    auto placeCombo = [&](HWND label, HWND value, int labelW, int valueW) {
        if (!ControlShown(value)) return;
        if (labelW > 0 && ControlShown(label)) {
            MoveWindow(label, px, labelY, labelW, kLabelH, TRUE);
            MoveWindow(value, px + labelW + 4, comboY, valueW, kComboH, TRUE);
            px += labelW + 4 + valueW + 10;
        } else {
            MoveWindow(value, px, comboY, valueW, kComboH, TRUE);
            px += valueW + 10;
        }
    };

    // スライダー（ラベル + スライダー + チェック）を横並びで配置
    auto placeSliderGroup = [&](HWND label, HWND slider, HWND check, int labelW, int sliderW, int checkW) {
        const bool hasSlider = ControlShown(slider);
        const bool hasCheck  = check && ControlShown(check);
        if (!hasSlider && !hasCheck) return;
        if (ControlShown(label)) {
            MoveWindow(label, px, labelY, labelW, kLabelH, TRUE);
            px += labelW + 2;
        }
        if (hasSlider) {
            MoveWindow(slider, px, sliderY, sliderW, kSliderH, TRUE);
            px += sliderW + 4;
        }
        if (hasCheck) {
            MoveWindow(check, px, checkY, checkW, kCheckH, TRUE);
            px += checkW + 10;
        } else if (hasSlider) {
            px += 4;
        }
    };

    placeCombo(g.propLabel[0], g.propValue[0], 40, 120);
    placeCombo(g.propLabel[1], g.propValue[1], 40, 72);
    placeCombo(g.propLabel[2], g.propValue[2], 40, 72);
    placeSliderGroup(g.propDiameterLabel, g.propDiameter, nullptr, 36, 96, 0);

    // スタンプ選択ボタン群
    for (uint32_t i = 0; i < kStampCount; ++i) {
        if (ControlShown(g.stampPickButtons[i])) {
            MoveWindow(g.stampPickButtons[i], px, stampY, kStampPickSize, kStampPickSize, TRUE);
            px += kStampPickSize + 4;
        }
    }

    // propOutline がサイズスライダー(propDiameter)と同時に表示される場合（Text/Callout）:
    // サイズスライダーと線幅スライダーの間に周囲色チェックを置く
    const bool outlineAfterDiameter = ControlShown(g.propDiameter) && ControlShown(g.propOutline);
    if (outlineAfterDiameter) {
        MoveWindow(g.propOutline, px, checkY, 56, kCheckH, TRUE);
        px += 60;
    }

    // propFill は線幅スライダーの左（破線・向き反転など）に配置
    if (ControlShown(g.propFill)) {
        MoveWindow(g.propFill, px, checkY, 88, kCheckH, TRUE);
        px += 92;
    }
    placeSliderGroup(g.propWidthLabel, g.propWidth,
        outlineAfterDiameter ? nullptr : g.propOutline, 36, 88, 56);
    placeSliderGroup(g.propOutlineWidthLabel, g.propOutlineWidth, nullptr,       40, 80,  0);
    placeSliderGroup(g.propOpacityLabel,      g.propOpacity,      nullptr,       44, 88,  0);

    if (ControlShown(g.propApply)) {
        MoveWindow(g.propApply, px, checkY, 52, kCheckH, TRUE);
        px += 60;
    }
    if (ControlShown(g.propSizeInfo)) {
        MoveWindow(g.propSizeInfo, px, labelY, 360, kLabelH, TRUE);
        px += 368;
    }

    // 色1・色2は右端固定
    const int colorX  = static_cast<int>(client.right) - 120;
    const int colorY2 = colorBoxY + 10;
    MoveWindow(g.color1Label, colorX,      colorY2,   24,        kLabelH,    TRUE);
    MoveWindow(g.color1Box,   colorX + 24, colorBoxY, kColorBoxW, kColorBoxH, TRUE);
    MoveWindow(g.color2Label, colorX + 58, colorY2,   24,        kLabelH,    TRUE);
    MoveWindow(g.color2Box,   colorX + 82, colorBoxY, kColorBoxW, kColorBoxH, TRUE);
}

HWND CreateChild(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int id) {
    HWND hwnd = CreateWindowW(cls, text, WS_CHILD | style, 0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g.instance, nullptr);
    ApplyUiFont(hwnd);
    return hwnd;
}

void CreateControls(HWND hwnd) {
    g.uiFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    g.propFont = CreateFontW(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g.tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwnd, nullptr, g.instance, nullptr);
    SendMessageW(g.tooltip, TTM_SETMAXTIPWIDTH, 0, 320);
    LoadIconFont();
    for (const auto& entry : kToolbarButtons) {
        DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP;
        if (entry.selectable || entry.id == IDC_CMD_RESIZE) style |= BS_PUSHLIKE;
        else style |= BS_PUSHBUTTON;
        HWND button = CreateWindowW(L"BUTTON", entry.label, style, 0, 0, 32, 32, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(entry.id)), g.instance, nullptr);
        ApplyButtonGlyph(button, entry.glyph, entry.label, g.ribbonIconFont);
        AddTooltip(hwnd, button, entry.label);
    }
    for (uint32_t i = 0; i < kStampCount; ++i) {
        g.stampPickButtons[i] = CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_TABSTOP | BS_PUSHLIKE,
            0, 0, kStampPickSize, kStampPickSize, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STAMP_PICK_FIRST + i)), g.instance, nullptr);
        ApplyButtonGlyph(g.stampPickButtons[i], kStampGlyphs(i), kStampLabels(i), g.stampIconFont);
        AddTooltip(hwnd, g.stampPickButtons[i], kStampLabels(i));
        ShowWindow(g.stampPickButtons[i], SW_HIDE);
    }
    g.canvas = CreateWindowExW(0, kCanvasClass, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 100, 100, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCanvasId)), g.instance, nullptr);
    for (int i = 0; i < 3; ++i) {
        g.propLabel[i] = CreateChild(hwnd, L"STATIC", L"", 0, IDC_PROP_LABEL1 + i);
        g.propValue[i] = CreateChild(hwnd, WC_COMBOBOXW, L"", WS_VISIBLE | CBS_DROPDOWN | WS_VSCROLL, IDC_PROP_VALUE1 + i);
    }
    g.propWidthLabel = CreateChild(hwnd, L"STATIC", L"線幅", 0, 0);
    g.propWidth = CreateWindowW(TRACKBAR_CLASSW, L"", WS_CHILD | TBS_HORZ | TBS_NOTICKS, 0, 0, 100, 28, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROP_WIDTH)), g.instance, nullptr);
    g.propOutline = CreateChild(hwnd, L"BUTTON", L"周囲色", BS_AUTOCHECKBOX | BS_VCENTER, IDC_PROP_OUTLINE);
    g.propOutlineWidthLabel = CreateChild(hwnd, L"STATIC", L"縁取り", 0, 0);
    g.propOutlineWidth = CreateWindowW(TRACKBAR_CLASSW, L"", WS_CHILD | TBS_HORZ | TBS_NOTICKS, 0, 0, 90, 28, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROP_OUTLINE_WIDTH)), g.instance, nullptr);
    g.propOpacityLabel = CreateChild(hwnd, L"STATIC", L"透明度", 0, 0);
    g.propOpacity = CreateWindowW(TRACKBAR_CLASSW, L"", WS_CHILD | TBS_HORZ | TBS_NOTICKS, 0, 0, 100, 28, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROP_OPACITY)), g.instance, nullptr);
    g.propDiameterLabel = CreateChild(hwnd, L"STATIC", L"直径", 0, 0);
    g.propDiameter = CreateWindowW(TRACKBAR_CLASSW, L"", WS_CHILD | TBS_HORZ | TBS_NOTICKS, 0, 0, 110, 28, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROP_DIAMETER)), g.instance, nullptr);
    g.propFill = CreateChild(hwnd, L"BUTTON", L"背景色", BS_AUTOCHECKBOX | BS_VCENTER, IDC_PROP_FILL);
    g.propSizeInfo = CreateChild(hwnd, L"STATIC", L"", 0, IDC_PROP_SIZEINFO);
    g.propApply = CreateChild(hwnd, L"BUTTON", L"適用", WS_TABSTOP, IDC_PROP_APPLY);
    g.color1Label = CreateChild(hwnd, L"STATIC", L"色1", WS_VISIBLE, IDC_COLOR1_LABEL);
    g.color1Box = CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 28, 24, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_COLOR1)), g.instance, nullptr);
    g.color2Label = CreateChild(hwnd, L"STATIC", L"色2", WS_VISIBLE, IDC_COLOR2_LABEL);
    g.color2Box = CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 28, 24, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_COLOR2)), g.instance, nullptr);
    AddTooltip(hwnd, g.color1Box, L"色1");
    AddTooltip(hwnd, g.color2Box, L"色2");
    DragAcceptFiles(hwnd, TRUE);
    DragAcceptFiles(g.canvas, TRUE);
    SetTool(Tool::Select);
}

void EnsureCanvasTarget(HWND hwnd) {
    RECT client{}; GetClientRect(hwnd, &client);
    const UINT width  = static_cast<UINT>((std::max)(1, static_cast<int>(client.right)));
    const UINT height = static_cast<UINT>((std::max)(1, static_cast<int>(client.bottom)));
    // PerMonitor DPI: 物理DPIをレンダリングターゲットに設定することで座標系を一致させる
    const float dpi = static_cast<float>(GetDpiForWindow(hwnd));
    if (!g.target) {
        g.d2d->CreateHwndRenderTarget(D2D1::RenderTargetProperties(), D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(width, height)), &g.target);
        if (g.target) g.target->SetDpi(dpi, dpi);
        return;
    }
    const auto size = g.target->GetPixelSize();
    if (size.width != width || size.height != height) {
        if (FAILED(g.target->Resize(D2D1::SizeU(width, height)))) {
            g.target.Reset();
            InvalidateDocBitmap();
        }
        if (!g.target) {
            g.d2d->CreateHwndRenderTarget(D2D1::RenderTargetProperties(), D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(width, height)), &g.target);
        }
    }
    if (g.target) g.target->SetDpi(dpi, dpi);
}

void PaintCanvas(HWND hwnd) {
    PAINTSTRUCT ps{}; BeginPaint(hwnd, &ps);
    EnsureCanvasTarget(hwnd);
    if (g.target) {
        g.target->BeginDraw();
        g.target->Clear(D2D1::ColorF(0x26292E));
        g.target->SetTransform(D2D1::Matrix3x2F::Scale(g.zoom, g.zoom) * D2D1::Matrix3x2F::Translation(g.origin.x, g.origin.y));
        DrawDocument(g.target.Get(), false);
        if (g.cropActive) {
            ComPtr<ID2D1SolidColorBrush> shade;
            ComPtr<ID2D1SolidColorBrush> outline;
            g.target->CreateSolidColorBrush(D2D1::ColorF(0.f, 0.f, 0.f, 0.45f), &shade);
            g.target->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Orange), &outline);
            const auto crop = NormalizeRect(g.cropRect);
            const D2D1_RECT_F bounds = D2D1::RectF(0, 0, static_cast<float>(g.doc.width), static_cast<float>(g.doc.height));
            g.target->FillRectangle(D2D1::RectF(bounds.left, bounds.top, bounds.right, crop.top), shade.Get());
            g.target->FillRectangle(D2D1::RectF(bounds.left, crop.bottom, bounds.right, bounds.bottom), shade.Get());
            g.target->FillRectangle(D2D1::RectF(bounds.left, crop.top, crop.left, crop.bottom), shade.Get());
            g.target->FillRectangle(D2D1::RectF(crop.right, crop.top, bounds.right, crop.bottom), shade.Get());
            g.target->DrawRectangle(crop, outline.Get(), 2.0f / g.zoom);
            for (auto p : {D2D1::Point2F(crop.left, crop.top), D2D1::Point2F((crop.left + crop.right) * 0.5f, crop.top), D2D1::Point2F(crop.right, crop.top), D2D1::Point2F(crop.right, (crop.top + crop.bottom) * 0.5f),
                           D2D1::Point2F(crop.right, crop.bottom), D2D1::Point2F((crop.left + crop.right) * 0.5f, crop.bottom), D2D1::Point2F(crop.left, crop.bottom), D2D1::Point2F(crop.left, (crop.top + crop.bottom) * 0.5f)}) {
                g.target->FillRectangle(D2D1::RectF(p.x - 4.0f / g.zoom, p.y - 4.0f / g.zoom, p.x + 4.0f / g.zoom, p.y + 4.0f / g.zoom), outline.Get());
            }
        }
        g.target->SetTransform(D2D1::Matrix3x2F::Identity());
        if (g.target->EndDraw() == D2DERR_RECREATE_TARGET) {
            g.target.Reset();
            InvalidateDocBitmap();
        }
    }
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK CanvasProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_PAINT: PaintCanvas(hwnd); return 0;
    case WM_SIZE: {
        const UINT width = LOWORD(lParam), height = HIWORD(lParam);
        if (width == 0 || height == 0) return 0;
        if (g.target && FAILED(g.target->Resize(D2D1::SizeU(width, height)))) {
            g.target.Reset();
            InvalidateDocBitmap();
        }
        FitOrigin();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_DROPFILES:
        HandleDropFiles(reinterpret_cast<HDROP>(wParam));
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_SETCURSOR:
        if (reinterpret_cast<HWND>(lParam) == hwnd && LOWORD(wParam) == HTCLIENT) {
            POINT pt{};
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            if (const HCURSOR cursor = CanvasCursorAt(CanvasToImage(MAKELPARAM(pt.x, pt.y)))) {
                SetCursor(cursor);
                return TRUE;
            }
        }
        break;
    case WM_GETDLGCODE: return DLGC_WANTCHARS | DLGC_WANTARROWS | DLGC_WANTTAB;
    case WM_TIMER:
        if (wParam == 1 && g.textEditing) {
            g.caretVisible = !g.caretVisible;
            InvalidateCanvas();
        }
        return 0;
    case WM_CHAR:
        if (g.textEditing && g.selected >= 0 && g.selected < static_cast<int>(g.doc.objects.size())) {
            auto& object = g.doc.objects[g.selected];
            if (wParam == VK_RETURN || wParam == VK_ESCAPE || wParam == VK_TAB) {
                CommitInlineEdit(wParam != VK_ESCAPE);
            } else if (wParam == VK_BACK) {
                if (!object.text.empty()) object.text.pop_back();
                if (object.type == ObjectType::Callout) ExpandCalloutForText(object);
            } else if (wParam >= 32) {
                if (object.type == ObjectType::NumberStamp && (wParam < L'0' || wParam > L'9')) return 0;
                object.text.push_back(static_cast<wchar_t>(wParam));
                if (object.type == ObjectType::Text) {
                    object.rect.right = object.rect.left + std::max(120.0f, object.extra * static_cast<float>(std::max<size_t>(object.text.size(), 4)));
                } else if (object.type == ObjectType::Callout) {
                    ExpandCalloutForText(object);
                }
            }
            g.caretVisible = true;
            InvalidateCanvas();
            return 0;
        }
        return 0;
    case WM_MOUSEWHEEL: ZoomBy(GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 1.2f : 1.0f / 1.2f); return 0;
    case WM_MBUTTONDOWN: g.panning = true; g.panLast = POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}; SetCapture(hwnd); return 0;
    case WM_MBUTTONUP: if (g.panning) { g.panning = false; ReleaseCapture(); } return 0;
    case WM_LBUTTONDOWN: {
        SetFocus(hwnd);
        if (g.doc.pixels.empty()) {
            ShowError(g.mainWindow, L"先に画像を開いてください。");
            return 0;
        }
        CommitInlineEdit(false);
        const auto p = CanvasToImage(lParam); g.down = g.last = p;
        if (g.selected >= 0 && g.selected < static_cast<int>(g.doc.objects.size()) && g.tool != Tool::Crop && g.tool != Tool::Loupe) {
            const int handle = HitHandle(g.doc.objects[g.selected], p);
            if (handle == 10) {
                g.movingLoupePreview = true; g.resizing = false; g.dragging = true; SetCapture(hwnd); return 0;
            }
            if (handle >= 0) { g.activeHandle = handle; g.resizing = true; g.dragging = true; SetCapture(hwnd); return 0; }
            if (g.doc.objects[g.selected].type == ObjectType::Callout && PointInRect(g.doc.objects[g.selected].rect, p)) {
                g.textEditing = true; g.caretVisible = true;
                if (g.canvas) SetTimer(g.canvas, 1, 500, nullptr);
                InvalidateCanvas();
                return 0;
            }
        }
        if (g.tool == Tool::Crop) {
            const int handle = g.cropActive ? HitCropHandle(p) : -1;
            if (handle >= 0) { g.activeCropHandle = handle; g.resizingCrop = true; }
            else if (g.cropActive && PointInRect(g.cropRect, p)) { g.movingCrop = true; }
            else { g.cropActive = true; g.cropRect = D2D1::RectF(p.x, p.y, p.x, p.y); g.resizingCrop = true; g.activeCropHandle = 4; }
            g.dragging = true; SetCapture(hwnd); return 0;
        }
        if (g.tool == Tool::Select) {
            g.movingLoupePreview = false;
            if (g.selected >= 0 && g.selected < static_cast<int>(g.doc.objects.size())) {
                const int handle = HitHandle(g.doc.objects[g.selected], p);
                if (handle == 10) {
                    g.movingLoupePreview = true; g.resizing = false; g.dragging = true; SetCapture(hwnd); return 0;
                }
                if (handle >= 0) { g.activeHandle = handle; g.resizing = true; g.dragging = true; SetCapture(hwnd); return 0; }
            }
            g.selected = -1;
            int32_t topZ = INT32_MIN;
            for (size_t i = 0; i < g.doc.objects.size(); ++i) if (g.doc.objects[i].z >= topZ && HitTest(g.doc.objects[i], p)) { topZ = g.doc.objects[i].z; g.selected = static_cast<int>(i); }
            if (g.selected >= 0) {
                auto& object = g.doc.objects[g.selected];
                if (object.type == ObjectType::Loupe) g.movingLoupePreview = PointInRect(LoupePreviewRect(object), p) && !PointInRect(object.rect, p);
                g.dragging = true; SetCapture(hwnd); ApplySelectionToProperties();
            }
            InvalidateCanvas();
            return 0;
        }
        if (g.tool == Tool::Loupe) {
            g.movingLoupePreview = false;
            int hitLoupe = -1;
            int32_t topZ = INT32_MIN;
            for (size_t i = 0; i < g.doc.objects.size(); ++i) {
                const auto& object = g.doc.objects[i];
                if (object.type != ObjectType::Loupe || !object.visible) continue;
                if (object.z >= topZ && HitTest(object, p)) {
                    topZ = object.z;
                    hitLoupe = static_cast<int>(i);
                }
            }
            if (hitLoupe >= 0) {
                g.selected = hitLoupe;
                ApplySelectionToProperties();
                auto& object = g.doc.objects[g.selected];
                const int handle = HitHandle(object, p);
                if (handle >= 0 && handle <= 3) {
                    g.activeHandle = handle;
                    g.resizing = true;
                    g.movingLoupePreview = false;
                    g.dragging = true;
                    SetCapture(hwnd);
                    return 0;
                }
                if (handle == 10) {
                    g.movingLoupePreview = true;
                    g.resizing = false;
                    g.dragging = true;
                    SetCapture(hwnd);
                    return 0;
                }
                if (PointInRect(object.rect, p)) {
                    g.movingLoupePreview = false;
                    g.resizing = false;
                    g.dragging = true;
                    SetCapture(hwnd);
                    return 0;
                }
            }
            g.draft = NewObject(p);
            g.dragging = true;
            SetCapture(hwnd);
            return 0;
        }
        if (g.tool == Tool::Text) { BeginInlineEdit(p, ObjectType::Text); return 0; }
        if (g.tool == Tool::Number) { BeginInlineEdit(p, ObjectType::NumberStamp); return 0; }
        if (g.tool == Tool::Stamp) { PlaceStampAt(p); return 0; }
        g.draft = NewObject(p); g.dragging = true; SetCapture(hwnd); return 0;
    }
    case WM_MOUSEMOVE:
        if (g.panning) {
            const POINT now{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            g.origin.x += static_cast<float>(now.x - g.panLast.x) / g.dpiScale;
            g.origin.y += static_cast<float>(now.y - g.panLast.y) / g.dpiScale;
            g.panLast = now; InvalidateCanvas(); return 0;
        }
        if (!g.dragging) return 0;
        {
            const auto p = CanvasToImage(lParam);
            if (g.tool == Tool::Crop) {
                if (g.resizingCrop) ResizeCrop(g.activeCropHandle, p);
                else if (g.movingCrop) {
                    auto r = NormalizeRect(g.cropRect);
                    const float w = r.right - r.left, h = r.bottom - r.top;
                    const float dx = p.x - g.last.x, dy = p.y - g.last.y;
                    r.left = std::clamp(r.left + dx, 0.0f, static_cast<float>(g.doc.width) - w);
                    r.top = std::clamp(r.top + dy, 0.0f, static_cast<float>(g.doc.height) - h);
                    r.right = r.left + w; r.bottom = r.top + h;
                    g.cropRect = r;
                }
                UpdateCropSizeInfo();
            } else if (g.selected >= 0 && (g.resizing || g.movingLoupePreview || g.tool == Tool::Select ||
                (g.tool == Tool::Loupe && g.doc.objects[g.selected].type == ObjectType::Loupe))) {
                if (g.resizing) ResizeObject(g.doc.objects[g.selected], g.activeHandle, p);
                else MoveObject(g.doc.objects[g.selected], p.x - g.last.x, p.y - g.last.y);
            } else {
                UpdateDraft(p);
            }
            g.last = p; InvalidateCanvas();
        }
        return 0;
    case WM_LBUTTONUP:
        if (!g.dragging) return 0;
        ReleaseCapture();
        if (g.tool == Tool::Crop) {
            g.dragging = false; g.resizingCrop = false; g.movingCrop = false; g.activeCropHandle = -1; InvalidateCanvas(); return 0;
        }
        if (g.selected >= 0 && !g.draft && (g.tool == Tool::Select || g.tool == Tool::Loupe || g.resizing || g.movingLoupePreview)) {
            if (g.last.x != g.down.x || g.last.y != g.down.y) PushHistory();
            g.dragging = false; g.resizing = false; g.movingLoupePreview = false; g.activeHandle = -1; InvalidateCanvas(); return 0;
        }
        if (g.draft) {
            bool accept = false;
            if (g.draft->type == ObjectType::Arrow || g.draft->type == ObjectType::Line) {
                accept = DistanceToSegment(g.draft->points[0], g.draft->points[0], g.draft->points[1]) > 2.0f
                    || std::hypot(g.draft->points[1].x - g.draft->points[0].x, g.draft->points[1].y - g.draft->points[0].y) >= 4.0f;
            } else if (g.draft->type == ObjectType::Freehand) {
                accept = g.draft->points.size() >= 2;
            } else if (g.draft->type == ObjectType::Loupe) {
                const auto b = NormalizeRect(g.draft->rect);
                if (b.right - b.left < 8.0f || b.bottom - b.top < 8.0f) {
                    const float size = 72.0f;
                    const float cx = g.down.x, cy = g.down.y;
                    g.draft->rect = D2D1::RectF(cx - size * 0.5f, cy - size * 0.5f, cx + size * 0.5f, cy + size * 0.5f);
                    PlaceLoupePreview(*g.draft);
                }
                accept = true;
            } else if (g.draft->type == ObjectType::Callout) {
                auto b = NormalizeRect(g.draft->rect);
                if (b.right - b.left < 40.0f || b.bottom - b.top < 28.0f) {
                    g.draft->rect = D2D1::RectF(g.down.x - 60.0f, g.down.y - 24.0f, g.down.x + 60.0f, g.down.y + 24.0f);
                    b = NormalizeRect(g.draft->rect);
                } else {
                    g.draft->rect = b;
                }
                g.draft->points = {D2D1::Point2F((b.left + b.right) * 0.5f, b.bottom + 28.0f)};
                accept = true;
            } else {
                const auto b = Bounds(*g.draft);
                accept = (b.right - b.left >= 2.0f && b.bottom - b.top >= 2.0f);
            }
            if (accept) {
                const bool callout = g.draft->type == ObjectType::Callout;
                g.doc.objects.push_back(std::move(*g.draft));
                g.selected = static_cast<int>(g.doc.objects.size()) - 1;
                ApplySelectionToProperties();
                if (callout) {
                    g.textEditing = true;
                    g.caretVisible = true;
                    if (g.canvas) SetTimer(g.canvas, 1, 500, nullptr);
                    SetFocus(g.canvas);
                } else {
                    PushHistory();
                }
            }
            g.draft.reset();
        }
        g.dragging = false;
        InvalidateCanvas();
        return 0;
    case WM_KEYDOWN:
        if (g.textEditing) {
            if (wParam == VK_BACK) { SendMessageW(hwnd, WM_CHAR, VK_BACK, 0); return 0; }
            if (wParam == VK_RETURN) { CommitInlineEdit(true); return 0; }
            if (wParam == VK_ESCAPE) { CommitInlineEdit(false); return 0; }
        }
        if (wParam == VK_DELETE) { DeleteSelected(); return 0; }
        if (wParam == VK_ESCAPE) {
            if (g.dragging) ReleaseCapture();
            g.dragging = false; g.resizing = false; g.resizingCrop = false; g.movingCrop = false; g.draft.reset();
            if (g.tool == Tool::Crop) g.cropActive = false;
            g.awaitingCalloutTip = false;
            InvalidateCanvas(); return 0;
        }
        if (g.tool == Tool::Crop && wParam == VK_RETURN) { ApplyCrop(); return 0; }
        if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == 'Z') { Undo(false); return 0; }
        if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == 'Y') { Undo(true); return 0; }
        if (!g.textEditing && g.selected >= 0 && (wParam == VK_LEFT || wParam == VK_RIGHT || wParam == VK_UP || wParam == VK_DOWN)) {
            const float amount = (GetKeyState(VK_SHIFT) & 0x8000) ? 10.0f : 1.0f;
            MoveObject(g.doc.objects[g.selected], wParam == VK_LEFT ? -amount : wParam == VK_RIGHT ? amount : 0.0f,
                wParam == VK_UP ? -amount : wParam == VK_DOWN ? amount : 0.0f);
            PushHistory(); InvalidateCanvas(); return 0;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

// --- メインウィンドウ ---

LRESULT CALLBACK MainProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: g.mainWindow = hwnd; CreateControls(hwnd); Layout(hwnd); UpdateToolbarSelection(); return 0;
    case WM_SIZE: Layout(hwnd); if (g.canvas) InvalidateRect(g.canvas, nullptr, FALSE); InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        HPEN sepPen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_3DSHADOW));
        HPEN oldPen = static_cast<HPEN>(SelectObject(dc, sepPen));
        SetBkMode(dc, TRANSPARENT);
        HFONT oldFont = static_cast<HFONT>(SelectObject(dc, g.propFont ? g.propFont : g.uiFont));
        for (const auto& group : g_ribbonGroups) {
            if (group.bounds.right <= group.bounds.left) continue;
            MoveToEx(dc, group.bounds.right + kRibbonGroupGap / 2, kRibbonTop, nullptr);
            LineTo(dc, group.bounds.right + kRibbonGroupGap / 2, kToolbarHeight - kRibbonLabelHeight - 2);
            RECT labelRect{group.bounds.left, kToolbarHeight - kRibbonLabelHeight - 2, group.bounds.right, kToolbarHeight - 2};
            DrawTextW(dc, group.label, -1, &labelRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        SelectObject(dc, oldFont);
        SelectObject(dc, oldPen);
        DeleteObject(sepPen);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DPICHANGED: {
        g.dpiScale = static_cast<float>(LOWORD(wParam)) / 96.0f;
        const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
        if (suggested) SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        g.target.Reset();
        InvalidateDocBitmap();
        FitOrigin();
        Layout(hwnd);
        return 0;
    }
    case WM_DROPFILES:
        HandleDropFiles(reinterpret_cast<HDROP>(wParam));
        return 0;
    case WM_KEYDOWN:
        if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == 'Z') { Undo(false); return 0; }
        if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == 'Y') { Undo(true); return 0; }
        break;
    case WM_DRAWITEM: {
        const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if (!item || (item->CtlID != IDC_COLOR1 && item->CtlID != IDC_COLOR2)) break;
        const uint32_t color = item->CtlID == IDC_COLOR1 ? g.color1 : g.color2;
        HBRUSH fill = CreateSolidBrush(RgbaToColorRef(color));
        FillRect(item->hDC, &item->rcItem, fill);
        DeleteObject(fill);
        FrameRect(item->hDC, &item->rcItem, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        return TRUE;
    }
    case WM_HSCROLL:
        if (reinterpret_cast<HWND>(lParam) == g.propWidth || reinterpret_cast<HWND>(lParam) == g.propOutlineWidth ||
            reinterpret_cast<HWND>(lParam) == g.propOpacity || reinterpret_cast<HWND>(lParam) == g.propDiameter) {
            ReadPropertyControls(LOWORD(wParam) == TB_ENDTRACK);
            return 0;
        }
        break;
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        if (id >= IDC_TOOLBAR_FIRST && id <= IDC_TOOLBAR_LAST) {
            ExecuteToolbarCommand(id);
            SetFocus(g.canvas);
            return 0;
        }
        if (id >= IDC_STAMP_PICK_FIRST && id <= IDC_STAMP_PICK_LAST) {
            g.selectedStampIndex = static_cast<uint32_t>(id - IDC_STAMP_PICK_FIRST);
            UpdateStampPickerSelection();
            if (g.selected >= 0 && g.selected < static_cast<int>(g.doc.objects.size()) &&
                g.doc.objects[g.selected].type == ObjectType::IconStamp) {
                g.doc.objects[g.selected].blockSize = g.selectedStampIndex;
                PushHistory();
                InvalidateCanvas();
            }
            ReadPropertyControls(false);
            SetFocus(g.canvas);
            return 0;
        }
        if (id == IDC_COLOR1) { ChooseColor(1); SetFocus(g.canvas); return 0; }
        if (id == IDC_COLOR2) { ChooseColor(2); SetFocus(g.canvas); return 0; }
        if (id == IDC_PROP_OUTLINE || id == IDC_PROP_FILL) {
            ReadPropertyControls(true);
            return 0;
        }
        if (id == IDC_PROP_APPLY) {
            if (CurrentPropertyKind() == PropertyKind::Crop) ApplyCrop();
            else if (g.pendingResizeScale > 0.0f) ApplyImageResize(g.pendingResizeScale);
            return 0;
        }
        if (id >= IDC_PROP_VALUE1 && id <= IDC_PROP_VALUE3) {
            if (HIWORD(wParam) == CBN_SELCHANGE || HIWORD(wParam) == CBN_SELENDOK ||
                (HIWORD(wParam) == CBN_KILLFOCUS && CurrentPropertyKind() != PropertyKind::Resize)) {
                ReadPropertyControls(true);
                return 0;
            }
        }
        return 0;
    }
    case WM_CLOSE:
        if (!g.doc.dirty) { DestroyWindow(hwnd); return 0; }
        switch (PromptUnsavedClose()) {
        case IDC_CLOSE_DISCARD: DestroyWindow(hwnd); break;
        case IDC_CLOSE_SAVE: if (DoOverwriteSave() && !g.doc.dirty) DestroyWindow(hwnd); break;
        case IDC_CLOSE_SAVE_AS: if (DoSave(true) && !g.doc.dirty) DestroyWindow(hwnd); break;
        default: break;
        }
        return 0;
    case WM_DESTROY:
        SaveSettings();
        if (g.propFont) { DeleteObject(g.propFont); g.propFont = nullptr; }
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

HICON LoadAppIcon(int cx, int cy) {
    return static_cast<HICON>(LoadImageW(g.instance, MAKEINTRESOURCEW(IDI_LITEDRAW), IMAGE_ICON, cx, cy, 0));
}

bool RegisterClasses() {
    const HICON appIcon = LoadAppIcon(GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
    const HICON appIconSm = LoadAppIcon(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
    WNDCLASSEXW mainClass{sizeof(mainClass)};
    mainClass.style = CS_HREDRAW | CS_VREDRAW; mainClass.lpfnWndProc = MainProc; mainClass.hInstance = g.instance;
    mainClass.hIcon = appIcon;
    mainClass.hIconSm = appIconSm ? appIconSm : appIcon;
    mainClass.hCursor = LoadCursorW(nullptr, IDC_ARROW); mainClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1); mainClass.lpszClassName = kMainClass;
    if (!RegisterClassExW(&mainClass)) return false;
    WNDCLASSEXW canvasClass{sizeof(canvasClass)};
    canvasClass.style = CS_HREDRAW | CS_VREDRAW; canvasClass.lpfnWndProc = CanvasProc; canvasClass.hInstance = g.instance;
    canvasClass.hCursor = LoadCursorW(nullptr, IDC_CROSS); canvasClass.lpszClassName = kCanvasClass;
    if (!RegisterClassExW(&canvasClass)) return false;
    WNDCLASSEXW promptClass{sizeof(promptClass)};
    promptClass.lpfnWndProc = PromptProc; promptClass.hInstance = g.instance; promptClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    promptClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1); promptClass.lpszClassName = kPromptClass;
    if (!RegisterClassExW(&promptClass)) return false;
    WNDCLASSEXW closeClass{sizeof(closeClass)};
    closeClass.lpfnWndProc = CloseProc; closeClass.hInstance = g.instance; closeClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    closeClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1); closeClass.lpszClassName = kCloseClass;
    if (!RegisterClassExW(&closeClass)) return false;
    WNDCLASSEXW helpClass{sizeof(helpClass)};
    helpClass.lpfnWndProc = HelpProc; helpClass.hInstance = g.instance; helpClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    helpClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1); helpClass.lpszClassName = kHelpClass;
    return RegisterClassExW(&helpClass) != 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    g.instance = instance;
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&controls);
    LoadSettings();
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, g.d2d.GetAddressOf());
    if (SUCCEEDED(hr)) {
        auto makeStroke = [&](D2D1_DASH_STYLE dash, ID2D1StrokeStyle** out) {
            D2D1_STROKE_STYLE_PROPERTIES strokeProps{
                D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                D2D1_LINE_JOIN_ROUND, 10.0f, dash, 0.0f
            };
            return g.d2d->CreateStrokeStyle(strokeProps, nullptr, 0, out);
        };
        hr = makeStroke(D2D1_DASH_STYLE_SOLID, &g.roundStroke);
        if (SUCCEEDED(hr)) hr = makeStroke(D2D1_DASH_STYLE_DASH, &g.dashStroke);
        if (SUCCEEDED(hr)) hr = makeStroke(D2D1_DASH_STYLE_DASH_DOT, &g.dashDotStroke);
        if (SUCCEEDED(hr)) hr = makeStroke(D2D1_DASH_STYLE_DASH_DOT_DOT, &g.dashDotDotStroke);
    }
    if (SUCCEEDED(hr)) hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(g.dwrite.GetAddressOf()));
    if (SUCCEEDED(hr)) hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g.wic));
    if (FAILED(hr) || !RegisterClasses()) {
        ShowError(nullptr, L"LiteDraw を初期化できませんでした。");
        CoUninitialize();
        return 1;
    }
    const int contentWidth = ComputeToolbarWidth();
    RECT windowRect{0, 0, contentWidth, 860};
    AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);
    g.mainWindow = CreateWindowExW(0, kMainClass, L"LiteDraw", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        windowRect.right - windowRect.left, windowRect.bottom - windowRect.top, nullptr, nullptr, instance, nullptr);
    if (!g.mainWindow) { CoUninitialize(); return 1; }
    g.dpiScale = static_cast<float>(GetDpiForWindow(g.mainWindow)) / 96.0f;
    ShowWindow(g.mainWindow, showCommand);
    UpdateWindow(g.mainWindow);
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc >= 2) OpenDocumentFromPath(argv[1]);
    if (argv) LocalFree(argv);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    ReleaseIconFont();
    CoUninitialize();
    return static_cast<int>(message.wParam);
}
