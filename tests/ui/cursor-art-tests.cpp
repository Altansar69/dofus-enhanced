#include "ui/cursor-art.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// « Curseur Dofus »'s art (cursor-art.cpp), against the design's reference
// renderer (cursor_art.py, numpy): every cursor in the four colours at 32 and
// 64 px and in « Or » at 128 px, then the hotspots, the buffer's layout, the
// palettes and the time a cursor takes. No game, window or file is involved,
// except with `--dump <file>`, which writes every image for a byte-for-byte
// comparison with the reference outside the repository.
namespace {
using namespace cursorArt;
using S = Shape;
using C = Colour;
void Check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
constexpr Shape Shapes[] = {S::Arrow, S::Hand, S::Beam, S::Open, S::Closed, S::ResizeEW, S::ResizeNS, S::ResizeNWSE, S::ResizeNESW,
    S::ArrowForbidden, S::HandForbidden, S::ArrowBusy, S::HandBusy};
constexpr Colour Colours[] = {C::Gold, C::Theme, C::Emerald, C::Silver};
constexpr const char* ShapeNames[] = {"Arrow", "Hand", "Beam", "Open", "Closed", "ResizeEW", "ResizeNS", "ResizeNWSE", "ResizeNESW",
    "ArrowForbidden", "HandForbidden", "ArrowBusy", "HandBusy"};
constexpr const char* ColourNames[] = {"Or", "Thème", "Émeraude", "Argent"};
constexpr int TableSizes[] = {32, 40, 48, 56, 64, 96, 128};
// « Couleur du thème du jeu » with the Standard theme's accent and dark background.
constexpr uint32_t StandardAccent = 0xF0FF4F, StandardDark = 0x1B1D32;
Image Draw(Shape shape, Colour colour, int size) { return Render(shape, PaletteOf(colour, StandardAccent, StandardDark), size); }
std::string Name(Shape shape, Colour colour, int size) {
    return std::string(ShapeNames[static_cast<int>(shape)]) + " " + ColourNames[static_cast<int>(colour)] + " " + std::to_string(size) + " px";
}
bool ArrowTip(Shape shape) { return shape == S::Arrow || shape == S::ArrowForbidden || shape == S::ArrowBusy; }
bool Fingertip(Shape shape) { return shape == S::Hand || shape == S::HandForbidden || shape == S::HandBusy; }
struct Rgba { int r = 0, g = 0, b = 0, a = 0; };
// Pixel (x, y) from the top-left: rows are stored bottom-up.
Rgba At(const Image& image, int x, int y) {
    const uint8_t* p = &image.rgba[(static_cast<size_t>(image.size - 1 - y) * image.size + x) * 4];
    return {p[0], p[1], p[2], p[3]};
}

// What the reference renderer drew for each cursor and colour at 32 and 64 px,
// and in « Or » at 128 px: the alpha sum, the alpha-weighted mean colour, and
// four pixels (x, y from the top-left, 0xRRGGBBAA): the alpha centroid, the
// brightest and the darkest opaque pixels, an edge pixel near half alpha. The
// reference was given the palettes PaletteOf returns, the theme's rounded to
// 8 bits.
struct Sample { int x, y; uint32_t rgba; };
struct Expected { Shape shape; Colour colour; int size, alpha; double r, g, b; Sample samples[4]; };
constexpr Expected References[] = {
    // Gold: #FFF3C4 / #FFD336 / #B97A12 / #2A1806 gem #B9F6D2 / #2FCB78 / #0E6B3D
    {S::Arrow, C::Gold, 32, 70003, 146.962, 125.177, 51.177, {{7, 14, 0x4BBB6AFF}, {2, 3, 0xFFF4C9FF}, {1, 1, 0x2A1806FF}, {11, 28, 0x24150581}}},
    {S::Arrow, C::Gold, 64, 278721, 148.008, 125.722, 50.700, {{14, 30, 0x43763BFF}, {12, 23, 0xECFBF3FF}, {3, 2, 0x2A1806FF}, {1, 4, 0x2A180680}}},
    {S::Hand, C::Gold, 32, 119663, 157.031, 124.341, 40.733, {{15, 17, 0x8E6E1BFF}, {11, 3, 0xFFEDACFF}, {12, 1, 0x2A1806FF}, {5, 19, 0x29170681}}},
    {S::Hand, C::Gold, 64, 476421, 158.423, 126.199, 43.042, {{30, 35, 0x3F2A0AFF}, {23, 5, 0xFFF3C7FF}, {24, 2, 0x2A1806FF}, {17, 52, 0x2A180680}}},
    {S::Beam, C::Gold, 32, 38336, 138.282, 119.267, 76.071, {{15, 15, 0xFFEEAEFF}, {12, 5, 0xFFF6D2FF}, {12, 4, 0x2A1806FF}, {21, 6, 0x24150583}}},
    {S::Beam, C::Gold, 64, 153578, 137.219, 118.245, 75.348, {{31, 31, 0xFFECA5FF}, {25, 10, 0xFFF7D7FF}, {25, 8, 0x2A1806FF}, {41, 15, 0x160D0361}}},
    {S::Open, C::Gold, 32, 136276, 166.774, 135.546, 50.780, {{15, 14, 0xFFD43BFF}, {14, 2, 0xFEEFB7FF}, {13, 1, 0x2A1806FF}, {7, 21, 0x2917067F}}},
    {S::Open, C::Gold, 64, 543755, 167.705, 136.536, 51.587, {{30, 30, 0xFFD336FF}, {17, 9, 0xFFF2C1FF}, {27, 2, 0x2A1806FF}, {18, 51, 0x2A180680}}},
    {S::Closed, C::Gold, 32, 95755, 156.087, 126.054, 47.431, {{15, 18, 0xFED339FF}, {9, 11, 0xFFEFB2FF}, {12, 9, 0x2A1806FF}, {23, 29, 0x20120580}}},
    {S::Closed, C::Gold, 64, 381880, 156.159, 126.537, 48.753, {{31, 38, 0xF8CB33FF}, {18, 22, 0xFFF4C9FF}, {25, 17, 0x2A1806FF}, {17, 51, 0x2A180680}}},
    {S::ResizeEW, C::Gold, 32, 58334, 146.081, 116.559, 41.452, {{15, 15, 0xFFDA56FF}, {5, 14, 0xFEEAA4FF}, {9, 9, 0x2A1806FF}, {11, 10, 0x2616057F}}},
    {S::ResizeEW, C::Gold, 64, 233144, 146.264, 116.931, 42.699, {{31, 31, 0xFFD53FFF}, {15, 24, 0xFFF1BFFF}, {19, 18, 0x2A1806FF}, {8, 37, 0x27160680}}},
    {S::ResizeNS, C::Gold, 32, 58247, 142.621, 113.707, 47.042, {{15, 15, 0xFFD84EFF}, {15, 4, 0xFFF2C0FF}, {15, 2, 0x2A1806FF}, {10, 7, 0x2A180684}}},
    {S::ResizeNS, C::Gold, 64, 232802, 143.174, 114.312, 47.567, {{31, 31, 0xFFD43CFF}, {31, 7, 0xFFF5CEFF}, {31, 4, 0x2A1806FF}, {44, 47, 0x20120580}}},
    {S::ResizeNWSE, C::Gold, 32, 53120, 139.892, 111.420, 46.808, {{15, 15, 0xFFD53EFF}, {8, 8, 0xFFEEAEFF}, {16, 7, 0x2A1806FF}, {25, 18, 0x25150580}}},
    {S::ResizeNWSE, C::Gold, 64, 211824, 139.104, 110.787, 47.854, {{31, 31, 0xFFD43AFF}, {15, 15, 0xFFF7D7FF}, {13, 13, 0x2A1806FF}, {31, 50, 0x27160680}}},
    {S::ResizeNESW, C::Gold, 32, 53295, 143.437, 114.238, 40.690, {{15, 15, 0xFFE176FF}, {17, 8, 0xFFEBA1FF}, {15, 7, 0x2A1806FF}, {24, 17, 0x2314057F}}},
    {S::ResizeNESW, C::Gold, 64, 212604, 142.473, 113.697, 42.261, {{31, 31, 0xFFD951FF}, {33, 16, 0xFFF0B8FF}, {39, 13, 0x2A1806FF}, {45, 12, 0x2A180681}}},
    {S::ArrowForbidden, C::Gold, 32, 114881, 161.173, 122.027, 77.728, {{13, 17, 0x352108FF}, {23, 20, 0xF2F1F0FF}, {1, 1, 0x2A1806FF}, {30, 23, 0x24150580}}},
    {S::ArrowForbidden, C::Gold, 64, 458381, 161.284, 121.535, 76.552, {{26, 36, 0x251505AF}, {12, 23, 0xEEFBF4FF}, {3, 2, 0x2A1806FF}, {1, 4, 0x2A180680}}},
    {S::HandForbidden, C::Gold, 32, 130905, 173.774, 129.598, 70.416, {{16, 17, 0xF7C932FF}, {24, 21, 0xFDFAF2FF}, {12, 1, 0x2A1806FF}, {30, 23, 0x24150580}}},
    {S::HandForbidden, C::Gold, 64, 521791, 174.207, 130.008, 71.200, {{33, 36, 0xF5C731FF}, {35, 49, 0xFDFAF6FF}, {24, 2, 0x2A1806FF}, {17, 52, 0x2A180680}}},
    {S::ArrowBusy, C::Gold, 32, 114881, 125.693, 105.166, 48.291, {{13, 17, 0x352108FF}, {2, 3, 0xFFF4CAFF}, {1, 1, 0x2A1806FF}, {30, 23, 0x24150580}}},
    {S::ArrowBusy, C::Gold, 64, 458381, 126.459, 105.637, 48.033, {{26, 36, 0x251505AF}, {12, 23, 0xEEFBF4FF}, {3, 2, 0x2A1806FF}, {1, 4, 0x2A180680}}},
    {S::HandBusy, C::Gold, 32, 130905, 142.086, 114.349, 44.456, {{16, 17, 0xF7C932FF}, {11, 3, 0xFFEEADFF}, {12, 1, 0x2A1806FF}, {30, 23, 0x24150580}}},
    {S::HandBusy, C::Gold, 64, 521791, 143.089, 115.647, 46.077, {{33, 36, 0xF5C731FF}, {23, 5, 0xFFF3C7FF}, {24, 2, 0x2A1806FF}, {17, 52, 0x2A180680}}},
    // Theme: #F7FF9E / #F0FF4F / #A5B045 / #121320 gem #B9F6D2 / #2FCB78 / #0E6B3D
    {S::Arrow, C::Theme, 32, 70003, 129.642, 142.953, 71.201, {{7, 14, 0x48C472FF}, {2, 3, 0xF8FFACFF}, {1, 1, 0x121320FF}, {11, 28, 0x10101C81}}},
    {S::Arrow, C::Theme, 64, 278721, 130.653, 143.871, 71.069, {{14, 30, 0x377D4CFF}, {12, 23, 0xECFBF3FF}, {3, 2, 0x121320FF}, {1, 4, 0x12132080}}},
    {S::Hand, C::Theme, 32, 119663, 138.826, 148.278, 66.130, {{15, 17, 0x7A8236FF}, {11, 3, 0xF6FF97FF}, {12, 1, 0x121320FF}, {5, 19, 0x11121F81}}},
    {S::Hand, C::Theme, 64, 476421, 140.362, 149.780, 67.706, {{30, 35, 0x272A25FF}, {23, 5, 0xF9FFB5FF}, {24, 2, 0x121320FF}, {17, 52, 0x12132080}}},
    {S::Beam, C::Theme, 32, 38336, 121.941, 126.727, 82.638, {{15, 15, 0xF7FF9DFF}, {12, 5, 0xF9FFB6FF}, {12, 4, 0x121320FF}, {21, 6, 0x0F101B83}}},
    {S::Beam, C::Theme, 64, 153578, 120.855, 125.593, 82.144, {{31, 31, 0xF6FF94FF}, {25, 10, 0xFAFFBEFF}, {25, 8, 0x121320FF}, {41, 15, 0x090A1161}}},
    {S::Open, C::Theme, 32, 136276, 149.332, 158.831, 72.059, {{15, 14, 0xF0FF52FF}, {14, 2, 0xF7FEA7FF}, {13, 1, 0x121320FF}, {7, 21, 0x12131F7F}}},
    {S::Open, C::Theme, 64, 543755, 150.349, 159.916, 72.763, {{30, 30, 0xF0FF4FFF}, {17, 9, 0xF9FFB4FF}, {27, 2, 0x121320FF}, {18, 51, 0x12132080}}},
    {S::Closed, C::Theme, 32, 95755, 138.279, 147.726, 69.755, {{15, 18, 0xEFFE51FF}, {9, 11, 0xF7FFA3FF}, {12, 9, 0x121320FF}, {23, 29, 0x0E0E1880}}},
    {S::Closed, C::Theme, 64, 381880, 138.424, 147.852, 70.530, {{31, 38, 0xE9F74EFF}, {18, 22, 0xF9FFBAFF}, {25, 17, 0x121320FF}, {17, 51, 0x12132080}}},
    {S::ResizeEW, C::Theme, 32, 58334, 127.710, 134.825, 63.754, {{15, 15, 0xF2FF69FF}, {5, 14, 0xF6FE9FFF}, {9, 9, 0x121320FF}, {11, 10, 0x10111D7F}}},
    {S::ResizeEW, C::Theme, 64, 233144, 127.971, 135.057, 65.047, {{31, 31, 0xF1FF55FF}, {15, 24, 0xF9FFB6FF}, {19, 18, 0x121320FF}, {8, 37, 0x11121D80}}},
    {S::ResizeNS, C::Theme, 32, 58247, 124.289, 130.899, 67.382, {{15, 15, 0xF2FF62FF}, {14, 5, 0xF7FEAEFF}, {15, 2, 0x121320FF}, {10, 7, 0x12132084}}},
    {S::ResizeNS, C::Theme, 64, 232802, 124.913, 131.503, 68.074, {{31, 31, 0xF0FF53FF}, {31, 7, 0xF9FFBCFF}, {31, 4, 0x121320FF}, {44, 47, 0x0E0E1880}}},
    {S::ResizeNWSE, C::Theme, 32, 53120, 121.517, 127.914, 67.104, {{15, 15, 0xF0FF53FF}, {14, 8, 0xF7FF9AFF}, {16, 7, 0x121320FF}, {25, 18, 0x10111C80}}},
    {S::ResizeNWSE, C::Theme, 64, 211824, 120.721, 126.958, 68.004, {{31, 31, 0xF0FF51FF}, {15, 15, 0xFAFFC0FF}, {13, 13, 0x121320FF}, {31, 50, 0x11121E80}}},
    {S::ResizeNESW, C::Theme, 32, 53295, 124.956, 131.954, 63.073, {{15, 15, 0xF5FF85FF}, {17, 8, 0xF6FF98FF}, {15, 7, 0x121320FF}, {24, 17, 0x0F101A7F}}},
    {S::ResizeNESW, C::Theme, 64, 212604, 124.117, 130.897, 64.512, {{31, 31, 0xF2FF65FF}, {33, 16, 0xF8FFADFF}, {39, 13, 0x121320FF}, {45, 12, 0x12132081}}},
    {S::ArrowForbidden, C::Theme, 32, 114881, 147.939, 131.267, 90.932, {{13, 17, 0x1D1F22FF}, {23, 20, 0xF1F1F2FF}, {1, 1, 0x121320FF}, {30, 23, 0x10101C80}}},
    {S::ArrowForbidden, C::Theme, 64, 458381, 148.061, 130.988, 90.097, {{26, 36, 0x10111DAF}, {12, 23, 0xEEFBF4FF}, {3, 2, 0x121320FF}, {1, 4, 0x12132080}}},
    {S::HandForbidden, C::Theme, 32, 130905, 159.905, 145.381, 87.501, {{16, 17, 0xE8F64EFF}, {24, 21, 0xFCFDF4FF}, {12, 1, 0x121320FF}, {30, 23, 0x10101C80}}},
    {S::HandForbidden, C::Theme, 64, 521791, 160.425, 145.567, 87.899, {{33, 36, 0xE6F44EFF}, {35, 49, 0xFCFCF8FF}, {24, 2, 0x121320FF}, {17, 52, 0x12132080}}},
    {S::ArrowBusy, C::Theme, 32, 114881, 107.383, 115.887, 67.055, {{13, 17, 0x1D1F22FF}, {2, 3, 0xF8FFADFF}, {1, 1, 0x121320FF}, {30, 23, 0x10101C80}}},
    {S::ArrowBusy, C::Theme, 64, 458381, 108.189, 116.587, 67.034, {{26, 36, 0x10111DAF}, {12, 23, 0xEEFBF4FF}, {3, 2, 0x121320FF}, {1, 4, 0x12132080}}},
    {S::HandBusy, C::Theme, 32, 130905, 123.731, 131.262, 66.402, {{16, 17, 0xE8F64EFF}, {11, 3, 0xF6FF98FF}, {12, 1, 0x121320FF}, {30, 23, 0x10101C80}}},
    {S::HandBusy, C::Theme, 64, 521791, 124.875, 132.354, 67.525, {{33, 36, 0xE6F44EFF}, {23, 5, 0xF9FFB6FF}, {24, 2, 0x121320FF}, {17, 52, 0x12132080}}},
    // Emerald: #C8F7DA / #3FD47A / #138A4A / #06240F gem #FFF0B0 / #FFC700 / #9A6A00
    {S::Arrow, C::Emerald, 32, 70003, 59.948, 132.153, 78.527, {{7, 14, 0xD3BA16FF}, {2, 3, 0xCDF7DDFF}, {1, 1, 0x06240FFF}, {11, 28, 0x051F0D81}}},
    {S::Arrow, C::Emerald, 64, 278721, 59.390, 132.672, 78.694, {{14, 30, 0x767C12FF}, {12, 23, 0xFFFAE8FF}, {3, 2, 0x06240FFF}, {1, 4, 0x06240F80}}},
    {S::Hand, C::Emerald, 32, 119663, 45.632, 132.300, 78.173, {{15, 17, 0x1F7640FF}, {11, 3, 0xB0F0C9FF}, {12, 1, 0x06240FFF}, {5, 19, 0x06230E81}}},
    {S::Hand, C::Emerald, 64, 476421, 47.929, 133.951, 80.191, {{30, 35, 0x0B3519FF}, {23, 5, 0xCAF5DBFF}, {24, 2, 0x06240FFF}, {17, 52, 0x06240F80}}},
    {S::Beam, C::Emerald, 32, 38336, 78.273, 126.788, 96.126, {{15, 15, 0xB2F0CAFF}, {12, 5, 0xD5F9E2FF}, {12, 4, 0x06240FFF}, {21, 6, 0x051F0D83}}},
    {S::Beam, C::Emerald, 64, 153578, 77.513, 125.826, 95.249, {{31, 31, 0xAAEFC5FF}, {25, 10, 0xD9F9E6FF}, {25, 8, 0x06240FFF}, {41, 15, 0x03130861}}},
    {S::Open, C::Emerald, 32, 136276, 55.875, 142.589, 88.408, {{15, 14, 0x44D57EFF}, {14, 2, 0xBBF2D0FF}, {13, 1, 0x06240FFF}, {7, 21, 0x06230F7F}}},
    {S::Open, C::Emerald, 64, 543755, 56.710, 143.524, 89.256, {{30, 30, 0x3FD47AFF}, {17, 9, 0xC4F4D7FF}, {27, 2, 0x06240FFF}, {18, 51, 0x06240F80}}},
    {S::Closed, C::Emerald, 32, 95755, 52.486, 133.729, 82.070, {{15, 18, 0x42D47CFF}, {9, 11, 0xB6F1CDFF}, {12, 9, 0x06240FFF}, {23, 29, 0x051B0B80}}},
    {S::Closed, C::Emerald, 64, 381880, 53.874, 134.204, 82.970, {{31, 38, 0x3BCD76FF}, {18, 22, 0xCCF6DCFF}, {25, 17, 0x06240FFF}, {17, 51, 0x06240F80}}},
    {S::ResizeEW, C::Emerald, 32, 58334, 44.998, 123.878, 75.491, {{15, 15, 0x5EDB90FF}, {5, 14, 0xA9ECC3FF}, {9, 9, 0x06240FFF}, {11, 10, 0x05200E7F}}},
    {S::ResizeEW, C::Emerald, 64, 233144, 46.175, 124.275, 76.377, {{31, 31, 0x47D680FF}, {15, 24, 0xC2F3D5FF}, {19, 18, 0x06240FFF}, {8, 37, 0x06210E80}}},
    {S::ResizeNS, C::Emerald, 32, 58247, 49.874, 122.003, 78.046, {{15, 15, 0x56D98AFF}, {15, 4, 0xC4F4D7FF}, {15, 2, 0x06240FFF}, {10, 7, 0x06240F84}}},
    {S::ResizeNS, C::Emerald, 64, 232802, 50.427, 122.570, 78.621, {{31, 31, 0x45D57EFF}, {31, 7, 0xD1F7DFFF}, {31, 4, 0x06240FFF}, {44, 47, 0x051B0B80}}},
    {S::ResizeNWSE, C::Emerald, 32, 53120, 49.540, 119.881, 76.988, {{15, 15, 0x47D67FFF}, {8, 8, 0xB3F1CBFF}, {16, 7, 0x06240FFF}, {25, 18, 0x051F0D80}}},
    {S::ResizeNWSE, C::Emerald, 64, 211824, 50.462, 119.402, 77.399, {{31, 31, 0x43D57DFF}, {15, 15, 0xDAF9E6FF}, {13, 13, 0x06240FFF}, {31, 50, 0x06210E80}}},
    {S::ResizeNESW, C::Emerald, 32, 53295, 44.125, 121.666, 74.045, {{15, 15, 0x7CE2A4FF}, {17, 8, 0xA6EDC2FF}, {15, 7, 0x06240FFF}, {24, 17, 0x051E0C7F}}},
    {S::ResizeNESW, C::Emerald, 64, 212604, 45.570, 121.214, 74.746, {{31, 31, 0x59DA8CFF}, {33, 16, 0xBCF2D1FF}, {39, 13, 0x06240FFF}, {45, 12, 0x06240F81}}},
    {S::ArrowForbidden, C::Emerald, 32, 114881, 105.301, 127.362, 95.298, {{13, 17, 0x092D14FF}, {23, 20, 0xF0F2F1FF}, {1, 1, 0x06240FFF}, {30, 23, 0x051F0D80}}},
    {S::ArrowForbidden, C::Emerald, 64, 458381, 104.382, 126.875, 94.483, {{26, 36, 0x05200DAF}, {12, 23, 0xFFFBEAFF}, {3, 2, 0x06240FFF}, {1, 4, 0x06240F80}}},
    {S::HandForbidden, C::Emerald, 32, 130905, 94.002, 135.235, 97.195, {{16, 17, 0x3ACC75FF}, {24, 21, 0xF2FAF6FF}, {12, 1, 0x06240FFF}, {30, 23, 0x051F0D80}}},
    {S::HandForbidden, C::Emerald, 64, 521791, 95.068, 135.584, 97.745, {{33, 36, 0x39CA73FF}, {35, 49, 0xF6FBF8FF}, {24, 2, 0x06240FFF}, {17, 52, 0x06240F80}}},
    {S::ArrowBusy, C::Emerald, 32, 114881, 54.714, 113.114, 70.446, {{13, 17, 0x092D14FF}, {2, 3, 0xCEF7DEFF}, {1, 1, 0x06240FFF}, {30, 23, 0x051F0D80}}},
    {S::ArrowBusy, C::Emerald, 64, 458381, 54.560, 113.566, 70.550, {{26, 36, 0x05200DAF}, {12, 23, 0xFFFBEAFF}, {3, 2, 0x06240FFF}, {1, 4, 0x06240F80}}},
    {S::HandBusy, C::Emerald, 32, 130905, 49.444, 122.273, 75.144, {{16, 17, 0x3ACC75FF}, {11, 3, 0xB2F1CAFF}, {12, 1, 0x06240FFF}, {30, 23, 0x051F0D80}}},
    {S::HandBusy, C::Emerald, 64, 521791, 51.213, 123.509, 76.518, {{33, 36, 0x39CA73FF}, {23, 5, 0xCAF6DBFF}, {24, 2, 0x06240FFF}, {17, 52, 0x06240F80}}},
    // Silver: #FFFFFF / #DDE2EC / #8E97AB / #1B1D32 gem #B9F6D2 / #2FCB78 / #0E6B3D
    {S::Arrow, C::Silver, 32, 70003, 126.042, 135.750, 146.076, {{7, 14, 0x45C082FF}, {2, 3, 0xFEFEFEFF}, {1, 1, 0x1B1D32FF}, {11, 28, 0x17192B81}}},
    {S::Arrow, C::Silver, 64, 278721, 126.823, 136.370, 146.827, {{14, 30, 0x367D63FF}, {4, 5, 0xFFFFFFFF}, {3, 2, 0x1B1D32FF}, {1, 4, 0x1B1D3280}}},
    {S::Hand, C::Silver, 32, 119663, 131.889, 137.414, 151.585, {{15, 17, 0x757989FF}, {11, 3, 0xF8F9FBFF}, {12, 1, 0x1B1D32FF}, {5, 19, 0x1A1C3081}}},
    {S::Hand, C::Silver, 64, 476421, 133.598, 139.039, 153.014, {{30, 35, 0x2E3044FF}, {23, 5, 0xFBFBFDFF}, {24, 2, 0x1B1D32FF}, {17, 52, 0x1B1D3280}}},
    {S::Beam, C::Silver, 32, 38336, 125.738, 127.561, 139.615, {{15, 15, 0xF7F8FBFF}, {12, 5, 0xFFFFFFFF}, {12, 4, 0x1B1D32FF}, {21, 6, 0x17192B83}}},
    {S::Beam, C::Silver, 64, 153578, 124.701, 126.501, 138.637, {{31, 31, 0xF6F7FAFF}, {25, 10, 0xFFFFFFFF}, {25, 8, 0x1B1D32FF}, {41, 15, 0x0E0F1A61}}},
    {S::Open, C::Silver, 32, 136276, 143.036, 148.042, 161.099, {{15, 14, 0xDEE3EDFF}, {14, 2, 0xF8F9FBFF}, {13, 1, 0x1B1D32FF}, {7, 21, 0x1A1C317F}}},
    {S::Open, C::Silver, 64, 543755, 143.990, 149.014, 161.979, {{30, 30, 0xDDE2ECFF}, {27, 5, 0xF9FAFCFF}, {27, 2, 0x1B1D32FF}, {18, 51, 0x1B1D3280}}},
    {S::Closed, C::Silver, 32, 95755, 132.832, 138.343, 151.761, {{15, 18, 0xDDE2ECFF}, {9, 11, 0xF8F9FBFF}, {12, 9, 0x1B1D32FF}, {23, 29, 0x15162680}}},
    {S::Closed, C::Silver, 64, 381880, 133.178, 138.752, 152.002, {{31, 38, 0xD5DBE5FF}, {27, 19, 0xFAFBFCFF}, {25, 17, 0x1B1D32FF}, {17, 51, 0x1B1D3280}}},
    {S::ResizeEW, C::Silver, 32, 58334, 123.814, 127.193, 141.886, {{15, 15, 0xE3E7EFFF}, {8, 12, 0xF3F4F8FF}, {9, 9, 0x1B1D32FF}, {11, 10, 0x181A2D7F}}},
    {S::ResizeEW, C::Silver, 64, 233144, 124.114, 127.488, 142.180, {{31, 31, 0xDFE4EDFF}, {18, 22, 0xF8F9FBFF}, {19, 18, 0x1B1D32FF}, {8, 37, 0x191B2E80}}},
    {S::ResizeNS, C::Silver, 32, 58247, 121.617, 124.965, 139.687, {{15, 15, 0xE2E6EFFF}, {15, 4, 0xFBFBFDFF}, {15, 2, 0x1B1D32FF}, {10, 7, 0x1B1D3284}}},
    {S::ResizeNS, C::Silver, 64, 232802, 122.147, 125.497, 140.224, {{31, 31, 0xDEE3EDFF}, {31, 7, 0xFCFCFDFF}, {31, 4, 0x1B1D32FF}, {44, 47, 0x14162680}}},
    {S::ResizeNWSE, C::Silver, 32, 53120, 119.267, 122.518, 137.438, {{15, 15, 0xDFE4EDFF}, {8, 8, 0xF9FAFCFF}, {16, 7, 0x1B1D32FF}, {25, 18, 0x18192C80}}},
    {S::ResizeNWSE, C::Silver, 64, 211824, 118.618, 121.908, 136.815, {{31, 31, 0xDEE3EDFF}, {15, 15, 0xFEFEFFFF}, {13, 13, 0x1B1D32FF}, {31, 50, 0x191B2E80}}},
    {S::ResizeNESW, C::Silver, 32, 53295, 121.386, 124.744, 139.570, {{15, 15, 0xE8ECF2FF}, {17, 8, 0xF4F5F9FF}, {15, 7, 0x1B1D32FF}, {24, 17, 0x1618297F}}},
    {S::ResizeNESW, C::Silver, 64, 212604, 120.751, 124.065, 138.873, {{31, 31, 0xE2E6EFFF}, {33, 16, 0xF7F8FBFF}, {39, 13, 0x1B1D32FF}, {45, 12, 0x1B1D3281}}},
    {S::ArrowForbidden, C::Silver, 32, 114881, 147.632, 128.587, 139.195, {{13, 17, 0x25273CFF}, {2, 3, 0xFEFEFEFF}, {1, 1, 0x1B1D32FF}, {30, 23, 0x17192B80}}},
    {S::ArrowForbidden, C::Silver, 64, 458381, 147.525, 128.112, 138.882, {{26, 36, 0x181A2DAF}, {4, 5, 0xFFFFFFFF}, {3, 2, 0x1B1D32FF}, {1, 4, 0x1B1D3280}}},
    {S::HandForbidden, C::Silver, 32, 130905, 156.113, 138.463, 150.765, {{16, 17, 0xD4DAE5FF}, {24, 21, 0xFBFBFCFF}, {12, 1, 0x1B1D32FF}, {30, 23, 0x17192B80}}},
    {S::HandForbidden, C::Silver, 64, 521791, 156.731, 138.759, 150.973, {{33, 36, 0xD2D8E3FF}, {46, 34, 0xFCFCFDFF}, {24, 2, 0x1B1D32FF}, {17, 52, 0x1B1D3280}}},
    {S::ArrowBusy, C::Silver, 32, 114881, 107.804, 113.757, 125.806, {{13, 17, 0x25273CFF}, {2, 3, 0xFEFEFEFF}, {1, 1, 0x1B1D32FF}, {30, 23, 0x17192B80}}},
    {S::ArrowBusy, C::Silver, 64, 458381, 108.412, 114.275, 126.233, {{26, 36, 0x181A2DAF}, {4, 5, 0xFFFFFFFF}, {3, 2, 0x1B1D32FF}, {1, 4, 0x1B1D3280}}},
    {S::HandBusy, C::Silver, 32, 130905, 120.687, 124.923, 138.534, {{16, 17, 0xD4DAE5FF}, {11, 3, 0xF8F9FBFF}, {12, 1, 0x1B1D32FF}, {30, 23, 0x17192B80}}},
    {S::HandBusy, C::Silver, 64, 521791, 121.934, 126.131, 139.426, {{33, 36, 0xD2D8E3FF}, {23, 5, 0xFBFCFDFF}, {24, 2, 0x1B1D32FF}, {17, 52, 0x1B1D3280}}},
    // Gold at 128 px, where the painted region's margins leave the least slack:
    // its bounds are rounded out to whole pixels, up to 1/s units, so a margin
    // cut short shows here first.
    {S::Arrow, C::Gold, 128, 1113568, 148.342, 125.999, 50.767, {{29, 61, 0x2C4E28FF}, {23, 45, 0xF1FCF6FF}, {5, 4, 0x2A1806FF}, {35, 101, 0x2A180682}}},
    {S::Hand, C::Gold, 128, 1905468, 158.511, 126.283, 43.266, {{62, 71, 0xF6C731FF}, {44, 11, 0xFFF6D2FF}, {48, 4, 0x2A1806FF}, {16, 70, 0x29180681}}},
    {S::Beam, C::Gold, 128, 613719, 137.077, 118.096, 75.329, {{63, 63, 0xFFEBA1FF}, {48, 21, 0xFFF8DBFF}, {50, 16, 0x2A1806FF}, {47, 16, 0x2A180698}}},
    {S::Open, C::Gold, 128, 2174372, 167.838, 136.603, 51.790, {{61, 61, 0xFED236FF}, {53, 10, 0xFFF4CDFF}, {58, 3, 0x2A1806FF}, {99, 21, 0x2A180680}}},
    {S::Closed, C::Gold, 128, 1527147, 156.117, 126.222, 48.219, {{63, 76, 0xF6C932FF}, {52, 39, 0xFFF5CEFF}, {53, 33, 0x2A1806FF}, {60, 32, 0x2A180680}}},
    {S::ResizeEW, C::Gold, 128, 932418, 146.320, 117.000, 42.731, {{63, 63, 0xFFD438FF}, {35, 44, 0xFFF4C9FF}, {38, 35, 0x2A1806FF}, {42, 34, 0x2A180680}}},
    {S::ResizeNS, C::Gold, 128, 931144, 143.193, 114.423, 47.732, {{63, 63, 0xFFD338FF}, {63, 13, 0xFFF6D4FF}, {61, 8, 0x2A1806FF}, {47, 23, 0x2A180680}}},
    {S::ResizeNWSE, C::Gold, 128, 847477, 139.308, 110.999, 47.685, {{63, 63, 0xFFD338FF}, {30, 30, 0xFFF7D9FF}, {27, 25, 0x2A1806FF}, {69, 34, 0x26160580}}},
    {S::ResizeNESW, C::Gold, 128, 850517, 142.622, 113.825, 42.118, {{63, 63, 0xFFD53DFF}, {64, 32, 0xFFF3C8FF}, {91, 25, 0x2A1806FF}, {97, 24, 0x2A18067F}}},
    {S::ArrowForbidden, C::Gold, 128, 1832786, 161.402, 121.642, 76.594, {{54, 73, 0x190E0457}, {23, 45, 0xF2FCF7FF}, {5, 4, 0x2A1806FF}, {66, 76, 0x26160580}}},
    {S::HandForbidden, C::Gold, 128, 2086951, 174.316, 130.195, 71.539, {{66, 73, 0xF5C731FF}, {69, 98, 0xFDFBF7FF}, {48, 4, 0x2A1806FF}, {16, 70, 0x29180681}}},
    {S::ArrowBusy, C::Gold, 128, 1832786, 126.498, 105.716, 48.292, {{54, 73, 0x190E0457}, {23, 45, 0xF2FCF7FF}, {5, 4, 0x2A1806FF}, {66, 76, 0x26160580}}},
    {S::HandBusy, C::Gold, 128, 2086951, 143.132, 115.798, 46.601, {{66, 73, 0xF5C731FF}, {44, 11, 0xFFF6D3FF}, {48, 4, 0x2A1806FF}, {16, 70, 0x29180681}}},
};
// The port computes as the reference does: double precision, numpy's order of
// operations, the Windows C runtime's hypot, cos and sin as numpy calls them,
// and numpy's rounding, half to even. It matched all 5 044 images (four
// colours, 13 cursors, every size from 32 to 128) byte for byte. Under another compiler or runtime a value
// within a few ulps of a rounding edge (k + ½) could round the other way: one
// unit on one byte. Hence one unit per sample channel, 4 on an alpha sum, and
// 0.05 on a mean channel: one such byte moves a mean by at most 255 divided by
// the smallest alpha sum (38 336), about 0.007, and the table keeps 3 decimals.
void Reference() {
    for (const auto& expected : References) {
        const auto image = Draw(expected.shape, expected.colour, expected.size);
        const auto what = Name(expected.shape, expected.colour, expected.size);
        double alpha = 0, r = 0, g = 0, b = 0;
        for (size_t k = 0; k < image.rgba.size(); k += 4) {
            const double a = image.rgba[k + 3];
            alpha += a, r += image.rgba[k] * a, g += image.rgba[k + 1] * a, b += image.rgba[k + 2] * a;
        }
        Check(std::abs(alpha - expected.alpha) <= 4, what + ": alpha sum " + std::to_string(alpha) + ", not " + std::to_string(expected.alpha));
        Check(std::abs(r / alpha - expected.r) <= 0.05 && std::abs(g / alpha - expected.g) <= 0.05 && std::abs(b / alpha - expected.b) <= 0.05,
            what + ": mean colour " + std::to_string(r / alpha) + " " + std::to_string(g / alpha) + " " + std::to_string(b / alpha));
        for (const auto& sample : expected.samples) {
            const auto pixel = At(image, sample.x, sample.y);
            const int want[] = {static_cast<int>(sample.rgba >> 24), static_cast<int>(sample.rgba >> 16 & 0xFF),
                static_cast<int>(sample.rgba >> 8 & 0xFF), static_cast<int>(sample.rgba & 0xFF)};
            Check(std::abs(pixel.r - want[0]) <= 1 && std::abs(pixel.g - want[1]) <= 1 && std::abs(pixel.b - want[2]) <= 1 &&
                std::abs(pixel.a - want[3]) <= 1, what + ": pixel " + std::to_string(sample.x) + "," + std::to_string(sample.y));
        }
    }
}

// The three fixed colours as the design gives them, whatever the theme; the
// theme's from its accent and dark background, like the reference's
// theme_palette rounded half to even: a dark accent is lightened by steps of
// 0.025 until its luminance reaches 0.30, a green one (hue 90° to 170° and
// saturation above 0.35, once lightened) gets the gold gem. Each of the gem
// rule's bounds is pinned from both sides: hue 84.8° and 93.8°, 168° and
// 171.4°, saturation 0.337 and 0.391.
void Palettes() {
    using Seven = std::array<uint32_t, 7>;
    const auto all = [](const Palette& p) { return Seven{p.light, p.mid, p.deep, p.outline, p.gemLight, p.gemMid, p.gemDeep}; };
    Check(all(PaletteOf(C::Gold)) == Seven{0xFFF3C4, 0xFFD336, 0xB97A12, 0x2A1806, 0xB9F6D2, 0x2FCB78, 0x0E6B3D}, "« Or », with the emerald");
    Check(all(PaletteOf(C::Emerald)) == Seven{0xC8F7DA, 0x3FD47A, 0x138A4A, 0x06240F, 0xFFF0B0, 0xFFC700, 0x9A6A00}, "« Émeraude », with gold");
    Check(all(PaletteOf(C::Silver)) == Seven{0xFFFFFF, 0xDDE2EC, 0x8E97AB, 0x1B1D32, 0xB9F6D2, 0x2FCB78, 0x0E6B3D}, "« Argent »");
    for (const auto colour : {C::Gold, C::Emerald, C::Silver})
        Check(all(PaletteOf(colour, 0x3D405C, 0x90E052)) == all(PaletteOf(colour)), "A fixed colour ignores the theme's");
    Check(all(PaletteOf(static_cast<Colour>(4))) == all(PaletteOf(C::Gold)), "An unknown colour is « Or »");
    struct Theme { uint32_t accent, dark; Seven palette; const char* what; };
    constexpr uint32_t E[] = {0xB9F6D2, 0x2FCB78, 0x0E6B3D}, G[] = {0xFFF0B0, 0xFFC700, 0x9A6A00};
    const Theme themes[] = {
        {0xF0FF4F, 0x1B1D32, {0xF7FF9E, 0xF0FF4F, 0xA5B045, 0x121320, E[0], E[1], E[2]}, "Standard: lime-yellow, the emerald"},
        {0xB6B3E6, 0x292B33, {0xD7D5F1, 0xB6B3E6, 0x8583A7, 0x1B1C21, E[0], E[1], E[2]}, "A light lavender, as it is"},
        {0x3D405C, 0x1B1D32, {0xC4C5CE, 0x9496A5, 0x6A6C7D, 0x121320, E[0], E[1], E[2]}, "A dark slate, lightened by 0.45"},
        {0x8B0000, 0x1F1F1F, {0xE1BCBC, 0xC88686, 0x8D6262, 0x141414, E[0], E[1], E[2]}, "A dark red, lightened by 0.525"},
        {0x808080, 0x212121, {0xC5C5C5, 0x969696, 0x6D6D6D, 0x151515, E[0], E[1], E[2]}, "A grey, lightened by 0.175"},
        {0x000000, 0x000000, {0xC7C7C7, 0x999999, 0x636363, 0x000000, E[0], E[1], E[2]}, "Black, the arguments' default: grey"},
        {0x90E052, 0x1F2625, {0xC2EEA0, 0x90E052, 0x689F42, 0x141918, G[0], G[1], G[2]}, "A green, hue 93.8°: the gold gem"},
        {0x2E7D32, 0x1F2625, {0xAFCDB0, 0x6DA470, 0x527855, 0x141918, E[0], E[1], E[2]}, "A dark green, saturation 0.337 once lightened: too pale for gold"},
        {0x8CE68C, 0x1B1D32, {0xC0F1C0, 0x8CE68C, 0x64A06C, 0x121320, G[0], G[1], G[2]}, "A pale green, saturation 0.391: gold"},
        {0xA8E650, 0x1B1D32, {0xCFF19F, 0xA8E650, 0x77A046, 0x121320, E[0], E[1], E[2]}, "A yellow-green, hue 84.8°: the emerald"},
        {0x50E6C8, 0x1B1D32, {0x9FF1E1, 0x50E6C8, 0x3DA094, 0x121320, G[0], G[1], G[2]}, "A turquoise, hue 168°: gold"},
        {0x5AE6D2, 0x1B1D32, {0xA4F1E6, 0x5AE6D2, 0x44A09A, 0x121320, E[0], E[1], E[2]}, "A turquoise, hue 171.4°: the emerald"}};
    for (const auto& theme : themes) Check(all(PaletteOf(C::Theme, theme.accent, theme.dark)) == theme.palette, theme.what);
    Check(all(PaletteOf(C::Theme, 0xFF000000 | StandardAccent, 0x80000000 | StandardDark)) == themes[0].palette, "The alpha byte is ignored");
}

// The design's table from 32 to 128 px: the arrow's tip (2, 2), the pointing
// hand's fingertip (12.5, 2.2) and the centre of the others, in grid units
// times s = size/32 rounded down; a badge keeps its cursor's. At every size
// the hotspot is that formula, worked out in integers (12.5·s = 25·size/64,
// 2.2·s = 11·size/160), its pixel is opaque, and nothing shows more than
// ⌈1.25·s⌉ (the outline) above the arrow's and the hand's, nor left of the
// arrow's: the hand's thumb lies left of its fingertip by design.
void Hotspots() {
    constexpr int Tip[] = {2, 2, 3, 3, 4, 6, 8}, FingerX[] = {12, 15, 18, 21, 25, 37, 50};
    for (int k = 0; k < 7; ++k)
        for (const auto shape : Shapes) {
            const int size = TableSizes[k];
            const auto image = Draw(shape, C::Gold, size);
            const int x = ArrowTip(shape) ? Tip[k] : Fingertip(shape) ? FingerX[k] : size / 2;
            const int y = ArrowTip(shape) || Fingertip(shape) ? Tip[k] : size / 2;
            Check(image.hotX == x && image.hotY == y, "Hotspot of " + Name(shape, C::Gold, size) + ": " + std::to_string(image.hotX) + "," +
                std::to_string(image.hotY));
        }
    for (int size = MinSize; size <= MaxSize; ++size)
        for (const auto shape : Shapes) {
            const auto image = Draw(shape, C::Gold, size);
            const int wantX = ArrowTip(shape) ? size / 16 : Fingertip(shape) ? 25 * size / 64 : size / 2;
            const int wantY = ArrowTip(shape) ? size / 16 : Fingertip(shape) ? 11 * size / 160 : size / 2;
            Check(image.hotX == wantX && image.hotY == wantY, "Hotspot of " + Name(shape, C::Gold, size) + ": " +
                std::to_string(image.hotX) + "," + std::to_string(image.hotY));
            Check(At(image, image.hotX, image.hotY).a == 255, "Opaque hotspot: " + Name(shape, C::Gold, size));
            if (!ArrowTip(shape) && !Fingertip(shape)) continue;
            const int margin = static_cast<int>(std::ceil(1.25 * size / 32));
            for (int y = 0; y < size; ++y)
                for (int x = 0; x < size; ++x)
                    if (y < image.hotY - margin || (ArrowTip(shape) && x < image.hotX - margin))
                        Check(At(image, x, y).a == 0, "Nothing beyond the tip: " + Name(shape, C::Gold, size));
        }
}

// The buffer: size² RGBA32 pixels, transparent ones black, the corners
// transparent and nothing on the border above alpha 64 (the shadow's soft
// end). Rows go bottom-up: the arrow's tip lies in the buffer's last rows.
void Layout() {
    for (const auto colour : Colours)
        for (const auto shape : Shapes)
            for (const int size : TableSizes) {
                const auto image = Draw(shape, colour, size);
                const auto what = Name(shape, colour, size);
                Check(image.size == size && image.rgba.size() == static_cast<size_t>(size) * size * 4, "Size: " + what);
                int border = 0;
                for (int y = 0; y < size; ++y)
                    for (int x = 0; x < size; ++x) {
                        const auto pixel = At(image, x, y);
                        Check(pixel.a || (!pixel.r && !pixel.g && !pixel.b), "Transparent pixels are black: " + what);
                        if (x == 0 || y == 0 || x == size - 1 || y == size - 1) border = std::max(border, pixel.a);
                    }
                Check(border <= 64, "Nothing opaque on the border (" + std::to_string(border) + "): " + what);
                Check(!At(image, 0, 0).a && !At(image, size - 1, 0).a && !At(image, 0, size - 1).a && !At(image, size - 1, size - 1).a,
                    "Transparent corners: " + what);
            }
    const auto arrow = Draw(S::Arrow, C::Gold, 64);
    const auto raw = [&arrow](int row, int x) { return arrow.rgba[(static_cast<size_t>(row) * 64 + x) * 4 + 3]; };
    bool bottomEmpty = true;
    for (int x = 0; x < 64; ++x) bottomEmpty = bottomEmpty && !raw(0, x);
    Check(arrow.hotX == 4 && arrow.hotY == 4 && raw(63 - 4, 4) == 255 && !raw(4, 4) && bottomEmpty,
        "Rows bottom-up: the arrow's tip (4, 4) in the buffer's row 59, its first row the image's empty bottom");
}

// Sizes outside 32..128 are clamped; an unknown shape is drawn as the arrow
// (an unknown colour is « Or »: Palettes).
void OutOfRange() {
    const auto arrow = Draw(S::Arrow, C::Gold, 40), unknown = Draw(static_cast<Shape>(ShapeCount), C::Gold, 40);
    Check(unknown.rgba == arrow.rgba && unknown.hotX == arrow.hotX && unknown.hotY == arrow.hotY, "An unknown shape is the arrow");
    const auto small = Draw(S::Hand, C::Gold, 32), large = Draw(S::Hand, C::Gold, 128);
    for (const int size : {INT_MIN, -1, 0, 1, 31}) {
        const auto image = Draw(S::Hand, C::Gold, size);
        Check(image.size == 32 && image.rgba == small.rgba && image.hotX == small.hotX && image.hotY == small.hotY,
            "Clamped to 32: " + std::to_string(size));
    }
    for (const int size : {129, 256, 4096, INT_MAX}) {
        const auto image = Draw(S::Hand, C::Gold, size);
        Check(image.size == 128 && image.rgba == large.rgba && image.hotX == large.hotX && image.hotY == large.hotY,
            "Clamped to 128: " + std::to_string(size));
    }
}

// The same cursor gives the same bytes, whatever was drawn in between.
void Deterministic() {
    for (const auto colour : Colours)
        for (const auto shape : Shapes) {
            const auto first = Draw(shape, colour, 48);
            Draw(Shapes[(static_cast<int>(shape) + 5) % ShapeCount], C::Emerald, 128);
            const auto second = Draw(shape, colour, 48);
            Check(first.rgba == second.rgba && first.hotX == second.hotX && first.hotY == second.hotY, "Same bytes: " + Name(shape, colour, 48));
        }
}

// The double arrows' outline and fill (alpha 200 and more; the shadow alone
// stays under 90) mirror each other: ↔ and ↕ both ways, ↘ left-right as ↙. To
// one pixel: the shadow, which falls right and down, raises a few edge pixels.
using Mask = std::vector<bool>;
Mask Solid(const Image& image) {
    Mask mask(static_cast<size_t>(image.size) * image.size);
    for (int y = 0; y < image.size; ++y)
        for (int x = 0; x < image.size; ++x) mask[static_cast<size_t>(y) * image.size + x] = At(image, x, y).a >= 200;
    return mask;
}
Mask Mirrored(const Mask& mask, int size, bool leftRight) {
    Mask mirrored(mask.size());
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            mirrored[static_cast<size_t>(y) * size + x] =
                mask[static_cast<size_t>(leftRight ? y : size - 1 - y) * size + (leftRight ? size - 1 - x : x)];
    return mirrored;
}
// Every pixel of each mask has one of the other within a pixel.
bool Alike(const Mask& a, const Mask& b, int size) {
    const auto covered = [size](const Mask& from, const Mask& by) {
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) {
                if (!from[static_cast<size_t>(y) * size + x]) continue;
                bool found = false;
                for (int dy = -1; dy <= 1 && !found; ++dy)
                    for (int dx = -1; dx <= 1 && !found; ++dx) {
                        const int nx = x + dx, ny = y + dy;
                        found = nx >= 0 && ny >= 0 && nx < size && ny < size && by[static_cast<size_t>(ny) * size + nx];
                    }
                if (!found) return false;
            }
        return true;
    };
    return covered(a, b) && covered(b, a);
}
void Mirrors() {
    for (const int size : TableSizes) {
        const auto ew = Solid(Draw(S::ResizeEW, C::Gold, size)), ns = Solid(Draw(S::ResizeNS, C::Gold, size));
        const auto nwse = Solid(Draw(S::ResizeNWSE, C::Gold, size)), nesw = Solid(Draw(S::ResizeNESW, C::Gold, size));
        const auto at = " at " + std::to_string(size) + " px";
        Check(Alike(ew, Mirrored(ew, size, true), size) && Alike(ew, Mirrored(ew, size, false), size), "↔ symmetric" + at);
        Check(Alike(ns, Mirrored(ns, size, true), size) && Alike(ns, Mirrored(ns, size, false), size), "↕ symmetric" + at);
        Check(Alike(Mirrored(nwse, size, true), nesw, size), "↘ mirrored is ↙" + at);
    }
}

