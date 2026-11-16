#pragma once
#include <array>

// Shared by the actual minimap/head renderer and its offline checks.
namespace MapQuestMarkerPolicy
{
    constexpr int Width = 32, Height = 40;
    inline char Symbol(bool MainQuest) { return MainQuest ? '!' : '?'; }
    inline bool GlyphPixel(bool MainQuest, int X, int Y)
    {
        static const char* Main[] = {"01110", "01110", "01110", "00100", "00000", "00100", "00100"};
        static const char* Side[] = {"01110", "11011", "00011", "00110", "01100", "00000", "01100"};
        X -= 6; Y -= 6;
        if (X < 0 || Y < 0 || X >= 20 || Y >= 28) return false;
        const char* Row = MainQuest ? Main[Y / 4] : Side[Y / 4];
        return Row[X / 4] == '1';
    }
    // 0 = transparent, 1 = dark outline, 2 = white body, tinted centrally.
    inline std::array<unsigned char, Width * Height> Raster(bool MainQuest)
    {
        std::array<unsigned char, Width * Height> Pixels{};
        for (int Y = 0; Y < Height; ++Y) for (int X = 0; X < Width; ++X)
        {
            if (GlyphPixel(MainQuest, X, Y)) Pixels[unsigned(Y * Width + X)] = 2;
            else
                for (int DY = -2; DY <= 2; ++DY) for (int DX = -2; DX <= 2; ++DX)
                    if (GlyphPixel(MainQuest, X + DX, Y + DY)) Pixels[unsigned(Y * Width + X)] = 1;
        }
        return Pixels;
    }
}
