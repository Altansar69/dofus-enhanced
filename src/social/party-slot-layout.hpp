#pragma once
// Where the mod puts its elements on a party slot, and where the game's own
// elements are, in the slot's parentMember box (the element the mod adds to).
// From the game's partyMemberItem.uxml, its inline styles and partyMember.uss
// (3.7.12.12): parentMember is 88 px square (.Big) or 52 px (.Small); the life
// bar is on the left edge, the leader's crown in the top right corner, the
// follow arrow in the top left corner, the auto-follow / auto-join / auto-ready
// icons (or toggles on the player's own slot) stack up the right edge from the
// bottom, the leader's auto-follow icon sits just left of the bottom right one,
// and the arena status in the bottom right corner. The owner also reported the
// game's sword icon at the left middle (kept clear here as a band). The mod's
// button goes in the free band at the top, between the follow arrow and the
// crown; the spouse's heart in the bottom left corner, right of the life bar.
namespace party_slot {
struct Box { float left, top, width, height; };
constexpr bool Overlap(Box a, Box b) {
    return a.left < b.left + b.width && b.left < a.left + a.width && a.top < b.top + b.height && b.top < a.top + a.height;
}
struct Native { const char* name; Box big, small; };
// The guest hourglass (tx_guest, top right) only shows on invited guests, never
// on the accepted members that get the mod's elements.
inline constexpr Native NativeElements[] = {
    {"pb_lifePoints", {0, 0, 10, 88}, {0, 0, 8, 52}},
    {"tx_followArrow", {10, 0, 16, 16}, {8, 0, 10, 10}},
    {"tx_leaderCrown", {58, 3, 30, 30}, {32, 3, 20, 20}},
    {"ctr_iconToggles", {60, 0, 26, 88}, {36, 0, 14, 52}},
    {"leaderAutoFollow", {32, 60, 28, 28}, {22, 37, 14, 14}},
    {"tx_arenaStatus", {58, 58, 30, 30}, {28, 28, 24, 24}},
    {"sword (left middle)", {0, 30, 44, 30}, {0, 18, 26, 18}}};
constexpr float Slot(bool small) { return small ? 52.0f : 88.0f; }
// The spouse's teleport button and the other members' travel button: the same
// native secondary toggle as the slot's own (26 px, 14 px on a .Small slot).
constexpr float ButtonSize(bool small) { return small ? 14.0f : 26.0f; }
constexpr float ButtonLeft(bool small) { return small ? 18.0f : 29.0f; }
constexpr float ButtonTop = 3.0f;
constexpr Box Button(bool small) { return {ButtonLeft(small), ButtonTop, ButtonSize(small), ButtonSize(small)}; }
// The spouse's heart badge: bottom left, right of the life bar.
constexpr float HeartSize(bool small) { return small ? 12.0f : 16.0f; }
constexpr float HeartLeft(bool small) { return small ? 9.0f : 12.0f; }
constexpr float HeartBottom(bool small) { return small ? 3.0f : 4.0f; }
constexpr Box Heart(bool small) {
    return {HeartLeft(small), Slot(small) - HeartBottom(small) - HeartSize(small), HeartSize(small), HeartSize(small)};
}
constexpr bool Clear(Box box, bool small) {
    if (box.left < 0 || box.top < 0 || box.left + box.width > Slot(small) || box.top + box.height > Slot(small)) return false;
    for (const auto& element : NativeElements) if (Overlap(box, small ? element.small : element.big)) return false;
    return true;
}
static_assert(Clear(Button(false), false) && Clear(Button(true), true) && Clear(Heart(false), false) && Clear(Heart(true), true) &&
    !Overlap(Button(false), Heart(false)) && !Overlap(Button(true), Heart(true)), "The mod's party slot elements cover none of the game's");
}
