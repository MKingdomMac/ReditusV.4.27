#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

/** CPU artwork shared by the runtime texture builder and the standalone preview.
 * Frame corners occupy 16 pixels of a 96px nine-slice, so ornaments do not stretch.
 */
namespace ReditusHUDSkin
{
    struct Pixel
    {
        std::uint8_t R, G, B, A;
        Pixel(int Red=0,int Green=0,int Blue=0,int Alpha=255)
            : R(static_cast<std::uint8_t>(Red)),G(static_cast<std::uint8_t>(Green)),
              B(static_cast<std::uint8_t>(Blue)),A(static_cast<std::uint8_t>(Alpha)) {}
    };
    struct Image
    {
        int Width, Height;
        std::vector<Pixel> Pixels;
        Image(int W,int H) : Width(W),Height(H),Pixels(static_cast<std::size_t>(W*H),Pixel(0,0,0,0)) {}
    };
    inline Pixel Mix(Pixel First,Pixel Second,float Amount)
    {
        Amount=std::max(0.f,std::min(1.f,Amount));
        return Pixel(static_cast<int>(First.R+(Second.R-First.R)*Amount),
                     static_cast<int>(First.G+(Second.G-First.G)*Amount),
                     static_cast<int>(First.B+(Second.B-First.B)*Amount),
                     static_cast<int>(First.A+(Second.A-First.A)*Amount));
    }
    inline void Put(Image& Target,int X,int Y,Pixel Ink)
    {
        if (X<0 || Y<0 || X>=Target.Width || Y>=Target.Height) return;
        Pixel& Dest=Target.Pixels[static_cast<std::size_t>(Y*Target.Width+X)];
        const float Alpha=Ink.A/255.f;
        const float DestAlpha=Dest.A/255.f;
        const float Combined=Alpha+DestAlpha*(1.f-Alpha);
        if (Combined<=0.f) return;
        Dest=Pixel(static_cast<int>((Ink.R*Alpha+Dest.R*DestAlpha*(1.f-Alpha))/Combined),
                   static_cast<int>((Ink.G*Alpha+Dest.G*DestAlpha*(1.f-Alpha))/Combined),
                   static_cast<int>((Ink.B*Alpha+Dest.B*DestAlpha*(1.f-Alpha))/Combined),
                   static_cast<int>(Combined*255.f));
    }
    inline void Line(Image& Target,int X0,int Y0,int X1,int Y1,Pixel Ink)
    {
        const int Steps=std::max(std::abs(X1-X0),std::abs(Y1-Y0));
        for (int I=0; I<=Steps; ++I)
        {
            const float T=Steps ? static_cast<float>(I)/Steps : 0.f;
            Put(Target,static_cast<int>(std::round(X0+(X1-X0)*T)),static_cast<int>(std::round(Y0+(Y1-Y0)*T)),Ink);
        }
    }
    inline void Outline(Image& Target,int Inset,int Cut,Pixel Ink)
    {
        const int End=Target.Width-1-Inset;
        Line(Target,Inset+Cut,Inset,End-Cut,Inset,Ink);
        Line(Target,Inset+Cut,End,End-Cut,End,Ink);
        Line(Target,Inset,Inset+Cut,Inset,End-Cut,Ink);
        Line(Target,End,Inset+Cut,End,End-Cut,Ink);
        Line(Target,Inset,Inset+Cut,Inset+Cut,Inset,Ink);
        Line(Target,End-Cut,Inset,End,Inset+Cut,Ink);
        Line(Target,Inset,End-Cut,Inset+Cut,End,Ink);
        Line(Target,End-Cut,End,End,End-Cut,Ink);
    }
    inline Image Frame(bool Selected=false,bool Silver=false)
    {
        Image Result(96,96);
        const Pixel Edge=Silver ? Pixel(119,141,166) : Pixel(167,123,47);
        const Pixel Highlight=Selected ? Pixel(255,237,151) : (Silver ? Pixel(202,217,233) : Pixel(240,200,105));
        for (int Y=0; Y<96; ++Y) for (int X=0; X<96; ++X)
        {
            const int NearX=std::min(X,95-X),NearY=std::min(Y,95-Y);
            if (NearX+NearY<9) continue;
            const float T=Y/95.f;
            Pixel Base=Mix(Pixel(14,26,43,250),Pixel(4,10,20,250),T);
            if (NearY>10 && NearX>10 && Y<35) Base=Mix(Base,Pixel(30,46,63,250),0.18f);
            Put(Result,X,Y,Base);
            if (Selected)
            {
                const int Distance=std::min(NearX,NearY);
                if (Distance<8) Put(Result,X,Y,Pixel(255,193,49,(8-Distance)*15));
            }
        }
        Outline(Result,1,7,Pixel(2,5,10,230));
        if (Selected) Outline(Result,2,6,Pixel(255,189,34,150));
        Outline(Result,3,5,Edge);
        Outline(Result,4,5,Highlight);
        Outline(Result,5,5,Pixel(66,47,22));
        Outline(Result,7,4,Silver ? Pixel(43,60,80) : Pixel(102,77,35));
        Line(Result,13,8,82,8,Silver ? Pixel(121,140,164,95) : Pixel(232,199,106,110));
        // Mirrored metal corner fittings: inset diamond, two studs and engraved arms.
        for (int Corner=0; Corner<4; ++Corner)
        {
            const auto Mark=[&](int X,int Y,Pixel Ink) {
                Put(Result,(Corner&1) ? 95-X : X,(Corner&2) ? 95-Y : Y,Ink);
            };
            for (int Y=4; Y<=12; ++Y) for (int X=4; X<=12; ++X)
            {
                const int Distance=std::abs(X-8)+std::abs(Y-8);
                if (Distance<=4) Mark(X,Y,Distance==4 ? Edge : (Distance==3 ? Pixel(42,29,14) : Highlight));
            }
            Mark(8,8,Pixel(255,247,199));
            for (int I=8; I<16; ++I) { Mark(I,3,Highlight); Mark(3,I,Highlight); }
            Mark(13,10,Edge); Mark(10,13,Edge); Mark(14,11,Highlight); Mark(11,14,Highlight);
        }
        return Result;
    }
    inline Image Bar(bool Filled)
    {
        Image Result(32,16);
        for (int Y=0; Y<16; ++Y) for (int X=0; X<32; ++X)
        {
            if (Filled)
            {
                Pixel Ink=Mix(Pixel(252,255,255),Pixel(110,129,146),Y/15.f);
                if (Y==1) Ink=Pixel(255,255,255);
                if (Y==0 || Y==15 || X==0 || X==31) Ink=Pixel(85,103,120);
                Put(Result,X,Y,Ink);
            }
            else
            {
                const bool Rim=X==0 || Y==0 || X==31 || Y==15;
                Put(Result,X,Y,Rim ? Pixel(89,103,119) : Mix(Pixel(7,12,20),Pixel(29,40,53),Y/15.f));
            }
        }
        return Result;
    }
    inline Image Halo()
    {
        Image Result(96,96);
        for (int Y=0; Y<96; ++Y) for (int X=0; X<96; ++X)
        {
            const int NearX=std::min(X,95-X),NearY=std::min(Y,95-Y);
            if (NearX+NearY<7) continue;
            const float Distance=static_cast<float>(std::min(NearX,NearY)-7);
            const int Alpha=static_cast<int>(170.f*std::exp(-Distance*Distance/13.f));
            if (Alpha>0) Put(Result,X,Y,Pixel(255,181,33,Alpha));
        }
        return Result;
    }
    inline Image AmpPoint()
    {
        Image Result(24,24);
        for (int Y=0; Y<24; ++Y) for (int X=0; X<24; ++X)
        {
            const float DX=X-11.5f,DY=Y-11.5f;
            const float Radius=std::sqrt(DX*DX+DY*DY);
            if (Radius>10.f) continue;
            Pixel Ink=Radius>8.5f ? Pixel(122,79,20) : (Radius>7.2f ? Pixel(255,235,153) : Mix(Pixel(255,232,89),Pixel(199,133,16),Y/23.f));
            if (DX*DX+(DY+3.f)*(DY+3.f)<12.f) Ink=Pixel(255,255,220);
            Put(Result,X,Y,Ink);
        }
        return Result;
    }
}
