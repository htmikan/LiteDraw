#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "resource.h"

using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kMainClass[] = L"LiteDraw.Main";
constexpr wchar_t kCanvasClass[] = L"LiteDraw.Canvas";
constexpr wchar_t kPromptClass[] = L"LiteDraw.Prompt";
constexpr UINT_PTR kCanvasId = 60001;
constexpr float kPi = 3.14159265358979323846f;
constexpr int kToolbarHeight = 48;
constexpr int kPropertyHeight = 40;
constexpr int kToolbarButton = 36;
constexpr int kToolbarGap = 6;

enum class ObjectType : uint8_t {
    Arrow = 1, Rectangle, Ellipse, Text, Freehand, BlurRegion, MosaicRegion, NumberStamp, Loupe
};

enum class Tool {
    Select, Crop, Arrow, Rectangle, Ellipse, Freehand, Text, Loupe, Blur, Mosaic, Number
};

enum class PropertyKind {
    None, Text, Number, Stroke, Blur, Mosaic, Loupe
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
    float extra = 16.0f;
    uint32_t blockSize = 12;
    bool fill = false;
};

struct Document {
    UINT width = 0;
    UINT height = 0;
    std::vector<uint8_t> pixels;
    std::vector<uint8_t> png;
    std::wstring originalName;
    std::wstring projectPath;
    FILETIME created{};
    FILETIME modified{};
    std::vector<Object> objects;
    uint32_t nextId = 1;
    bool dirty = false;
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
    HWND propColor{};
    HFONT uiFont{};
    HFONT iconFont{};
    bool iconFontLoaded = false;
    ComPtr<ID2D1Factory> d2d;
    ComPtr<IDWriteFactory> dwrite;
    ComPtr<IWICImagingFactory> wic;
    ComPtr<ID2D1HwndRenderTarget> target;
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
    int activeHandle = -1;
    int activeCropHandle = -1;
    POINT panLast{};
    D2D1_POINT_2F down{};
    D2D1_POINT_2F last{};
    D2D1_RECT_F cropRect{};
    float zoom = 1.0f;
    D2D1_POINT_2F origin{16.0f, 16.0f};
    float lineWidth = 3.0f;
    uint32_t effectSize = 12;
    uint32_t numberDiameter = 42;
    float textSize = 24.0f;
    float loupeZoom = 2.0f;
    bool loupeCircle = true;
    std::wstring textFont = L"Segoe UI";
    uint32_t color = 0xE53935FFU;
    std::vector<std::vector<uint8_t>> history;
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

const std::array<ToolButton, 26> kToolbarButtons{{
    {IDC_CMD_OPEN, false, Tool::Select, L"Open", L"\U000F0341"},
    {IDC_CMD_SAVE, false, Tool::Select, L"Save", L"\U000F0214"},
    {IDC_CMD_SAVE_AS, false, Tool::Select, L"Save As", L"\U000F0818"},
    {IDC_CMD_EXPORT, false, Tool::Select, L"Export", L"\U000F0207"},
    {IDC_CMD_RESIZE_75, false, Tool::Select, L"Resize 75%", L"75"},
    {IDC_CMD_RESIZE_50, false, Tool::Select, L"Resize 50%", L"50"},
    {IDC_CMD_RESIZE_25, false, Tool::Select, L"Resize 25%", L"25"},
    {IDC_TOOL_SELECT, true, Tool::Select, L"Select", L"\U000F0493"},
    {IDC_TOOL_CROP, true, Tool::Crop, L"Crop", L"\U000F0194"},
    {IDC_TOOL_ARROW, true, Tool::Arrow, L"Arrow", L"\U000F004D"},
    {IDC_TOOL_RECT, true, Tool::Rectangle, L"Rectangle", L"\U000F0E5E"},
    {IDC_TOOL_ELLIPSE, true, Tool::Ellipse, L"Ellipse", L"\U000F0765"},
    {IDC_TOOL_FREEHAND, true, Tool::Freehand, L"Freehand", L"\U000F15FC"},
    {IDC_TOOL_TEXT, true, Tool::Text, L"Text", L"\U000F09A8"},
    {IDC_TOOL_LOUPE, true, Tool::Loupe, L"Loupe", L"\U000F0349"},
    {IDC_TOOL_BLUR, true, Tool::Blur, L"Blur", L"\U000F0E52"},
    {IDC_TOOL_MOSAIC, true, Tool::Mosaic, L"Mosaic", L"\U000F075A"},
    {IDC_TOOL_NUMBER, true, Tool::Number, L"Number", L"\U000F03D5"},
    {IDC_CMD_UNDO, false, Tool::Select, L"Undo", L"\U000F054C"},
    {IDC_CMD_REDO, false, Tool::Select, L"Redo", L"\U000F044E"},
    {IDC_CMD_DELETE, false, Tool::Select, L"Delete", L"\U000F01B4"},
    {IDC_CMD_ZOOM_IN, false, Tool::Select, L"Zoom In", L"\U000F034B"},
    {IDC_CMD_ZOOM_100, false, Tool::Select, L"Zoom 100%", L"1:1"},
    {IDC_CMD_ZOOM_OUT, false, Tool::Select, L"Zoom Out", L"\U000F034A"},
    {IDC_CMD_HELP, false, Tool::Select, L"Help", L"\U000F02D7"},
    {IDC_CMD_EXIT, false, Tool::Select, L"Exit", L"\U000F0156"},
}};

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

HRESULT EncodePixels(const std::vector<uint8_t>& pixels, UINT width, UINT height, REFGUID container, std::vector<uint8_t>& output) {
    output.clear();
    ComPtr<IStream> stream;
    HRESULT hr = CreateStreamOnHGlobal(nullptr, TRUE, &stream);
    ComPtr<IWICBitmapEncoder> encoder;
    if (SUCCEEDED(hr)) hr = g.wic->CreateEncoder(container, nullptr, &encoder);
    if (SUCCEEDED(hr)) hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> props;
    if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, &props);
    if (SUCCEEDED(hr)) hr = frame->Initialize(props.Get());
    if (SUCCEEDED(hr)) hr = frame->SetSize(width, height);
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (IsEqualGUID(container, GUID_ContainerFormatJpeg)) format = GUID_WICPixelFormat24bppBGR;
    else if (IsEqualGUID(container, GUID_ContainerFormatBmp)) format = GUID_WICPixelFormat32bppBGR;
    if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&format);
    ComPtr<IWICBitmap> source;
    if (SUCCEEDED(hr)) hr = g.wic->CreateBitmapFromMemory(width, height, GUID_WICPixelFormat32bppPBGRA, width * 4U,
        static_cast<UINT>(pixels.size()), const_cast<BYTE*>(pixels.data()), &source);
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
    if (FAILED(hr) || width == 0 || height == 0) return FAILED(hr) ? hr : E_INVALIDARG;
    pixels.resize(static_cast<size_t>(width) * height * 4U);
    return converter->CopyPixels(nullptr, width * 4U, static_cast<UINT>(pixels.size()), pixels.data());
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

