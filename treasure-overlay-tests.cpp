#include "treasure-overlay-policy.hpp"
#include <climits>
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

int main() {
    using namespace TreasureOverlayPolicy;
    try {
        for (const auto* allowed : {
            HomeUrl, L"https://www.dofusdb.fr/fr/tools/treasure-hunt?x=2#clue",
            L"HTTPS://DOFUSDB.FR/fr", L"https://dofusdb.fr:443/", L"about:blank" })
            Check(AllowedNavigation(allowed), "Official HTTPS navigation must work");
        for (const auto* denied : {
            L"", L"http://dofusdb.fr", L"https://dofusdb.fr.example.com/",
            L"https://dofusdb.fr@evil.example/", L"https://evil.example@dofusdb.fr/",
            L"https://dofusdb.fr:444/", L"https://dofusdb.fr\\@evil.example/",
            L"javascript:alert(1)", L"file:///C:/Windows/", L"data:text/html,test",
            L"about:blank?external", L" https://dofusdb.fr", L"https://api.dofusdb.fr/",
            L"https://dofusdb.fr./", L"https://dofusdb.fr\n/" })
            Check(!AllowedNavigation(denied), "Untrusted top-level navigation must be denied");

        const Rect work{ -1920, 0, 0, 1040 };
        const auto moved = FitToWorkArea({ 3000, 3000, 3840, 3760 }, work);
        Check(moved.left == -840 && moved.top == 280 && moved.right == 0 &&
            moved.bottom == 1040, "Off-monitor position must be recovered");
        const auto oversized = FitToWorkArea({ -3000, -3000, 3000, 3000 }, work);
        Check(oversized.left == -1920 && oversized.top == 0 &&
            oversized.right == 0 && oversized.bottom == 1040, "Oversize must fit");
        const auto small = FitToWorkArea({ 10, 10, 15, 15 }, { 0, 0, 400, 300 });
        Check(small.left == 0 && small.top == 0 && small.right == 400 &&
            small.bottom == 300, "Small work areas override the minimum size");
        const auto malformed = FitToWorkArea({ INT_MAX, INT_MAX, INT_MIN, INT_MIN },
            { 0, 0, 1920, 1080 });
        Check(malformed.right - malformed.left == MinimumWidth &&
            malformed.bottom - malformed.top == MinimumHeight, "Corrupt saved geometry must be bounded");
        std::cout << "Treasure overlay policy tests passed\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
