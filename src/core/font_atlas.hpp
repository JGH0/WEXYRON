/**
 * @file font_atlas.hpp
 * @brief Built-in 3x5 bitmap font, baked into a texture-array layer
 *
 * The renderer draws UI text from this atlas instead of one cube per glyph
 * pixel. The glyph shapes are identical to the original drawText()
 * implementation, so the UI looks unchanged.
 */

#ifndef FONT_ATLAS_HPP
#define FONT_ATLAS_HPP

#include <cstdint>
#include <cstring>
#include <cctype>

namespace font_atlas {

/// Atlas grid: 16 columns x 7 rows of 3x5 cells (with 1px padding between).
inline constexpr int kCellW = 3;
inline constexpr int kCellH = 5;
inline constexpr int kCols = 16;
inline constexpr int kRows = 7;
inline constexpr int kAtlasW = kCols * (kCellW + 1);
inline constexpr int kAtlasH = kRows * (kCellH + 1);
inline constexpr int kGlyphCount = 98; // ASCII 32..127

/// 15-bit pattern for one printable character (0 = blank).
inline uint16_t glyphPattern(char c) {
    switch (std::toupper(c)) {
        case 'A': return 0b010101111101101;
        case 'B': return 0b111101110101110;
        case 'C': return 0b111100100100111;
        case 'D': return 0b110101101101110;
        case 'E': return 0b111100111100111;
        case 'F': return 0b111100111100100;
        case 'G': return 0b111100101101111;
        case 'H': return 0b101101111101101;
        case 'I': return 0b010010010010010;
        case 'J': return 0b111001001101111;
        case 'K': return 0b101101110101101;
        case 'L': return 0b100100100100111;
        case 'M': return 0b101111101101101;
        case 'N': return 0b101111111111101;
        case 'O': return 0b111101101101111;
        case 'P': return 0b111101111100100;
        case 'Q': return 0b111101101101011;
        case 'R': return 0b110101110101101;
        case 'S': return 0b111100111001111;
        case 'T': return 0b111010010010010;
        case 'U': return 0b101101101101111;
        case 'V': return 0b101101101101010;
        case 'W': return 0b101101111111101;
        case 'X': return 0b101101010101101;
        case 'Y': return 0b101101010010010;
        case 'Z': return 0b111001010100111;
        case '0': return 0b111101101101111;
        case '1': return 0b010110010010111;
        case '2': return 0b111001010100111;
        case '3': return 0b111001111001111;
        case '4': return 0b101101111001001;
        case '5': return 0b111100111001111;
        case '6': return 0b111100111101111;
        case '7': return 0b111001010010100;
        case '8': return 0b111101010101111;
        case '9': return 0b111101111001111;
        case '/': return 0b001001010100100;
        case '-': return 0b000000111000000;
        case '_': return 0b000000000000111;
        case '>': return 0b000010001010000;
        case '<': return 0b000010100010000;
        case '!': return 0b010010010000010;
        case '?': return 0b111001010000010;
        case '+': return 0b000010111010000;
        case '=': return 0b000111000111000;
        case '*': return 0b101010101000000;
        case '.': return 0b000000000000010;
        case ',': return 0b000000000010100;
        case ':': return 0b000010000010000;
        case ';': return 0b000010000010100;
        case '|': return 0b010010010010010;
        case ']': return 0b110010010010110;
        case '[': return 0b011010010010011;
        case '(': return 0b010100100100010;
        case ')': return 0b010001001001010;
        case '~': return 0b000010111000000;
        case 'x': return 0b101101111001001;
        case ' ': return 0;
        default:  return 0b010101010101010; // original "unknown glyph" raster
    }
}

/// Atlas cell index (row-major position) for an ASCII character.
inline int glyphIndex(char c) {
    unsigned char u = static_cast<unsigned char>(c);
    if (u < 32) u = '?';
    if (u > 127) u = '?';
    int idx = static_cast<int>(u) - 32;
    return idx < 0 ? 0 : (idx >= kGlyphCount ? 0 : idx);
}

/// Pixel offset (top-left corner) of a glyph inside the atlas.
inline void glyphCell(int index, int& px, int& py) {
    px = (index % kCols) * (kCellW + 1);
    py = (index / kCols) * (kCellH + 1);
}

/**
 * @brief Fill a single-channel atlas image (white glyphs, alpha coverage).
 * @param pixels RGBA output, kAtlasW * kAtlasH pixels, row 0 = top.
 */
inline void buildAtlas(uint8_t* pixels) {
    std::memset(pixels, 0, kAtlasW * kAtlasH * 4);
    for (int g = 0; g < kGlyphCount; ++g) {
        int px = 0, py = 0;
        glyphCell(g, px, py);
        char c = static_cast<char>(32 + g);
        uint16_t pattern = glyphPattern(c);
        for (int row = 0; row < kCellH; ++row) {
            for (int col = 0; col < kCellW; ++col) {
                int bit = row * kCellW + col;
                if (!(pattern & (1 << (14 - bit)))) continue;
                int x = px + col;
                int y = py + row;
                uint8_t* p = pixels + (y * kAtlasW + x) * 4;
                p[0] = p[1] = p[2] = p[3] = 255;
            }
        }
    }
}

} // namespace font_atlas

#endif // FONT_ATLAS_HPP