WICBitmapTransformOptions OrientationTransform(USHORT orientation) {
    switch (orientation) {
    case 2: return WICBitmapTransformFlipHorizontal;
    case 3: return WICBitmapTransformRotate180;
    case 4: return WICBitmapTransformFlipVertical;
    case 5: return static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate90 | WICBitmapTransformFlipHorizontal);
    case 6: return WICBitmapTransformRotate90;
    case 7: return static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate270 | WICBitmapTransformFlipHorizontal);
    case 8: return WICBitmapTransformRotate270;
    default: return WICBitmapTransformRotate0;
    }
}

void InvalidateCanvas() { if (g.canvas) InvalidateRect(g.canvas, nullptr, FALSE); }

void ResetHistory();

HRESULT LoadImageFile(const std::wstring& path) {
    ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = g.wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
    ComPtr<IWICBitmapFrameDecode> frame;
    if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);
    IWICBitmapSource* source = frame.Get();
    ComPtr<IWICBitmapFlipRotator> rotator;
    if (SUCCEEDED(hr)) {
        USHORT orientation = 1;
        ComPtr<IWICMetadataQueryReader> metadata;
        PROPVARIANT value{};
        PropVariantInit(&value);
        if (SUCCEEDED(frame->GetMetadataQueryReader(&metadata)) &&
            SUCCEEDED(metadata->GetMetadataByName(L"/app1/ifd/{ushort=274}", &value)) &&
            value.vt == VT_UI2) orientation = value.uiVal;
        PropVariantClear(&value);
        const auto transform = OrientationTransform(orientation);
        if (transform != WICBitmapTransformRotate0 &&
            SUCCEEDED(g.wic->CreateBitmapFlipRotator(&rotator)) &&
            SUCCEEDED(rotator->Initialize(frame.Get(), transform))) {
            source = rotator.Get();
        }
        hr = DecodeSource(source, g.doc.pixels, g.doc.width, g.doc.height);
    }
    if (SUCCEEDED(hr)) hr = EncodePixels(g.doc.pixels, g.doc.width, g.doc.height, GUID_ContainerFormatPng, g.doc.png);
    if (SUCCEEDED(hr)) {
        g.doc.originalName = std::filesystem::path(path).filename().wstring();
        g.doc.projectPath.clear();
        GetSystemTimeAsFileTime(&g.doc.created);
        g.doc.modified = g.doc.created;
        g.doc.objects.clear();
        g.doc.nextId = 1;
        g.doc.dirty = false;
        g.selected = -1;
        g.cropActive = false;
        g.zoom = 1.0f;
        ResetHistory();
    }
    return hr;
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
        break;
    case ObjectType::Rectangle:
    case ObjectType::Ellipse:
        rect(); w.pod(object.width); w.pod(object.color); break;
    case ObjectType::Text: {
        w.pod(object.rect.left); w.pod(object.rect.top);
        const auto text = ToUtf8(object.text);
        const auto font = ToUtf8(object.font);
        w.pod<uint32_t>(static_cast<uint32_t>(text.size())); w.bytes(std::string_view(text));
        w.pod<uint16_t>(static_cast<uint16_t>(std::min<size_t>(font.size(), 65535U)));
        w.bytes(std::string_view(font).substr(0, std::min<size_t>(font.size(), 65535U)));
        w.pod(object.extra); w.pod(object.color);
        break;
    }
    case ObjectType::Freehand:
        w.pod<uint32_t>(static_cast<uint32_t>(object.points.size()));
        for (auto p : object.points) { w.pod(p.x); w.pod(p.y); }
        w.pod(object.width); w.pod(object.color);
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
        w.pod(object.extra); w.pod(object.color);
        break;
    }
    case ObjectType::Loupe:
        rect(); w.pod(object.extra); w.pod<uint8_t>(object.fill ? 1 : 0); w.pod(object.color); break;
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
        object.points[0] = D2D1::Point2F(r.pod<float>(), r.pod<float>());
        object.points[1] = D2D1::Point2F(r.pod<float>(), r.pod<float>());
        object.width = r.pod<float>(); object.color = r.pod<uint32_t>(); object.extra = r.pod<float>();
        break;
    case ObjectType::Rectangle:
    case ObjectType::Ellipse:
        rect(); object.width = r.pod<float>(); object.color = r.pod<uint32_t>(); break;
    case ObjectType::Text: {
        object.rect.left = r.pod<float>(); object.rect.top = r.pod<float>();
        const auto textLen = r.pod<uint32_t>();
        object.text = FromUtf8(r.bytes(textLen));
        const auto fontLen = r.pod<uint16_t>();
        object.font = FromUtf8(r.bytes(fontLen));
        object.extra = r.pod<float>(); object.color = r.pod<uint32_t>();
        object.rect.right = object.rect.left + std::max(120.0f, object.extra * static_cast<float>(std::max<size_t>(object.text.size(), 6)));
        object.rect.bottom = object.rect.top + object.extra * 1.5f;
        break;
    }
    case ObjectType::Freehand: {
        const auto count = r.pod<uint32_t>();
        if (count > 1000000U) return false;
        object.points.resize(count);
        for (auto& p : object.points) { p = D2D1::Point2F(r.pod<float>(), r.pod<float>()); }
        object.width = r.pod<float>(); object.color = r.pod<uint32_t>();
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
        const float radius = object.extra * 0.5f;
        object.rect = D2D1::RectF(cx - radius, cy - radius, cx + radius, cy + radius);
        break;
    }
    case ObjectType::Loupe:
        rect(); object.extra = r.pod<float>(); object.fill = r.pod<uint8_t>() != 0; object.color = r.pod<uint32_t>(); break;
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
        if (!r.ok || type < 1 || type > 9) return false;
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
    g.history.push_back(SerializeObjects());
    g.historyPos = 0;
}

void PushHistory() {
    if (g.historyPos + 1 < g.history.size()) g.history.resize(g.historyPos + 1);
    g.history.push_back(SerializeObjects());
    g.historyPos = g.history.size() - 1;
    g.doc.dirty = true;
}

void Undo(bool redo) {
    if (redo) {
        if (g.historyPos + 1 >= g.history.size()) return;
        ++g.historyPos;
    } else {
        if (g.historyPos == 0) return;
        --g.historyPos;
    }
    DeserializeObjects(g.history[g.historyPos]);
    g.doc.dirty = true;
    InvalidateCanvas();
}

bool SaveLdl(const std::wstring& path) {
    if (g.doc.png.empty()) return false;
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
    g.doc.width = width; g.doc.height = height; g.doc.pixels = std::move(pixels); g.doc.png.assign(png.begin(), png.end());
    g.doc.originalName = name;
    g.doc.created.dwLowDateTime = created.LowPart; g.doc.created.dwHighDateTime = created.HighPart;
    g.doc.modified.dwLowDateTime = modified.LowPart; g.doc.modified.dwHighDateTime = modified.HighPart;
    if (!DeserializeObjects(r.data.subspan(r.pos))) return false;
    g.doc.projectPath = path; g.doc.dirty = false; g.cropActive = false; g.zoom = 1.0f;
    ResetHistory();
    return true;
}

