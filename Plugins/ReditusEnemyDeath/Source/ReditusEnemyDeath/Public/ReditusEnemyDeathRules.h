#pragma once
#include <cmath>
#include <cstdint>

// Engine-independent presentation math. No damage, rewards or combat state.
namespace ReditusEnemyDeathRules
{
    inline float Clamp(float v, float lo, float hi, float fallback)
    { return std::isfinite(v) ? (v < lo ? lo : v > hi ? hi : v) : fallback; }
    inline std::uint32_t Hash(int x, int y, int seed)
    {
        std::uint32_t h = std::uint32_t(x) * 1973u + std::uint32_t(y) * 9277u + std::uint32_t(seed) * 26699u;
        h = (h ^ 61u) ^ (h >> 16); h *= 9u; h ^= h >> 4; h *= 0x27d4eb2du; h ^= h >> 15;
        return h;
    }
    inline float Noise(int x, int y, int seed) { return float(Hash(x, y, seed) & 65535u) / 65535.f; }
    inline float FrontPosition(float progress, bool frontOnRight)
    {
        const float p = Clamp(progress, 0.f, 1.f, 0.f) * 1.12f - .06f;
        return Clamp(frontOnRight ? 1.f - p : p, 0.f, 1.f, 0.f);
    }
    // The body shader uses this same continuous sweep, independent of particle count.
    inline float BodyAlpha(float progress, float u, float noise, bool frontOnRight)
    {
        const float distance = (frontOnRight ? 1.f - u : u) + (noise - .5f) * .045f -
            (Clamp(progress, 0.f, 1.f, 0.f) * 1.12f - .06f);
        const float t = Clamp((distance + .018f) / .036f, 0.f, 1.f, 0.f);
        return t * t * (3.f - 2.f * t);
    }
    // Recycle a fixed component pool. Emission times depend on absolute age, not frame rate.
    inline float ParticleBirth(float age, int index, int count, float duration, float life)
    {
        if (!std::isfinite(age) || count <= 0 || index < 0 || index >= count ||
            !std::isfinite(duration) || !std::isfinite(life) || duration <= 0.f || life <= 0.f) return -1.f;
        const float first = .06f + float(index) * life / float(count);
        const float latest = age < duration - .0001f ? age : duration - .0001f;
        if (latest < first) return -1.f;
        return first + std::floor((latest - first) / life) * life;
    }
    inline float FragmentAlpha(float age, float life)
    {
        if (!std::isfinite(age) || !std::isfinite(life) || age < 0.f || life <= 0.f || age >= life) return 0.f;
        const float t = age / life;
        const float fadeIn = Clamp(age / (life * .12f), 0.f, 1.f, 0.f);
        return fadeIn * (1.f - t) * (1.f - t);
    }
    inline float Flash(float elapsed) { return Clamp(1.f - elapsed / .11f, 0.f, 1.f, 0.f); }
    inline bool ShouldDestroySource(bool claimed, bool sameEncounter, bool sourceValid, int hp, bool markedDead)
    { return claimed && sameEncounter && sourceValid && hp <= 0 && markedDead; }
}
