#pragma once

#include "ReditusBattleHUDSkin.h"

namespace ReditusTutorialSkin
{
    using ReditusHUDSkin::Image;
    using ReditusHUDSkin::Line;
    using ReditusHUDSkin::Mix;
    using ReditusHUDSkin::Pixel;
    using ReditusHUDSkin::Put;

    inline Image Plate()
    {
        Image Art = ReditusHUDSkin::Frame();
        for (int Y = 9; Y < Art.Height - 9; ++Y)
        {
            const float Height = static_cast<float>(Y - 9) / (Art.Height - 18);
            const Pixel Blue = Mix(Pixel(20, 37, 56, 224), Pixel(8, 15, 29, 227), Height);
            for (int X = 9; X < Art.Width - 9; ++X)
            {
                Art.Pixels[Y * Art.Width + X] = Blue;
            }
        }
        // Engraved brackets sit entirely within the existing nine-slice corners.
        for (int Corner = 0; Corner < 4; ++Corner)
        {
            const auto Mark = [&](int X, int Y, Pixel Ink)
            {
                Put(Art, (Corner & 1) ? 95 - X : X, (Corner & 2) ? 95 - Y : Y, Ink);
            };
            const Pixel Copper(136, 89, 37);
            const Pixel Light(249, 215, 139);
            for (int Offset = 10; Offset < 16; ++Offset)
            {
                Mark(Offset, 7, Copper);
                Mark(7, Offset, Copper);
            }
            Mark(9, 9, Light);
            Mark(10, 9, Light);
            Mark(9, 10, Light);
            Mark(12, 8, Light);
            Mark(8, 12, Light);
            Mark(14, 7, Light);
            Mark(7, 14, Light);
        }
        return Art;
    }

    inline Image Shield()
    {
        Image Art(48, 48);
        const Pixel Brass(141, 92, 32);
        const Pixel Gold(238, 189, 91);
        const Pixel Glint(255, 234, 162);
        for (int Y = 0; Y < Art.Height; ++Y)
        {
            for (int X = 0; X < Art.Width; ++X)
            {
                const int Radius = std::abs(2 * X - 47) + std::abs(2 * Y - 47);
                if (Radius <= 46)
                {
                    const Pixel Face = Mix(Pixel(30, 50, 63, 245), Pixel(10, 18, 33, 245), Y / 47.f);
                    const Pixel Rim = Y < 24 ? Gold : Brass;
                    Put(Art, X, Y, Radius >= 44 ? Pixel(3, 6, 13) : Radius >= 40 ? Rim :
                        Radius >= 34 ? Pixel(49, 38, 24) : Face);
                }
            }
        }
        // Taper the shield from its wide shoulders down to the pointed base.
        for (int Y = 12; Y <= 36; ++Y)
        {
            const int HalfWidth = Y < 25 ? 10 : (36 - Y) * 10 / 11;
            const Pixel Steel = Mix(Pixel(155, 196, 196), Pixel(40, 76, 88), (Y - 12) / 24.f);
            for (int X = 23 - HalfWidth; X <= 24 + HalfWidth; ++X)
            {
                const bool bEdge = X == 23 - HalfWidth || X == 24 + HalfWidth || Y < 14;
                Put(Art, X, Y, bEdge ? (Y < 16 ? Glint : Gold) :
                    X < 23 ? Steel : Mix(Steel, Pixel(9, 24, 41), 0.38f));
            }
        }
        Line(Art, 23, 16, 23, 30, Pixel(94, 137, 144));
        Line(Art, 16, 18, 20, 18, Glint);
        Line(Art, 22, 21, 25, 21, Gold);
        Line(Art, 22, 22, 25, 22, Gold);
        Put(Art, 23, 20, Glint);
        Put(Art, 24, 23, Brass);
        return Art;
    }

    inline Image Rule()
    {
        Image Art(192, 12);
        for (int X = 0; X < Art.Width; ++X)
        {
            const int Distance = std::abs(2 * X - 191);
            const int Alpha = 185 * (191 - Distance) / 191;
            Put(Art, X, 5, Pixel(218, 177, 94, Alpha));
            Put(Art, X, 6, Pixel(101, 68, 31, Alpha));
            for (int Y = 1; Y < 11; ++Y)
            {
                const int Diamond = Distance + std::abs(2 * Y - 11);
                if (Diamond <= 10)
                {
                    Put(Art, X, Y, Diamond >= 8 ? Pixel(167, 112, 49) : Pixel(255, 222, 141));
                }
            }
        }
        return Art;
    }

    inline Image StaminaSample(int MaximumStamina, bool bFull)
    {
        // The battle HUD uses pips through six Stamina and a gauge above six.
        if (MaximumStamina > 6)
        {
            Image Art = ReditusHUDSkin::Bar(bFull);
            if (bFull)
            {
                for (Pixel& Ink : Art.Pixels)
                {
                    Ink = Pixel(Ink.R, Ink.G * 78 / 100, Ink.B * 40 / 100, Ink.A);
                }
            }
            return Art;
        }

        const int PipCount = std::max(1, MaximumStamina);
        const Image Glyph = ReditusHUDSkin::EnemyBadgeGlyph(bFull ? 17 : 18);
        const int Spacing = Glyph.Width + 2;
        Image Art(PipCount * Spacing - 2, Glyph.Height);
        for (int Pip = 0; Pip < PipCount; ++Pip)
        {
            for (int Y = 0; Y < Glyph.Height; ++Y)
            {
                for (int X = 0; X < Glyph.Width; ++X)
                {
                    Art.Pixels[Y * Art.Width + Pip * Spacing + X] = Glyph.Pixels[Y * Glyph.Width + X];
                }
            }
        }
        return Art;
    }
}