// The forbidden badge's red (#E8404E) at every size, the hourglass's sand
// (#FFD336) from 48 px: smaller, it blends into the glass.
void Badges() {
    const auto has = [](const Image& image, int r, int g, int b) {
        for (int y = 0; y < image.size; ++y)
            for (int x = 0; x < image.size; ++x) {
                const auto pixel = At(image, x, y);
                if (pixel.a == 255 && pixel.r == r && pixel.g == g && pixel.b == b) return true;
            }
        return false;
    };
    for (const int size : TableSizes) {
        for (const auto shape : {S::ArrowForbidden, S::HandForbidden})
            Check(has(Draw(shape, C::Gold, size), 0xE8, 0x40, 0x4E), "Red badge: " + Name(shape, C::Gold, size));
        for (const auto shape : {S::ArrowBusy, S::HandBusy})
            Check(size < 48 || has(Draw(shape, C::Gold, size), 0xFF, 0xD3, 0x36), "Sand in the hourglass: " + Name(shape, C::Gold, size));
    }
}

// The runtime draws one cursor per frame: each in under 20 ms at 128 px, the
// best of five runs (the design aims at about 4 ms, 0.3 ms at 32 px). An
// unoptimized build only reports its times.
void Fast() {
    double totals[2] = {}, slowest = 0;
    for (const auto shape : Shapes)
        for (int k = 0; k < 2; ++k) {
            double best = 1e9;
            for (int run = 0; run < 5; ++run) {
                const auto start = std::chrono::steady_clock::now();
                const auto image = Draw(shape, C::Gold, k ? 128 : 32);
                best = std::min(best, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
                Check(!image.rgba.empty(), "Drawn");
            }
            totals[k] += best;
            if (k) slowest = std::max(slowest, best);
        }
    std::cout << "INFO: the 13 cursors in " << totals[0] << " ms at 32 px, " << totals[1] << " ms at 128 px (the slowest " << slowest << " ms)\n";
#ifdef NDEBUG
    Check(slowest < 20, "Each cursor in under 20 ms at 128 px");
#endif
}

// `--dump <file>`: every colour, cursor and size, each image after five int32
// (colour, shape, size, hotspot x and y), its rows bottom-up as Render gives them.
int Dump(const char* path) {
    std::ofstream out(path, std::ios::binary);
    for (const auto colour : Colours)
        for (const auto shape : Shapes)
            for (int size = MinSize; size <= MaxSize; ++size) {
                const auto image = Draw(shape, colour, size);
                const int32_t header[] = {static_cast<int32_t>(colour), static_cast<int32_t>(shape), image.size, image.hotX, image.hotY};
                out.write(reinterpret_cast<const char*>(header), sizeof(header));
                out.write(reinterpret_cast<const char*>(image.rgba.data()), static_cast<std::streamsize>(image.rgba.size()));
            }
    return out ? 0 : 1;
}
}

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--dump") return Dump(argv[2]);
    try {
        Reference();
        Palettes();
        Hotspots();
        Layout();
        OutOfRange();
        Deterministic();
        Mirrors();
        Badges();
        Fast();
        std::cout << "PASS: cursor art (the 13 cursors in the four colours as the design's reference renderer draws them at 32 and 64 px "
                     "and in « Or » at 128 px, the design's hotspots at every size from 32 to 128 px, opaque and at the tip, transparent "
                     "black background and border, rows bottom-up, sizes clamped to 32..128 and unknown values drawn as the arrow in "
                     "« Or », the same bytes every time, mirrored resize arrows, red and sand badges, the fixed palettes and the theme's "
                     "with the gem rule's bounds)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
