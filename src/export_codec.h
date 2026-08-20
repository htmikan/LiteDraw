#pragma once

#include <cstdint>
#include <vector>

// export_codec.cpp — 書き出し時の軽量エンコード。詳細は docs/FUNCTIONS.md を参照。

// BGRA ピクセル列を libjpeg-turbo で JPEG エンコード（quality 55, 4:2:0, optimize_coding）
bool EncodeJpegTurbo(const std::vector<uint8_t>& pixels, uint32_t width, uint32_t height, std::vector<uint8_t>& output);

// BGRA ピクセル列を libdeflate で可逆 PNG エンコード
bool EncodePngLibdeflate(const std::vector<uint8_t>& pixels, uint32_t width, uint32_t height, std::vector<uint8_t>& output);