D2D1_RECT_F Bounds(const Object& object) {
    if (!object.points.empty()) {
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
    if (object.type == ObjectType::Arrow && object.points.size() >= 2) return DistanceToSegment(p, object.points[0], object.points[1]) <= object.width + 6.0f;
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
    object.rect.left += dx; object.rect.right += dx; object.rect.top += dy; object.rect.bottom += dy;
    for (auto& p : object.points) { p.x += dx; p.y += dy; }
}

int HitHandle(const Object& object, D2D1_POINT_2F p) {
    const float radius = 8.0f / g.zoom;
    if (object.type == ObjectType::Arrow && object.points.size() >= 2) {
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

void ResizeObject(Object& object, int handle, D2D1_POINT_2F p) {
    if (object.type == ObjectType::Arrow && object.points.size() >= 2) { object.points[handle] = p; return; }
    const auto old = Bounds(object);
    auto next = old;
    if (handle == 0 || handle == 2) next.left = p.x; else next.right = p.x;
    if (handle == 0 || handle == 1) next.top = p.y; else next.bottom = p.y;
    next = NormalizeRect(next);
    if (next.right - next.left < 2.0f || next.bottom - next.top < 2.0f) return;
    if (object.type == ObjectType::Text) {
        object.rect = next; object.extra = std::clamp(next.bottom - next.top, 10.0f, 256.0f); return;
    }
    if (object.type == ObjectType::NumberStamp) {
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

void FitOrigin() {
    RECT rect{}; GetClientRect(g.canvas, &rect);
    g.origin.x = std::max(16.0f, (static_cast<float>(rect.right) - g.doc.width * g.zoom) / 2.0f);
    g.origin.y = std::max(16.0f, (static_cast<float>(rect.bottom) - g.doc.height * g.zoom) / 2.0f);
}

void ZoomBy(float factor) { g.zoom = std::clamp(g.zoom * factor, 0.1f, 8.0f); FitOrigin(); InvalidateCanvas(); }

D2D1_POINT_2F CanvasToImage(LPARAM value) {
    return D2D1::Point2F((static_cast<float>(GET_X_LPARAM(value)) - g.origin.x) / g.zoom, (static_cast<float>(GET_Y_LPARAM(value)) - g.origin.y) / g.zoom);
}

bool ApplyImageResize(float scale) {
    if (g.doc.pixels.empty()) return false;
    const UINT oldWidth = g.doc.width, oldHeight = g.doc.height;
    const UINT newWidth = std::max(1U, static_cast<UINT>(std::lround(static_cast<double>(oldWidth) * scale)));
    const UINT newHeight = std::max(1U, static_cast<UINT>(std::lround(static_cast<double>(oldHeight) * scale)));
    ComPtr<IWICBitmap> source;
    HRESULT hr = g.wic->CreateBitmapFromMemory(oldWidth, oldHeight, GUID_WICPixelFormat32bppPBGRA, oldWidth * 4U,
        static_cast<UINT>(g.doc.pixels.size()), g.doc.pixels.data(), &source);
    ComPtr<IWICBitmapScaler> scaler;
    if (SUCCEEDED(hr)) hr = g.wic->CreateBitmapScaler(&scaler);
    if (SUCCEEDED(hr)) hr = scaler->Initialize(source.Get(), newWidth, newHeight, WICBitmapInterpolationModeFant);
    std::vector<uint8_t> pixels;
    UINT width{}, height{};
    if (SUCCEEDED(hr)) hr = DecodeSource(scaler.Get(), pixels, width, height);
    if (FAILED(hr)) return false;
    g.doc.width = width; g.doc.height = height; g.doc.pixels = std::move(pixels);
    for (auto& object : g.doc.objects) {
        object.rect.left *= scale; object.rect.right *= scale; object.rect.top *= scale; object.rect.bottom *= scale;
        for (auto& p : object.points) { p.x *= scale; p.y *= scale; }
        if (object.type == ObjectType::Text || object.type == ObjectType::NumberStamp) object.extra *= scale;
    }
    EncodePixels(g.doc.pixels, g.doc.width, g.doc.height, GUID_ContainerFormatPng, g.doc.png);
    g.cropActive = false;
    PushHistory();
    FitOrigin();
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
    PushHistory();
    FitOrigin();
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
    if (object.type == ObjectType::Arrow && object.points.size() >= 2) {
        for (auto p : object.points) target->FillRectangle(D2D1::RectF(p.x - 4, p.y - 4, p.x + 4, p.y + 4), brush.Get());
        return;
    }
    const auto b = Bounds(object);
    target->DrawRectangle(b, brush.Get(), 1.0f);
    for (auto p : {D2D1::Point2F(b.left, b.top), D2D1::Point2F(b.right, b.top), D2D1::Point2F(b.left, b.bottom), D2D1::Point2F(b.right, b.bottom)}) {
        target->FillRectangle(D2D1::RectF(p.x - 3, p.y - 3, p.x + 3, p.y + 3), brush.Get());
    }
}

void DrawLoupe(ID2D1RenderTarget* target, const Object& object) {
    if (g.doc.pixels.empty()) return;
    const auto r = NormalizeRect(object.rect);
    const float width = r.right - r.left, height = r.bottom - r.top;
    if (width < 2.0f || height < 2.0f) return;
    const float zoom = std::clamp(object.extra, 1.5f, 3.0f);
    const float cx = (r.left + r.right) * 0.5f, cy = (r.top + r.bottom) * 0.5f;
    const D2D1_RECT_F source = D2D1::RectF(cx - width / (2.0f * zoom), cy - height / (2.0f * zoom), cx + width / (2.0f * zoom), cy + height / (2.0f * zoom));
    ComPtr<ID2D1Bitmap> bitmap;
    const D2D1_BITMAP_PROPERTIES props{D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f};
    if (FAILED(target->CreateBitmap(D2D1::SizeU(g.doc.width, g.doc.height), g.doc.pixels.data(), g.doc.width * 4U, props, &bitmap))) return;
    ComPtr<ID2D1SolidColorBrush> outline;
    target->CreateSolidColorBrush(ColorF(object.color), &outline);
    if (object.fill) {
        const D2D1_ELLIPSE ellipse{{cx, cy}, width * 0.5f, height * 0.5f};
        target->PushAxisAlignedClip(r, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        target->DrawBitmap(bitmap.Get(), r, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, source);
        target->PopAxisAlignedClip();
        target->DrawEllipse(ellipse, outline.Get(), 2.0f);
    } else {
        target->DrawBitmap(bitmap.Get(), r, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, source);
        target->DrawRectangle(r, outline.Get(), 2.0f);
    }
}

void DrawObject(ID2D1RenderTarget* target, const Object& object, bool selected, bool exportMode) {
    if (!object.visible) return;
    ComPtr<ID2D1SolidColorBrush> brush;
    target->CreateSolidColorBrush(ColorF(object.color), &brush);
    const auto r = NormalizeRect(object.rect);
    switch (object.type) {
    case ObjectType::Arrow:
        if (object.points.size() >= 2) {
            const auto a = object.points[0], b = object.points[1];
            target->DrawLine(a, b, brush.Get(), object.width);
            const float angle = std::atan2(b.y - a.y, b.x - a.x);
            const D2D1_POINT_2F p1{b.x - object.extra * std::cos(angle - kPi / 6.0f), b.y - object.extra * std::sin(angle - kPi / 6.0f)};
            const D2D1_POINT_2F p2{b.x - object.extra * std::cos(angle + kPi / 6.0f), b.y - object.extra * std::sin(angle + kPi / 6.0f)};
            target->DrawLine(b, p1, brush.Get(), object.width);
            target->DrawLine(b, p2, brush.Get(), object.width);
        }
        break;
    case ObjectType::Rectangle:
        target->DrawRectangle(r, brush.Get(), object.width); break;
    case ObjectType::Ellipse: {
        const D2D1_ELLIPSE ellipse{{(r.left + r.right) * 0.5f, (r.top + r.bottom) * 0.5f}, (r.right - r.left) * 0.5f, (r.bottom - r.top) * 0.5f};
        target->DrawEllipse(ellipse, brush.Get(), object.width); break;
    }
    case ObjectType::Text: {
        ComPtr<IDWriteTextFormat> format;
        if (SUCCEEDED(g.dwrite->CreateTextFormat(object.font.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, object.extra, L"ja-JP", &format))) {
            format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            target->DrawTextW(object.text.c_str(), static_cast<UINT32>(object.text.size()), format.Get(), r, brush.Get());
        }
        break;
    }
    case ObjectType::Freehand:
        for (size_t i = 1; i < object.points.size(); ++i) target->DrawLine(object.points[i - 1], object.points[i], brush.Get(), object.width);
        break;
    case ObjectType::BlurRegion:
    case ObjectType::MosaicRegion: {
        auto pixels = EffectPixels(object);
        ComPtr<ID2D1Bitmap> bitmap;
        const D2D1_BITMAP_PROPERTIES props{D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f};
        if (SUCCEEDED(target->CreateBitmap(D2D1::SizeU(g.doc.width, g.doc.height), pixels.data(), g.doc.width * 4U, props, &bitmap))) {
            target->PushAxisAlignedClip(r, D2D1_ANTIALIAS_MODE_ALIASED);
            target->DrawBitmap(bitmap.Get());
            target->PopAxisAlignedClip();
        }
        if (!exportMode) target->DrawRectangle(r, brush.Get(), 1.0f);
        break;
    }
    case ObjectType::NumberStamp: {
        const D2D1_ELLIPSE ellipse{{(r.left + r.right) * 0.5f, (r.top + r.bottom) * 0.5f}, (r.right - r.left) * 0.5f, (r.bottom - r.top) * 0.5f};
        target->FillEllipse(ellipse, brush.Get());
        ComPtr<ID2D1SolidColorBrush> white;
        target->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &white);
        ComPtr<IDWriteTextFormat> format;
        if (SUCCEEDED(g.dwrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, object.extra * 0.55f, L"ja-JP", &format))) {
            format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            target->DrawTextW(object.text.c_str(), static_cast<UINT32>(object.text.size()), format.Get(), r, white.Get());
        }
        break;
    }
    case ObjectType::Loupe:
        DrawLoupe(target, object); break;
    }
    if (selected && !exportMode) DrawSelectionHandles(target, object);
}

void DrawDocument(ID2D1RenderTarget* target, bool exportMode) {
    if (g.doc.pixels.empty()) return;
    ComPtr<ID2D1Bitmap> bitmap;
    const D2D1_BITMAP_PROPERTIES props{D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f};
    if (SUCCEEDED(target->CreateBitmap(D2D1::SizeU(g.doc.width, g.doc.height), g.doc.pixels.data(), g.doc.width * 4U, props, &bitmap))) target->DrawBitmap(bitmap.Get());
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

bool ExportImage(const std::wstring& path) {
    std::vector<uint8_t> pixels, encoded;
    if (FAILED(RenderFlattened(pixels))) return false;
    const auto ext = std::filesystem::path(path).extension().wstring();
    const GUID* format = &GUID_ContainerFormatPng;
    if (_wcsicmp(ext.c_str(), L".bmp") == 0) format = &GUID_ContainerFormatBmp;
    else if (_wcsicmp(ext.c_str(), L".jpg") == 0 || _wcsicmp(ext.c_str(), L".jpeg") == 0) format = &GUID_ContainerFormatJpeg;
    if (FAILED(EncodePixels(pixels, g.doc.width, g.doc.height, *format, encoded))) return false;
    std::ofstream file(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
    return !!file;
}

std::optional<std::wstring> FileDialog(HWND owner, bool save, const wchar_t* filter, const wchar_t* defaultExt) {
    std::array<wchar_t, 32768> path{};
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner = owner; dialog.lpstrFilter = filter; dialog.lpstrFile = path.data(); dialog.nMaxFile = static_cast<DWORD>(path.size()); dialog.lpstrDefExt = defaultExt;
    dialog.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog)) return std::wstring(path.data());
    return std::nullopt;
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

bool TryLoadIconFont() {
    wchar_t modulePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    std::filesystem::path baseDir = std::filesystem::path(modulePath).parent_path();
    std::array<std::filesystem::path, 5> candidates{
        baseDir / "assets" / "fonts" / "materialdesignicons-webfont.ttf",
        baseDir.parent_path() / "assets" / "fonts" / "materialdesignicons-webfont.ttf",
        baseDir.parent_path().parent_path() / "assets" / "fonts" / "materialdesignicons-webfont.ttf",
        std::filesystem::current_path() / "assets" / "fonts" / "materialdesignicons-webfont.ttf",
        std::filesystem::current_path().parent_path() / "assets" / "fonts" / "materialdesignicons-webfont.ttf"
    };
    for (const auto& fontPath : candidates) {
        if (!std::filesystem::exists(fontPath)) continue;
        if (AddFontResourceExW(fontPath.c_str(), FR_PRIVATE, nullptr) == 0) continue;
        g.iconFont = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 DEFAULT_PITCH | FF_DONTCARE, L"Material Design Icons");
        g.iconFontLoaded = g.iconFont != nullptr;
        if (g.iconFontLoaded) return true;
    }
    return false;
}

void SetToolbarButtonText(HWND button, const ToolButton& entry) {
    SetWindowTextW(button, g.iconFontLoaded ? entry.glyph : entry.label);
    SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(g.iconFontLoaded ? g.iconFont : g.uiFont), TRUE);
}

void AddTooltip(HWND owner, HWND target, const wchar_t* text) {
    TOOLINFOW info{};
    info.cbSize = sizeof(info); info.uFlags = TTF_IDISHWND | TTF_SUBCLASS; info.hwnd = owner; info.uId = reinterpret_cast<UINT_PTR>(target);
    info.lpszText = const_cast<wchar_t*>(text);
    SendMessageW(g.tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
}

PropertyKind CurrentPropertyKind() {
    if (g.selected >= 0 && g.selected < static_cast<int>(g.doc.objects.size())) {
        switch (g.doc.objects[g.selected].type) {
        case ObjectType::Text: return PropertyKind::Text;
        case ObjectType::NumberStamp: return PropertyKind::Number;
        case ObjectType::Arrow:
        case ObjectType::Rectangle:
        case ObjectType::Ellipse:
        case ObjectType::Freehand: return PropertyKind::Stroke;
        case ObjectType::BlurRegion: return PropertyKind::Blur;
        case ObjectType::MosaicRegion: return PropertyKind::Mosaic;
        case ObjectType::Loupe: return PropertyKind::Loupe;
        }
    }
    switch (g.tool) {
    case Tool::Text: return PropertyKind::Text;
    case Tool::Number: return PropertyKind::Number;
    case Tool::Arrow:
    case Tool::Rectangle:
    case Tool::Ellipse:
    case Tool::Freehand: return PropertyKind::Stroke;
    case Tool::Blur: return PropertyKind::Blur;
    case Tool::Mosaic: return PropertyKind::Mosaic;
    case Tool::Loupe: return PropertyKind::Loupe;
    default: return PropertyKind::None;
    }
}

void ShowPropertyControl(HWND hwnd, bool visible) {
    ShowWindow(hwnd, visible ? SW_SHOW : SW_HIDE);
    EnableWindow(hwnd, visible);
}

void ConfigureComboValues(HWND combo, std::span<const wchar_t* const> items) {
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (const auto* item : items) SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
}

void ApplySelectionToProperties() {
    static constexpr const wchar_t* kFonts[] = {L"Segoe UI", L"Yu Gothic UI", L"Meiryo", L"Arial"};
    static constexpr const wchar_t* kLoupeZooms[] = {L"1.5x", L"2x", L"3x"};
    static constexpr const wchar_t* kLoupeShapes[] = {L"Circle", L"Rectangle"};
    for (int i = 0; i < 3; ++i) {
        ShowPropertyControl(g.propLabel[i], false);
        ShowPropertyControl(g.propValue[i], false);
    }
    ShowPropertyControl(g.propColor, false);
    switch (CurrentPropertyKind()) {
    case PropertyKind::Text:
        ConfigureComboValues(g.propValue[0], kFonts);
        SetWindowTextW(g.propLabel[0], L"Font"); SetWindowTextW(g.propValue[0], g.textFont.c_str());
        SetWindowTextW(g.propLabel[1], L"Size"); SetWindowTextW(g.propValue[1], std::to_wstring(static_cast<int>(g.textSize)).c_str());
        ShowPropertyControl(g.propLabel[0], true); ShowPropertyControl(g.propValue[0], true);
        ShowPropertyControl(g.propLabel[1], true); ShowPropertyControl(g.propValue[1], true);
        ShowPropertyControl(g.propColor, true);
        break;
    case PropertyKind::Number:
        SetWindowTextW(g.propLabel[0], L"Diameter"); SetWindowTextW(g.propValue[0], std::to_wstring(static_cast<int>(g.numberDiameter)).c_str());
        ShowPropertyControl(g.propLabel[0], true); ShowPropertyControl(g.propValue[0], true); ShowPropertyControl(g.propColor, true);
        break;
    case PropertyKind::Stroke:
        SetWindowTextW(g.propLabel[0], L"Width"); SetWindowTextW(g.propValue[0], std::to_wstring(static_cast<int>(g.lineWidth)).c_str());
        ShowPropertyControl(g.propLabel[0], true); ShowPropertyControl(g.propValue[0], true); ShowPropertyControl(g.propColor, true);
        break;
    case PropertyKind::Blur:
        SetWindowTextW(g.propLabel[0], L"Blur"); SetWindowTextW(g.propValue[0], std::to_wstring(static_cast<int>(g.effectSize)).c_str());
        ShowPropertyControl(g.propLabel[0], true); ShowPropertyControl(g.propValue[0], true); ShowPropertyControl(g.propColor, true);
        break;
    case PropertyKind::Mosaic:
        SetWindowTextW(g.propLabel[0], L"Block"); SetWindowTextW(g.propValue[0], std::to_wstring(static_cast<int>(g.effectSize)).c_str());
        ShowPropertyControl(g.propLabel[0], true); ShowPropertyControl(g.propValue[0], true); ShowPropertyControl(g.propColor, true);
        break;
    case PropertyKind::Loupe:
        ConfigureComboValues(g.propValue[0], kLoupeZooms);
        ConfigureComboValues(g.propValue[1], kLoupeShapes);
        SetWindowTextW(g.propLabel[0], L"Zoom"); SetWindowTextW(g.propValue[0], g.loupeZoom == 1.5f ? L"1.5x" : g.loupeZoom == 3.0f ? L"3x" : L"2x");
        SetWindowTextW(g.propLabel[1], L"Shape"); SetWindowTextW(g.propValue[1], g.loupeCircle ? L"Circle" : L"Rectangle");
        ShowPropertyControl(g.propLabel[0], true); ShowPropertyControl(g.propValue[0], true);
        ShowPropertyControl(g.propLabel[1], true); ShowPropertyControl(g.propValue[1], true);
        ShowPropertyControl(g.propColor, true);
        break;
    case PropertyKind::None:
        break;
    }
}

void CommitObjectPropertyChange() {
    if (g.selected < 0 || g.selected >= static_cast<int>(g.doc.objects.size())) return;
    auto& object = g.doc.objects[g.selected];
    switch (object.type) {
    case ObjectType::Text:
        object.font = g.textFont; object.extra = g.textSize; object.color = g.color;
        object.rect.right = object.rect.left + std::max(120.0f, object.extra * static_cast<float>(std::max<size_t>(object.text.size(), 6)));
        object.rect.bottom = object.rect.top + object.extra * 1.5f;
        break;
    case ObjectType::NumberStamp: {
        object.extra = static_cast<float>(g.numberDiameter); object.color = g.color;
        const float cx = (object.rect.left + object.rect.right) * 0.5f, cy = (object.rect.top + object.rect.bottom) * 0.5f;
        const float r = object.extra * 0.5f;
        object.rect = D2D1::RectF(cx - r, cy - r, cx + r, cy + r);
        break;
    }
    case ObjectType::Arrow:
    case ObjectType::Rectangle:
    case ObjectType::Ellipse:
    case ObjectType::Freehand:
        object.width = g.lineWidth; object.color = g.color; break;
    case ObjectType::BlurRegion:
        object.extra = static_cast<float>(g.effectSize); object.color = g.color; break;
    case ObjectType::MosaicRegion:
        object.blockSize = g.effectSize; object.color = g.color; break;
    case ObjectType::Loupe:
        object.extra = g.loupeZoom; object.fill = g.loupeCircle; object.color = g.color; break;
    }
    PushHistory();
    InvalidateCanvas();
}

void ReadPropertyControls() {
    switch (CurrentPropertyKind()) {
    case PropertyKind::Text:
        g.textFont = GetWindowString(g.propValue[0]);
        g.textSize = std::clamp(static_cast<float>(_wtof(GetWindowString(g.propValue[1]).c_str())), 8.0f, 144.0f);
        break;
    case PropertyKind::Number:
        g.numberDiameter = std::clamp(_wtoi(GetWindowString(g.propValue[0]).c_str()), 16, 200);
        break;
    case PropertyKind::Stroke:
        g.lineWidth = std::clamp(static_cast<float>(_wtof(GetWindowString(g.propValue[0]).c_str())), 1.0f, 30.0f);
        break;
    case PropertyKind::Blur:
    case PropertyKind::Mosaic:
        g.effectSize = std::clamp(_wtoi(GetWindowString(g.propValue[0]).c_str()), 2, 128);
        break;
    case PropertyKind::Loupe: {
        const auto zoom = GetWindowString(g.propValue[0]);
        g.loupeZoom = zoom == L"1.5x" ? 1.5f : zoom == L"3x" ? 3.0f : 2.0f;
        g.loupeCircle = GetWindowString(g.propValue[1]) != L"Rectangle";
        break;
    }
    case PropertyKind::None:
        break;
    }
    CommitObjectPropertyChange();
}

Object NewObject(D2D1_POINT_2F p) {
    Object object;
    object.id = g.doc.nextId++;
    object.z = static_cast<int32_t>(g.doc.objects.size());
    object.width = g.lineWidth;
    object.color = g.color;
    object.rect = D2D1::RectF(p.x, p.y, p.x, p.y);
    switch (g.tool) {
    case Tool::Arrow: object.type = ObjectType::Arrow; object.points = {p, p}; object.extra = 18.0f; break;
    case Tool::Rectangle: object.type = ObjectType::Rectangle; break;
    case Tool::Ellipse: object.type = ObjectType::Ellipse; break;
    case Tool::Freehand: object.type = ObjectType::Freehand; object.points = {p}; break;
    case Tool::Blur: object.type = ObjectType::BlurRegion; object.extra = static_cast<float>(g.effectSize); break;
    case Tool::Mosaic: object.type = ObjectType::MosaicRegion; object.blockSize = g.effectSize; break;
    case Tool::Loupe: object.type = ObjectType::Loupe; object.extra = g.loupeZoom; object.fill = g.loupeCircle; break;
    default: break;
    }
    return object;
}

void UpdateDraft(D2D1_POINT_2F p) {
    if (!g.draft) return;
    if (g.draft->type == ObjectType::Arrow) g.draft->points[1] = p;
    else if (g.draft->type == ObjectType::Freehand) g.draft->points.push_back(p);
    else { g.draft->rect.right = p.x; g.draft->rect.bottom = p.y; }
}

void AddPointObject(D2D1_POINT_2F p, ObjectType type, std::wstring text) {
    Object object;
    object.id = g.doc.nextId++;
    object.z = static_cast<int32_t>(g.doc.objects.size());
    object.type = type;
    object.color = g.color;
    object.text = std::move(text);
    if (type == ObjectType::Text) {
        object.font = g.textFont;
        object.extra = g.textSize;
        object.rect = D2D1::RectF(p.x, p.y, p.x + std::max(120.0f, object.extra * static_cast<float>(std::max<size_t>(object.text.size(), 6))), p.y + object.extra * 1.5f);
    } else {
        object.extra = static_cast<float>(g.numberDiameter);
        const float r = object.extra * 0.5f;
        object.rect = D2D1::RectF(p.x - r, p.y - r, p.x + r, p.y + r);
    }
    g.doc.objects.push_back(std::move(object));
    g.selected = static_cast<int>(g.doc.objects.size()) - 1;
    PushHistory();
    ApplySelectionToProperties();
    InvalidateCanvas();
}

void DeleteSelected() {
    if (g.selected < 0 || g.selected >= static_cast<int>(g.doc.objects.size())) return;
    g.doc.objects.erase(g.doc.objects.begin() + g.selected);
    g.selected = -1;
    PushHistory();
    ApplySelectionToProperties();
    InvalidateCanvas();
}

bool ConfirmDiscard() {
    if (!g.doc.dirty) return true;
    return MessageBoxW(g.mainWindow, L"未保存の変更があります。破棄しますか？", L"LiteDraw", MB_YESNO | MB_ICONWARNING) == IDYES;
}

void DoOpen() {
    if (!ConfirmDiscard()) return;
    auto path = FileDialog(g.mainWindow, false,
        L"Supported (*.png;*.bmp;*.jpg;*.jpeg;*.ldl)\0*.png;*.bmp;*.jpg;*.jpeg;*.ldl\0"
        L"Image (*.png;*.bmp;*.jpg;*.jpeg)\0*.png;*.bmp;*.jpg;*.jpeg\0LiteDraw (*.ldl)\0*.ldl\0\0", nullptr);
    if (!path) return;
    const bool ldl = _wcsicmp(std::filesystem::path(*path).extension().c_str(), L".ldl") == 0;
    if (!(ldl ? LoadLdl(*path) : SUCCEEDED(LoadImageFile(*path)))) { ShowError(g.mainWindow, L"ファイルを開けませんでした。"); return; }
    UpdateWindowTitle();
    FitOrigin();
    InvalidateCanvas();
}

void DoSave(bool saveAs) {
    if (g.doc.png.empty()) { ShowError(g.mainWindow, L"先に画像を開いてください。"); return; }
    std::wstring path = g.doc.projectPath;
    if (saveAs || path.empty()) {
        auto selected = FileDialog(g.mainWindow, true, L"LiteDraw Layer (*.ldl)\0*.ldl\0\0", L"ldl");
        if (!selected) return;
        path = *selected;
    }
    if (!SaveLdl(path)) ShowError(g.mainWindow, L"保存できませんでした。");
    else UpdateWindowTitle();
}

void DoExport() {
    if (g.doc.png.empty()) { ShowError(g.mainWindow, L"先に画像を開いてください。"); return; }
    auto path = FileDialog(g.mainWindow, true, L"PNG (*.png)\0*.png\0Bitmap (*.bmp)\0*.bmp\0JPEG (*.jpg;*.jpeg)\0*.jpg;*.jpeg\0\0", L"png");
    if (path && !ExportImage(*path)) ShowError(g.mainWindow, L"画像を書き出せませんでした。");
}

void ChooseColor() {
    static COLORREF custom[16]{};
    CHOOSECOLORW picker{sizeof(picker)};
    picker.hwndOwner = g.mainWindow; picker.rgbResult = RgbaToColorRef(g.color);
    picker.lpCustColors = custom; picker.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (ChooseColorW(&picker)) {
        g.color = ColorRefToRgba(picker.rgbResult);
        CommitObjectPropertyChange();
    }
}

void SetTool(Tool tool) {
    g.tool = tool;
    g.draft.reset();
    g.dragging = false;
    g.resizing = false;
    g.activeHandle = -1;
    g.selected = (tool == Tool::Select) ? g.selected : -1;
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
    InvalidateCanvas();
}

void ExecuteToolbarCommand(int id) {
    switch (id) {
    case IDC_CMD_OPEN: DoOpen(); break;
    case IDC_CMD_SAVE: DoSave(false); break;
    case IDC_CMD_SAVE_AS: DoSave(true); break;
    case IDC_CMD_EXPORT: DoExport(); break;
    case IDC_CMD_RESIZE_75: ApplyImageResize(0.75f); break;
    case IDC_CMD_RESIZE_50: ApplyImageResize(0.50f); break;
    case IDC_CMD_RESIZE_25: ApplyImageResize(0.25f); break;
    case IDC_TOOL_SELECT: SetTool(Tool::Select); break;
    case IDC_TOOL_CROP: SetTool(Tool::Crop); break;
    case IDC_TOOL_ARROW: SetTool(Tool::Arrow); break;
    case IDC_TOOL_RECT: SetTool(Tool::Rectangle); break;
    case IDC_TOOL_ELLIPSE: SetTool(Tool::Ellipse); break;
    case IDC_TOOL_FREEHAND: SetTool(Tool::Freehand); break;
    case IDC_TOOL_TEXT: SetTool(Tool::Text); break;
    case IDC_TOOL_LOUPE: SetTool(Tool::Loupe); break;
    case IDC_TOOL_BLUR: SetTool(Tool::Blur); break;
    case IDC_TOOL_MOSAIC: SetTool(Tool::Mosaic); break;
    case IDC_TOOL_NUMBER: SetTool(Tool::Number); break;
    case IDC_CMD_UNDO: Undo(false); break;
    case IDC_CMD_REDO: Undo(true); break;
    case IDC_CMD_DELETE: DeleteSelected(); break;
    case IDC_CMD_ZOOM_IN: ZoomBy(1.25f); break;
    case IDC_CMD_ZOOM_100: g.zoom = 1.0f; FitOrigin(); InvalidateCanvas(); break;
    case IDC_CMD_ZOOM_OUT: ZoomBy(0.8f); break;
    case IDC_CMD_HELP: MessageBoxW(g.mainWindow, L"LiteDraw\nCrop: Enter で確定 / Esc でキャンセル", L"Help", MB_OK | MB_ICONINFORMATION); break;
    case IDC_CMD_EXIT: SendMessageW(g.mainWindow, WM_CLOSE, 0, 0); break;
    }
}

void Layout(HWND hwnd) {
    RECT client{}; GetClientRect(hwnd, &client);
    const int canvasWidth = static_cast<int>(client.right);
    const int canvasHeight = (std::max)(1, static_cast<int>(client.bottom) - (kToolbarHeight + kPropertyHeight));
    MoveWindow(g.canvas, 0, kToolbarHeight + kPropertyHeight, canvasWidth, canvasHeight, TRUE);
    int x = 10;
    for (const auto& entry : kToolbarButtons) {
        MoveWindow(GetDlgItem(hwnd, entry.id), x, 6, kToolbarButton, kToolbarButton - 4, TRUE);
        x += kToolbarButton + kToolbarGap;
    }
    int px = 12;
    for (int i = 0; i < 3; ++i) {
        MoveWindow(g.propLabel[i], px, kToolbarHeight + 10, 54, 20, TRUE);
        MoveWindow(g.propValue[i], px + 56, kToolbarHeight + 7, 120, 120, TRUE);
        px += 190;
    }
    MoveWindow(g.propColor, client.right - 92, kToolbarHeight + 7, 80, 24, TRUE);
}

void CreateControls(HWND hwnd) {
    g.uiFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    g.tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwnd, nullptr, g.instance, nullptr);
    SendMessageW(g.tooltip, TTM_SETMAXTIPWIDTH, 0, 320);
    TryLoadIconFont();
    for (const auto& entry : kToolbarButtons) {
        HWND button = CreateWindowW(L"BUTTON", entry.label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 32, 32, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(entry.id)), g.instance, nullptr);
        SetToolbarButtonText(button, entry);
        AddTooltip(hwnd, button, entry.label);
    }
    g.canvas = CreateWindowExW(0, kCanvasClass, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 100, 100, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCanvasId)), g.instance, nullptr);
    for (int i = 0; i < 3; ++i) {
        g.propLabel[i] = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 40, 20, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROP_LABEL1 + i)), g.instance, nullptr);
        g.propValue[i] = CreateWindowW(WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWN | WS_VSCROLL, 0, 0, 120, 120, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROP_VALUE1 + i)), g.instance, nullptr);
    }
    g.propColor = CreateWindowW(L"BUTTON", L"Color", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 80, 24, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROP_COLOR)), g.instance, nullptr);
    SetTool(Tool::Select);
}

