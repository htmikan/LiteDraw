#include "export_codec.h"

#include <libdeflate.h>
#include <jpeglib.h>

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

// --- PNG チャンク組み立てヘルパー ---

// PNG チャンク末尾に付与する CRC32 を計算する。
uint32_t Crc32(const uint8_t* data, size_t len) {
    static std::array<uint32_t, 256> table{};
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320U ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < len; ++i) crc = table[(crc ^ data[i]) & 0xFFU] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFU;
}

void AppendU32BE(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFFU));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFFU));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFFU));
    out.push_back(static_cast<uint8_t>(value & 0xFFU));
}

void WritePngChunk(std::vector<uint8_t>& out, const char type[4], const uint8_t* data, uint32_t len) {
    AppendU32BE(out, len);
    out.insert(out.end(), type, type + 4);
    if (len > 0 && data) out.insert(out.end(), data, data + len);
    std::vector<uint8_t> crcInput;
    crcInput.insert(crcInput.end(), type, type + 4);
    if (len > 0 && data) crcInput.insert(crcInput.end(), data, data + len);
    AppendU32BE(out, Crc32(crcInput.data(), crcInput.size()));
}

} // namespace

bool EncodeJpegTurbo(const std::vector<uint8_t>& pixels, uint32_t width, uint32_t height, std::vector<uint8_t>& output) {
    if (width == 0 || height == 0 || pixels.size() < static_cast<size_t>(width) * height * 4U) return false;

    jpeg_compress_struct cinfo{};
    jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);

    unsigned char* memBuffer = nullptr;
    unsigned long memSize = 0;
    jpeg_mem_dest(&cinfo, &memBuffer, &memSize);

    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    cinfo.comp_info[0].h_samp_factor = 2;
    cinfo.comp_info[0].v_samp_factor = 2;
    cinfo.comp_info[1].h_samp_factor = 1;
    cinfo.comp_info[1].v_samp_factor = 1;
    cinfo.comp_info[2].h_samp_factor = 1;
    cinfo.comp_info[2].v_samp_factor = 1;
    jpeg_set_quality(&cinfo, 55, TRUE);
    cinfo.optimize_coding = TRUE;

    jpeg_start_compress(&cinfo, TRUE);

    std::vector<uint8_t> row(width * 3U);
    while (cinfo.next_scanline < cinfo.image_height) {
        const auto y = cinfo.next_scanline;
        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t* p = pixels.data() + (static_cast<size_t>(y) * width + x) * 4U;
            row[x * 3U + 0] = p[2];
            row[x * 3U + 1] = p[1];
            row[x * 3U + 2] = p[0];
        }
        JSAMPROW scanline = row.data();
        if (jpeg_write_scanlines(&cinfo, &scanline, 1) != 1) {
            jpeg_destroy_compress(&cinfo);
            if (memBuffer) free(memBuffer);
            return false;
        }
    }

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);

    if (!memBuffer || memSize == 0) {
        if (memBuffer) free(memBuffer);
        return false;
    }
    output.assign(memBuffer, memBuffer + memSize);
    free(memBuffer);
    return true;
}

