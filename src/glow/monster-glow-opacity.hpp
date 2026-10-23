#pragma once

namespace monsterGlow {
// Match the native mask's alpha cutoff. Normal game translucency (for example
// AnimatedEntity.rgz's 0.7 cap) is still visible and must not be excluded.
inline constexpr float GlowAlphaClip = 0.02f;
}