void PaintCanvas(HWND hwnd) {
    PAINTSTRUCT ps{}; BeginPaint(hwnd, &ps);
    RECT client{}; GetClientRect(hwnd, &client);
    if (!g.target) {
        g.d2d->CreateHwndRenderTarget(D2D1::RenderTargetProperties(), D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(static_cast<UINT>(client.right), static_cast<UINT>(client.bottom))), &g.target);
    }
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
        if (g.target->EndDraw() == D2DERR_RECREATE_TARGET) g.target.Reset();
    }
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK CanvasProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_PAINT: PaintCanvas(hwnd); return 0;
    case WM_SIZE: if (g.target) g.target->Resize(D2D1::SizeU(LOWORD(lParam), HIWORD(lParam))); FitOrigin(); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEWHEEL: ZoomBy(GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 1.2f : 1.0f / 1.2f); return 0;
    case WM_MBUTTONDOWN: g.panning = true; g.panLast = POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}; SetCapture(hwnd); return 0;
    case WM_MBUTTONUP: if (g.panning) { g.panning = false; ReleaseCapture(); } return 0;
    case WM_LBUTTONDOWN: {
        SetFocus(hwnd);
        if (g.doc.pixels.empty()) return 0;
        const auto p = CanvasToImage(lParam); g.down = g.last = p;
        if (g.tool == Tool::Crop) {
            const int handle = g.cropActive ? HitCropHandle(p) : -1;
            if (handle >= 0) { g.activeCropHandle = handle; g.resizingCrop = true; }
            else if (g.cropActive && PointInRect(g.cropRect, p)) { g.movingCrop = true; }
            else { g.cropActive = true; g.cropRect = D2D1::RectF(p.x, p.y, p.x, p.y); g.resizingCrop = true; g.activeCropHandle = 4; }
            g.dragging = true; SetCapture(hwnd); return 0;
        }
        if (g.tool == Tool::Select) {
            if (g.selected >= 0 && g.selected < static_cast<int>(g.doc.objects.size())) {
                const int handle = HitHandle(g.doc.objects[g.selected], p);
                if (handle >= 0) { g.activeHandle = handle; g.resizing = true; g.dragging = true; SetCapture(hwnd); return 0; }
            }
            g.selected = -1;
            int32_t topZ = INT32_MIN;
            for (size_t i = 0; i < g.doc.objects.size(); ++i) if (g.doc.objects[i].z >= topZ && HitTest(g.doc.objects[i], p)) { topZ = g.doc.objects[i].z; g.selected = static_cast<int>(i); }
            if (g.selected >= 0) { g.dragging = true; SetCapture(hwnd); ApplySelectionToProperties(); }
            InvalidateCanvas();
            return 0;
        }
        if (g.tool == Tool::Text || g.tool == Tool::Number) {
            const auto value = Prompt(g.mainWindow, g.tool == Tool::Text ? L"Text" : L"Number", g.tool == Tool::Text ? L"" : L"1");
            if (value && !value->empty()) AddPointObject(p, g.tool == Tool::Text ? ObjectType::Text : ObjectType::NumberStamp, *value);
            return 0;
        }
        g.draft = NewObject(p); g.dragging = true; SetCapture(hwnd); return 0;
    }
    case WM_MOUSEMOVE:
        if (g.panning) {
            const POINT now{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            g.origin.x += static_cast<float>(now.x - g.panLast.x);
            g.origin.y += static_cast<float>(now.y - g.panLast.y);
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
            } else if (g.tool == Tool::Select && g.selected >= 0) {
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
        if (g.tool == Tool::Select && g.selected >= 0) {
            if (g.last.x != g.down.x || g.last.y != g.down.y) PushHistory();
            g.dragging = false; g.resizing = false; g.activeHandle = -1; InvalidateCanvas(); return 0;
        }
        if (g.draft) {
            const auto b = Bounds(*g.draft);
            if ((b.right - b.left >= 2.0f && b.bottom - b.top >= 2.0f) || g.draft->type == ObjectType::Arrow || g.draft->type == ObjectType::Freehand) {
                g.doc.objects.push_back(std::move(*g.draft));
                g.selected = static_cast<int>(g.doc.objects.size()) - 1;
                PushHistory();
                ApplySelectionToProperties();
            }
            g.draft.reset();
        }
        g.dragging = false;
        InvalidateCanvas();
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_DELETE) { DeleteSelected(); return 0; }
        if (wParam == VK_ESCAPE) {
            if (g.dragging) ReleaseCapture();
            g.dragging = false; g.resizing = false; g.resizingCrop = false; g.movingCrop = false; g.draft.reset();
            if (g.tool == Tool::Crop) g.cropActive = false;
            InvalidateCanvas(); return 0;
        }
        if (g.tool == Tool::Crop && wParam == VK_RETURN) { ApplyCrop(); return 0; }
        if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == 'Z') { Undo(false); return 0; }
        if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == 'Y') { Undo(true); return 0; }
        if (g.selected >= 0 && (wParam == VK_LEFT || wParam == VK_RIGHT || wParam == VK_UP || wParam == VK_DOWN)) {
            const float amount = (GetKeyState(VK_SHIFT) & 0x8000) ? 10.0f : 1.0f;
            MoveObject(g.doc.objects[g.selected], wParam == VK_LEFT ? -amount : wParam == VK_RIGHT ? amount : 0.0f,
                wParam == VK_UP ? -amount : wParam == VK_DOWN ? amount : 0.0f);
            PushHistory(); InvalidateCanvas(); return 0;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK MainProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: g.mainWindow = hwnd; CreateControls(hwnd); Layout(hwnd); return 0;
    case WM_SIZE: Layout(hwnd); return 0;
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        if ((id >= IDC_TOOLBAR_FIRST && id <= IDC_TOOLBAR_LAST) || id == IDC_PROP_COLOR) {
            if (id == IDC_PROP_COLOR) ChooseColor();
            else ExecuteToolbarCommand(id);
            SetFocus(g.canvas);
            return 0;
        }
        if (id >= IDC_PROP_VALUE1 && id <= IDC_PROP_VALUE3) {
            if (HIWORD(wParam) == CBN_SELCHANGE || HIWORD(wParam) == CBN_EDITCHANGE || HIWORD(wParam) == CBN_KILLFOCUS || HIWORD(wParam) == EN_KILLFOCUS) {
                ReadPropertyControls();
                return 0;
            }
        }
        return 0;
    }
    case WM_CLOSE: if (ConfirmDiscard()) DestroyWindow(hwnd); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool RegisterClasses() {
    WNDCLASSEXW mainClass{sizeof(mainClass)};
    mainClass.style = CS_HREDRAW | CS_VREDRAW; mainClass.lpfnWndProc = MainProc; mainClass.hInstance = g.instance;
    mainClass.hCursor = LoadCursorW(nullptr, IDC_ARROW); mainClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1); mainClass.lpszClassName = kMainClass;
    if (!RegisterClassExW(&mainClass)) return false;
    WNDCLASSEXW canvasClass{sizeof(canvasClass)};
    canvasClass.style = CS_HREDRAW | CS_VREDRAW; canvasClass.lpfnWndProc = CanvasProc; canvasClass.hInstance = g.instance;
    canvasClass.hCursor = LoadCursorW(nullptr, IDC_CROSS); canvasClass.lpszClassName = kCanvasClass;
    if (!RegisterClassExW(&canvasClass)) return false;
    WNDCLASSEXW promptClass{sizeof(promptClass)};
    promptClass.lpfnWndProc = PromptProc; promptClass.hInstance = g.instance; promptClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    promptClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1); promptClass.lpszClassName = kPromptClass;
    return RegisterClassExW(&promptClass) != 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int showCommand) {
    g.instance = instance;
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES};
    InitCommonControlsEx(&controls);
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, g.d2d.GetAddressOf());
    if (SUCCEEDED(hr)) hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(g.dwrite.GetAddressOf()));
    if (SUCCEEDED(hr)) hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g.wic));
    if (FAILED(hr) || !RegisterClasses()) {
        ShowError(nullptr, L"LiteDraw を初期化できませんでした。");
        CoUninitialize();
        return 1;
    }
    g.mainWindow = CreateWindowExW(0, kMainClass, L"LiteDraw", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1320, 860, nullptr, nullptr, instance, nullptr);
    if (!g.mainWindow) { CoUninitialize(); return 1; }
    ShowWindow(g.mainWindow, showCommand);
    UpdateWindow(g.mainWindow);
    if (commandLine && *commandLine) {
        std::wstring path = commandLine;
        if (path.size() >= 2 && path.front() == L'"' && path.back() == L'"') path = path.substr(1, path.size() - 2);
        const bool ldl = _wcsicmp(std::filesystem::path(path).extension().c_str(), L".ldl") == 0;
        if (ldl ? LoadLdl(path) : SUCCEEDED(LoadImageFile(path))) {
            UpdateWindowTitle();
            FitOrigin();
            InvalidateCanvas();
        }
    }
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (g.iconFont) DeleteObject(g.iconFont);
    CoUninitialize();
    return static_cast<int>(message.wParam);
}