namespace {

// --- PNG 適応フィルタ（スキャンライン単位で最適なフィルタ種別を選択） ---

uint8_t PaethPredictor(uint8_t a, uint8_t b, uint8_t c) {
    const int p = static_cast<int>(a) + static_cast<int>(b) - static_cast<int>(c);
    const int pa = std::abs(p - static_cast<int>(a));
    const int pb = std::abs(p - static_cast<int>(b));
    const int pc = std::abs(p - static_cast<int>(c));
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

void FilterScanline(uint8_t type, const uint8_t* row, const uint8_t* prev, uint8_t* dest, uint32_t bytesPerRow, int bpp) {
    dest[0] = type;
    for (uint32_t i = 0; i < bytesPerRow; ++i) {
        const uint8_t x = row[i];
        const uint8_t a = (i >= static_cast<uint32_t>(bpp)) ? row[i - static_cast<uint32_t>(bpp)] : 0;
        const uint8_t b = prev ? prev[i] : 0;
        const uint8_t c = (prev && i >= static_cast<uint32_t>(bpp)) ? prev[i - static_cast<uint32_t>(bpp)] : 0;
        uint8_t filtered = x;
        switch (type) {
        case 1: filtered = static_cast<uint8_t>(x - a); break;
        case 2: filtered = static_cast<uint8_t>(x - b); break;
        case 3: filtered = static_cast<uint8_t>(x - ((static_cast<unsigned>(a) + static_cast<unsigned>(b)) / 2U)); break;
        case 4: filtered = static_cast<uint8_t>(x - PaethPredictor(a, b, c)); break;
        default: break;
        }
        dest[1 + i] = filtered;
    }
}

uint32_t FilterHeuristicScore(const uint8_t* filtered, uint32_t bytesPerRow) {
    uint32_t score = 0;
    for (uint32_t i = 0; i < bytesPerRow; ++i) score += static_cast<uint32_t>(std::abs(static_cast<int8_t>(filtered[1 + i])));
    return score;
}

} // namespace

bool EncodePngLibdeflate(const std::vector<uint8_t>& pixels, uint32_t width, uint32_t height, std::vector<uint8_t>& output) {
    if (width == 0 || height == 0 || pixels.size() < static_cast<size_t>(width) * height * 4U) return false;

    bool opaque = true;
    for (size_t i = 3; i < pixels.size(); i += 4) {
        if (pixels[i] != 255) { opaque = false; break; }
    }
    const int bpp = opaque ? 3 : 4;
    const uint32_t bytesPerRow = width * static_cast<uint32_t>(bpp);

    std::vector<uint8_t> rgbaRow(bytesPerRow);
    std::vector<uint8_t> prevRow(bytesPerRow, 0);
    std::vector<uint8_t> candidate(bytesPerRow + 1);
    std::vector<uint8_t> best(bytesPerRow + 1);
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(height) * (bytesPerRow + 1U));

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t* p = pixels.data() + (static_cast<size_t>(y) * width + x) * 4U;
            const size_t o = static_cast<size_t>(x) * static_cast<size_t>(bpp);
            rgbaRow[o + 0] = p[2];
            rgbaRow[o + 1] = p[1];
            rgbaRow[o + 2] = p[0];
            if (!opaque) rgbaRow[o + 3] = p[3];
        }
        const uint8_t* prev = (y == 0) ? nullptr : prevRow.data();
        uint32_t bestScore = ~0U;
        for (uint8_t type = 0; type <= 4; ++type) {
            FilterScanline(type, rgbaRow.data(), prev, candidate.data(), bytesPerRow, bpp);
            const uint32_t score = FilterHeuristicScore(candidate.data(), bytesPerRow);
            if (score < bestScore) {
                bestScore = score;
                best.swap(candidate);
            }
        }
        raw.insert(raw.end(), best.begin(), best.end());
        prevRow.swap(rgbaRow);
        if (y + 1 < height) rgbaRow.assign(bytesPerRow, 0);
    }

    libdeflate_compressor* compressor = libdeflate_alloc_compressor(12);
    if (!compressor) compressor = libdeflate_alloc_compressor(9);
    if (!compressor) return false;

    const size_t bound = libdeflate_zlib_compress_bound(compressor, raw.size());
    std::vector<uint8_t> compressed(bound);
    const size_t compressedSize = libdeflate_zlib_compress(compressor, raw.data(), raw.size(), compressed.data(), compressed.size());
    libdeflate_free_compressor(compressor);
    if (compressedSize == 0 || compressedSize > 0xFFFFFFFFULL) return false;
    compressed.resize(compressedSize);

    uint8_t ihdr[13]{};
    ihdr[0] = static_cast<uint8_t>((width >> 24) & 0xFFU);
    ihdr[1] = static_cast<uint8_t>((width >> 16) & 0xFFU);
    ihdr[2] = static_cast<uint8_t>((width >> 8) & 0xFFU);
    ihdr[3] = static_cast<uint8_t>(width & 0xFFU);
    ihdr[4] = static_cast<uint8_t>((height >> 24) & 0xFFU);
    ihdr[5] = static_cast<uint8_t>((height >> 16) & 0xFFU);
    ihdr[6] = static_cast<uint8_t>((height >> 8) & 0xFFU);
    ihdr[7] = static_cast<uint8_t>(height & 0xFFU);
    ihdr[8] = 8;
    ihdr[9] = opaque ? 2 : 6; // RGB or RGBA
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;

    output.clear();
    const uint8_t signature[8]{137, 80, 78, 71, 13, 10, 26, 10};
    output.insert(output.end(), signature, signature + 8);
    WritePngChunk(output, "IHDR", ihdr, 13);
    WritePngChunk(output, "IDAT", compressed.data(), static_cast<uint32_t>(compressed.size()));
    WritePngChunk(output, "IEND", nullptr, 0);
    return true;
}
