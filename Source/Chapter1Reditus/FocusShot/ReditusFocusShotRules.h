#pragma once

#include <cmath>

// Pure rules shared by the runtime and the offline tests.
namespace ReditusFocusShotRules
{
    enum class EPhase { Waiting, Aiming, Feedback, Finished };
    enum class EResult { Normal, WeakSpot, NoShot };

    inline EResult Judge(bool bFired, bool bHasVisibleWeakSpot, float DistanceSquared, float Radius)
    {
        return bFired && bHasVisibleWeakSpot && Radius > 0.f && DistanceSquared <= Radius * Radius
            ? EResult::WeakSpot : EResult::Normal;
    }

    struct FAimPoint { float X; float Y; };
    struct FAimBounds { float Left; float Top; float Right; float Bottom; };

    // Uniform samples in the visible part of an annulus. Stay inside the
    // radius instead of clamping an off-screen point onto the viewport edge.
    template<typename RandomUnit>
    FAimPoint SampleAimStart(FAimPoint Center, FAimBounds Bounds, float Inner,
        float Outer, RandomUnit NextUnit)
    {
        const float FarX = Center.X - Bounds.Left > Bounds.Right - Center.X ? Bounds.Left : Bounds.Right;
        const float FarY = Center.Y - Bounds.Top > Bounds.Bottom - Center.Y ? Bounds.Top : Bounds.Bottom;
        const float DX = FarX - Center.X;
        const float DY = FarY - Center.Y;
        const float Farthest = std::sqrt(DX * DX + DY * DY);
        if (Farthest <= 0.f) return Center;
        Outer = Outer < Farthest ? Outer : Farthest;
        if (Inner >= Outer) Inner = Outer * 0.94f;
        for (int I = 0; I < 64; ++I)
        {
            const float Angle = 6.28318530718f * NextUnit();
            const float Radius = std::sqrt(Inner * Inner + (Outer * Outer - Inner * Inner) * NextUnit());
            const FAimPoint P{Center.X + Radius * std::cos(Angle), Center.Y + Radius * std::sin(Angle)};
            if (P.X >= Bounds.Left && P.X <= Bounds.Right && P.Y >= Bounds.Top && P.Y <= Bounds.Bottom)
                return P;
        }
        // With a very small viewport the requested inner radius may not fit.
        // The farthest corner provides an in-bounds direction in that case.
        const float Radius = Inner + (Outer - Inner) * (0.1f + 0.8f * NextUnit());
        return {Center.X + DX * (Radius / Farthest), Center.Y + DY * (Radius / Farthest)};
    }

    struct FRun
    {
        EPhase Phase = EPhase::Waiting;
        EResult Result = EResult::NoShot;
        bool Begin()
        {
            if (Phase != EPhase::Waiting) return false;
            Phase = EPhase::Aiming;
            return true;
        }
        bool Lock(EResult InResult)
        {
            if (Phase != EPhase::Aiming) return false;
            Result = InResult;
            Phase = EPhase::Feedback;
            return true;
        }
        bool Complete()
        {
            if (Phase == EPhase::Finished) return false;
            Phase = EPhase::Finished;
            return true;
        }
    };
}
