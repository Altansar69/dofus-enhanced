#pragma once
#include <algorithm>
#include <cwctype>
#include <string>
#include <string_view>

namespace TreasureOverlayPolicy {
inline constexpr wchar_t HomeUrl[] = L"https://dofusdb.fr/fr/tools/treasure-hunt";
inline constexpr wchar_t WindowClass[] = L"DofusMiniatures.TreasureOverlay.v1";
inline constexpr wchar_t MutexName[] = L"Local\\DofusMiniatures.TreasureOverlay.v1";
inline constexpr int MinimumWidth = 560;
inline constexpr int MinimumHeight = 420;

// Only top-level navigation is filtered. DofusDB retains its ordinary resources.
inline bool AllowedNavigation(std::wstring_view uri) {
    if (uri == L"about:blank") return true;
    std::wstring normalized(uri);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
        [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    constexpr std::wstring_view prefix = L"https://";
    if (!normalized.starts_with(prefix)) return false;
    const auto end = normalized.find_first_of(L"/?#", prefix.size());
    const auto authority = std::wstring_view(normalized).substr(prefix.size(),
        end == std::wstring::npos ? std::wstring::npos : end - prefix.size());
    return authority == L"dofusdb.fr" || authority == L"www.dofusdb.fr" ||
        authority == L"dofusdb.fr:443" || authority == L"www.dofusdb.fr:443";
}

struct Rect { int left, top, right, bottom; };

inline Rect FitToWorkArea(Rect desired, Rect work) {
    const auto workWidth = std::max(1LL, static_cast<long long>(work.right) - work.left);
    const auto workHeight = std::max(1LL, static_cast<long long>(work.bottom) - work.top);
    const auto width = std::clamp(static_cast<long long>(desired.right) - desired.left,
        std::min<long long>(MinimumWidth, workWidth), workWidth);
    const auto height = std::clamp(static_cast<long long>(desired.bottom) - desired.top,
        std::min<long long>(MinimumHeight, workHeight), workHeight);
    const auto left = std::clamp(static_cast<long long>(desired.left),
        static_cast<long long>(work.left), static_cast<long long>(work.right) - width);
    const auto top = std::clamp(static_cast<long long>(desired.top),
        static_cast<long long>(work.top), static_cast<long long>(work.bottom) - height);
    return { static_cast<int>(left), static_cast<int>(top),
        static_cast<int>(left + width), static_cast<int>(top + height) };
}
} // namespace TreasureOverlayPolicy
