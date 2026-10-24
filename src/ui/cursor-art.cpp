#include "ui/cursor-art.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <utility>

// The C++ port of the design's reference renderer (cursor_art.py, numpy): the
// same shapes, layers and colours, every value computed in double precision in
// the reference's order and rounded half to even like numpy, so both give the
// same bytes (a value within rounding of a half could at most move one unit).
// Only exact shortcuts are added: a primitive is measured only near its box, a
// polygon's length only on its nearest edges, the bevel only in its band, and
// nothing is painted under the topmost opaque layer, which hides it whole.
namespace cursorArt {
namespace {
// ---------------------------------------------------------------- colours
// 0..255 per channel, mixed like the reference: a + (b − a)·t.
struct Rgb { double r = 0, g = 0, b = 0; };
constexpr Rgb Unpack(uint32_t hex) {
    return {static_cast<double>(hex >> 16 & 0xFF), static_cast<double>(hex >> 8 & 0xFF), static_cast<double>(hex & 0xFF)};
}
constexpr Rgb White = Unpack(0xFFFFFF), Black = Unpack(0x000000), Red = Unpack(0xE8404E), Sand = Unpack(0xFFD336);
Rgb Mix(const Rgb& a, const Rgb& b, double t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
// numpy rounds half to even; this does too, whatever the floating-point environment.
double RoundHalfEven(double value) {
    const double floor = std::floor(value), rest = value - floor;
    if (rest != 0.5) return rest < 0.5 ? floor : floor + 1;
    return std::fmod(floor, 2.0) == 0 ? floor : floor + 1;
}
uint8_t Byte(double value) { return static_cast<uint8_t>(RoundHalfEven(std::clamp(value, 0.0, 255.0))); }
uint32_t Pack(const Rgb& colour) { return uint32_t{Byte(colour.r)} << 16 | uint32_t{Byte(colour.g)} << 8 | Byte(colour.b); }

// ---------------------------------------------------------------- palettes
// The gems: a Dofus Émeraude, or gold (#FFC700 is the game's kama yellow) on green.
constexpr uint32_t EmeraldGem[] = {0xB9F6D2, 0x2FCB78, 0x0E6B3D}, GoldGem[] = {0xFFF0B0, 0xFFC700, 0x9A6A00};
constexpr Palette Fixed(uint32_t light, uint32_t mid, uint32_t deep, uint32_t outline, const uint32_t (&gem)[3]) {
    return {light, mid, deep, outline, gem[0], gem[1], gem[2]};
}
// Relative luminance, from sRGB.
double Luminance(const Rgb& colour) {
    const auto linear = [](double c) {
        c /= 255.0;
        return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(colour.r) + 0.7152 * linear(colour.g) + 0.0722 * linear(colour.b);
}
// Hue in degrees and saturation, as HSV has them.
std::pair<double, double> HueSaturation(const Rgb& colour) {
    const double r = colour.r / 255.0, g = colour.g / 255.0, b = colour.b / 255.0;
    const double high = std::max({r, g, b}), low = std::min({r, g, b});
    if (high == low) return {0.0, 0.0};
    const double range = high - low;
    double hue = 0;
    if (high == r) {
        hue = std::fmod((g - b) / range, 6.0);
        hue = 60 * (hue < 0 ? hue + 6.0 : hue);
    } else if (high == g) {
        hue = 60 * ((b - r) / range + 2);
    } else {
        hue = 60 * ((r - g) / range + 4);
    }
    return {hue, range / high};
}
// « Couleur du thème du jeu »: the accent, lightened by steps of 0.025 towards
// white while its luminance is below 0.30 so that a dark theme still gives a
// visible cursor; a tint of it, a shade towards the theme's dark background,
// and that background darkened as the outline. A green accent gets the gold gem.
Palette ThemePalette(uint32_t accentHex, uint32_t darkHex) {
    Rgb accent = Unpack(accentHex);
    const Rgb dark = Unpack(darkHex);
    double lighten = 0.0;
    while (Luminance(Mix(accent, White, lighten)) < 0.30 && lighten < 1.0) lighten += 0.025;
    accent = Mix(accent, White, lighten);
    const auto [hue, saturation] = HueSaturation(accent);
    const auto& gem = hue >= 90 && hue <= 170 && saturation > 0.35 ? GoldGem : EmeraldGem;
    return {Pack(Mix(accent, White, 0.45)), Pack(accent), Pack(Mix(accent, dark, 0.35)), Pack(Mix(dark, Black, 0.35)),
        gem[0], gem[1], gem[2]};
}

// ---------------------------------------------------------------- signed distances, in grid units
struct Point { double x = 0, y = 0; };
double Length(double x, double y) { return std::hypot(x, y); }
// From the segment ab to (x, y).
Point Offset(double x, double y, const Point& a, const Point& b) {
    const double pax = x - a.x, pay = y - a.y, bax = b.x - a.x, bay = b.y - a.y;
    const double h = std::clamp((pax * bax + pay * bay) / (bax * bax + bay * bay + 1e-12), 0.0, 1.0);
    return {pax - bax * h, pay - bay * h};
}
double Segment(double x, double y, const Point& a, const Point& b) {
    const Point offset = Offset(x, y, a, b);
    return Length(offset.x, offset.y);
}
enum class Kind : uint8_t { Polygon, Capsule, RoundRect, Circle, Ring, Ellipse };
constexpr int MaxVertices = 7;
// One shape, exact but for the ellipse, and its box. An ellipse's distance can
// fall short of the true one by its axes' ratio: `reach` widens its box's margins.
struct Primitive {
    Kind kind = Kind::Circle;
    const Point* points = nullptr;  // polygon (even-odd)
    int count = 0;
    Point a, b;            // capsule: its ends; round rectangle: its centre and half size less the radius; others: the centre (a)
    double radius = 0;     // capsule, round rectangle, circle, ring (its middle)
    double width = 0;      // ring: half its width; capsule: the radius of the circle around `cut` it is clipped to
    Point cut;
    double rx = 0, ry = 0, cos = 1, sin = 0;  // ellipse
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0, reach = 1;
};
constexpr double Radians = 3.14159265358979323846 / 180.0;
Primitive Box(Primitive primitive, double x0, double y0, double x1, double y1) {
    primitive.x0 = x0, primitive.y0 = y0, primitive.x1 = x1, primitive.y1 = y1;
    return primitive;
}
template <size_t N> Primitive Polygon(const Point (&points)[N]) {
    static_assert(N >= 3 && N <= MaxVertices);
    Primitive polygon{Kind::Polygon, points, static_cast<int>(N)};
    polygon.x0 = polygon.x1 = points[0].x, polygon.y0 = polygon.y1 = points[0].y;
    for (const auto& point : points) {
        polygon.x0 = std::min(polygon.x0, point.x), polygon.x1 = std::max(polygon.x1, point.x);
        polygon.y0 = std::min(polygon.y0, point.y), polygon.y1 = std::max(polygon.y1, point.y);
    }
    return polygon;
}
Primitive Capsule(Point a, Point b, double radius) {
    Primitive capsule{Kind::Capsule};
    capsule.a = a, capsule.b = b, capsule.radius = radius;
    return Box(capsule, std::min(a.x, b.x) - radius, std::min(a.y, b.y) - radius, std::max(a.x, b.x) + radius,
        std::max(a.y, b.y) + radius);
}
// A capsule cut to a circle; its own box bounds the cut too.
Primitive Cut(Primitive capsule, Point centre, double radius) {
    capsule.cut = centre, capsule.width = radius;
    return capsule;
}
Primitive RoundRect(double x0, double y0, double x1, double y1, double radius) {
    Primitive rect{Kind::RoundRect};
    rect.a = {(x0 + x1) / 2, (y0 + y1) / 2}, rect.b = {(x1 - x0) / 2 - radius, (y1 - y0) / 2 - radius}, rect.radius = radius;
    return Box(rect, x0, y0, x1, y1);
}
Primitive Circle(Point centre, double radius) {
    Primitive circle{Kind::Circle};
    circle.a = centre, circle.radius = radius;
    return Box(circle, centre.x - radius, centre.y - radius, centre.x + radius, centre.y + radius);
}
// |circle| − width: a ring of 2·width around the circle.
Primitive Ring(Point centre, double radius, double width) {
    Primitive ring = Circle(centre, radius + width);
    ring.kind = Kind::Ring, ring.radius = radius, ring.width = width;
    return ring;
}
// Distance (|q/r| − 1)·min(rx, ry), q the point in the ellipse's own axes.
Primitive Ellipse(Point centre, double rx, double ry, double degrees = 0.0) {
    const double extent = std::max(rx, ry);
    Primitive ellipse = Box(Primitive{Kind::Ellipse}, centre.x - extent, centre.y - extent, centre.x + extent, centre.y + extent);
    ellipse.a = centre, ellipse.rx = rx, ellipse.ry = ry, ellipse.reach = extent / std::min(rx, ry);
    ellipse.cos = std::cos(degrees * Radians), ellipse.sin = std::sin(degrees * Radians);
    return ellipse;
}
double Distance(const Primitive& shape, double x, double y) {
    switch (shape.kind) {
    case Kind::Polygon: {
        // The nearest edge's length; only the edges whose squared length is
        // within rounding of the smallest can be it, so only they are measured.
        Point offsets[MaxVertices];
        double squares[MaxVertices], nearest = 1e300;
        bool inside = false;
        for (int k = 0; k < shape.count; ++k) {
            const Point& a = shape.points[k];
            const Point& b = shape.points[(k + 1) % shape.count];
            offsets[k] = Offset(x, y, a, b);
            squares[k] = offsets[k].x * offsets[k].x + offsets[k].y * offsets[k].y;
            nearest = std::min(nearest, squares[k]);
            if ((a.y > y) != (b.y > y) && x < (b.x - a.x) * (y - a.y) / (b.y - a.y + 1e-12) + a.x) inside = !inside;
        }
        double distance = 1e9;
        for (int k = 0; k < shape.count; ++k)
            if (squares[k] <= nearest * (1 + 1e-9)) distance = std::min(distance, Length(offsets[k].x, offsets[k].y));
        return inside ? -distance : distance;
    }
    case Kind::Capsule: {
        const double distance = Segment(x, y, shape.a, shape.b) - shape.radius;
        return shape.width > 0 ? std::max(distance, Length(x - shape.cut.x, y - shape.cut.y) - shape.width) : distance;
    }
    case Kind::RoundRect: {
        const double qx = std::abs(x - shape.a.x) - shape.b.x, qy = std::abs(y - shape.a.y) - shape.b.y;
        return Length(std::max(qx, 0.0), std::max(qy, 0.0)) + std::min(std::max(qx, qy), 0.0) - shape.radius;
    }
    case Kind::Circle: return Length(x - shape.a.x, y - shape.a.y) - shape.radius;
    case Kind::Ring: return std::abs(Length(x - shape.a.x, y - shape.a.y) - shape.radius) - shape.width;
    case Kind::Ellipse: {
        const double dx = x - shape.a.x, dy = y - shape.a.y;
        const double u = dx * shape.cos + dy * shape.sin, v = -dx * shape.sin + dy * shape.cos;
        return (Length(u / shape.rx, v / shape.ry) - 1.0) * std::min(shape.rx, shape.ry);
    }
    }
    return 1e9;
}

// ---------------------------------------------------------------- cursors
enum class Fill : uint8_t { Body, LightBody, DeepBody, Gem, Spec, White, Red, Dark, Sand };
// A part: the union of its primitives, turned and scaled about the grid's
// centre for the diagonal resize arrows, with its fill and the width of the
// outline-coloured seam drawn under it.
struct Part {
    std::array<Primitive, 3> terms{};
    int count = 0;
    Fill fill = Fill::Body;
    double seam = 0;
    bool turned = false;
    double cos = 1, sin = 0, scale = 1;
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0, reach = 1;
};
double Union(const Part& part, double x, double y) {
    double distance = Distance(part.terms[0], x, y);
    for (int k = 1; k < part.count; ++k) distance = std::min(distance, Distance(part.terms[k], x, y));
    return distance;
}
double Distance(const Part& part, double x, double y) {
    if (!part.turned) return Union(part, x, y);
    const double dx = (x - 16.0) / part.scale, dy = (y - 16.0) / part.scale;
    const double u = dx * part.cos + dy * part.sin, v = -dx * part.sin + dy * part.cos;
    return Union(part, u + 16.0, v + 16.0) * part.scale;
}
// Whether a shape can be nearer than `margin` to (x, y): beyond its box and
// margins its distance is larger (exact distances, ellipses with their reach).
template <class Boxed> bool Near(const Boxed& shape, double x, double y, double margin) {
    margin = margin * shape.reach + 1e-6;
    return x >= shape.x0 - margin && x <= shape.x1 + margin && y >= shape.y0 - margin && y <= shape.y1 + margin;
}
constexpr double Far = 1e9;
// The part's distance, exact wherever it is below `margin`; elsewhere a value
// of at least `margin`: the primitives farther than that are not measured.
double Measure(const Part& part, double x, double y, double margin) {
    if (!Near(part, x, y, margin)) return Far;
    if (part.turned) {
        const double dx = (x - 16.0) / part.scale, dy = (y - 16.0) / part.scale;
        x = dx * part.cos + dy * part.sin + 16.0, y = -dx * part.sin + dy * part.cos + 16.0;
        margin /= part.scale;
    }
    double distance = Far;
    for (int k = 0; k < part.count; ++k)
        if (Near(part.terms[k], x, y, margin)) distance = std::min(distance, Distance(part.terms[k], x, y));
    return part.turned && distance < Far ? distance * part.scale : distance;
}
constexpr int MaxParts = 12;
// A cursor: its parts back to front, the first `silhouette` of them (the shape
// and a badge's disc) giving its outline and shadow; its hotspot in units.
struct Design {
    std::array<Part, MaxParts> parts{};
    int count = 0, silhouette = 0;
    Point hot;
    double outline = 1.25;
    Part& Add(Fill fill, double seam, std::initializer_list<Primitive> terms) {
        Part& part = parts[count++];
        part.fill = fill, part.seam = seam;
        for (const auto& term : terms) {
            part.terms[part.count] = term;
            part.x0 = part.count ? std::min(part.x0, term.x0) : term.x0, part.y0 = part.count ? std::min(part.y0, term.y0) : term.y0;
            part.x1 = part.count ? std::max(part.x1, term.x1) : term.x1, part.y1 = part.count ? std::max(part.y1, term.y1) : term.y1;
            part.reach = std::max(part.reach, term.reach);
            ++part.count;
        }
        return part;
    }
    // Turns the last part by `degrees` and scales it about (16, 16); its box follows its corners.
    void Turn(double degrees, double scale) {
        Part& part = parts[count - 1];
        part.turned = true, part.cos = std::cos(degrees * Radians), part.sin = std::sin(degrees * Radians), part.scale = scale;
        const double xs[] = {part.x0, part.x1}, ys[] = {part.y0, part.y1};
        double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
        for (const double x : xs)
            for (const double y : ys) {
                const double wx = 16.0 + scale * (part.cos * (x - 16.0) - part.sin * (y - 16.0));
                const double wy = 16.0 + scale * (part.sin * (x - 16.0) + part.cos * (y - 16.0));
                x0 = std::min(x0, wx), x1 = std::max(x1, wx), y0 = std::min(y0, wy), y1 = std::max(y1, wy);
            }
        part.x0 = x0, part.y0 = y0, part.x1 = x1, part.y1 = y1;
    }
};

// The shapes, as the visual spec gives them (cursor_art.py's *_parts).
constexpr Point ArrowBody[] = {{2, 2}, {2, 23.25}, {7, 18.25}, {11.1, 27.25}, {14.25, 25.9}, {10.25, 17.1}, {17, 17}};
constexpr Point ResizeLeft[] = {{3.0, 16.0}, {10.2, 9.8}, {10.2, 22.2}}, ResizeRight[] = {{29.0, 16.0}, {21.8, 22.2}, {21.8, 9.8}};
constexpr Point GlassTop[] = {{20.4, 19.4}, {25.6, 19.4}, {23.0, 23.0}}, GlassBottom[] = {{23.0, 23.0}, {25.6, 26.6}, {20.4, 26.6}};
constexpr Point SandPile[] = {{21.5, 26.55}, {24.5, 26.55}, {23.0, 24.7}};
// « sélection »: a golden arrow with an emerald and its glint.
void Arrow(Design& design) {
    design.Add(Fill::Body, 0, {Polygon(ArrowBody)});
    design.Add(Fill::Gem, 0.6, {Ellipse({6.7, 12.9}, 1.9, 2.5, -15)});
    design.Add(Fill::Spec, 0, {Ellipse({6.1, 11.8}, 0.55, 0.8, -15)});
    design.hot = {2.0, 2.0};
}
// « lien »: a gauntlet pointing up, the gem set in its cuff; the hotspot at the fingertip.
void PointingHand(Design& design) {
    design.Add(Fill::Body, 0, {RoundRect(8.5, 13.5, 25.5, 25.5, 4.5)});
    design.Add(Fill::DeepBody, 0.75, {RoundRect(10.0, 24.5, 24.0, 29.4, 1.6)});
    design.Add(Fill::Body, 0.75, {Capsule({6.6, 15.0}, {10.6, 20.4}, 2.3)});    // thumb
    design.Add(Fill::Body, 0.75, {Capsule({23.4, 13.8}, {23.4, 18.4}, 2.0)});   // little finger
    design.Add(Fill::Body, 0.75, {Capsule({19.9, 12.6}, {19.9, 18.0}, 2.15)});  // ring finger
    design.Add(Fill::Body, 0.75, {Capsule({16.3, 11.9}, {16.3, 17.8}, 2.25)});  // middle finger
    design.Add(Fill::Body, 0.75, {Capsule({12.5, 4.5}, {12.5, 17.0}, 2.3)});    // index
    design.Add(Fill::Gem, 0.55, {Ellipse({17.0, 26.95}, 1.7, 1.25)});
    design.hot = {12.5, 2.2};
}
// « saisir »: the open gauntlet.
void OpenHand(Design& design) {
    design.Add(Fill::Body, 0, {RoundRect(9.0, 14.0, 24.0, 25.0, 4.5)});
    design.Add(Fill::DeepBody, 0.75, {RoundRect(10.5, 24.0, 22.5, 29.0, 1.6)});
    design.Add(Fill::Body, 0.75, {Capsule({5.4, 12.2}, {10.4, 19.6}, 2.2)});    // thumb
    design.Add(Fill::Body, 0.75, {Capsule({24.2, 8.4}, {22.3, 16.0}, 1.85)});   // little finger
    design.Add(Fill::Body, 0.75, {Capsule({19.7, 5.0}, {18.9, 15.0}, 2.05)});   // ring finger
    design.Add(Fill::Body, 0.75, {Capsule({14.6, 4.1}, {15.1, 15.0}, 2.1)});    // middle finger
    design.Add(Fill::Body, 0.75, {Capsule({9.6, 6.2}, {11.6, 15.5}, 2.05)});    // index
    design.Add(Fill::Gem, 0.55, {Ellipse({16.5, 26.5}, 1.7, 1.25)});
    design.hot = {16.0, 16.0};
}
// « saisi »: the closed gauntlet, its knuckles back to front.
void ClosedHand(Design& design) {
    design.Add(Fill::Body, 0, {RoundRect(8.5, 12.5, 24.5, 25.0, 5.0)});
    design.Add(Fill::DeepBody, 0.75, {RoundRect(10.0, 24.0, 23.0, 29.0, 1.6)});
    design.Add(Fill::Body, 0.75, {Capsule({21.8, 13.0}, {21.6, 16.0}, 2.0)});
    design.Add(Fill::Body, 0.75, {Capsule({18.3, 11.9}, {18.4, 15.2}, 2.2)});
    design.Add(Fill::Body, 0.75, {Capsule({14.4, 11.6}, {15.0, 15.0}, 2.3)});
    design.Add(Fill::Body, 0.75, {Capsule({10.6, 12.6}, {12.0, 15.5}, 2.2)});
    design.Add(Fill::Body, 0.75, {Capsule({8.0, 18.0}, {14.5, 19.0}, 2.1)});    // thumb
    design.Add(Fill::Gem, 0.55, {Ellipse({16.5, 26.5}, 1.7, 1.25)});
    design.hot = {16.0, 16.0};
}
// The text cursor: a light I-beam with a thinner outline.
void Beam(Design& design) {
    design.Add(Fill::LightBody, 0, {RoundRect(15.0, 6.5, 17.0, 25.5, 0.6), RoundRect(11.5, 5.0, 20.5, 7.0, 1.0),
        RoundRect(11.5, 25.0, 20.5, 27.0, 1.0)});
    design.hot = {16.0, 16.0};
    design.outline = 1.0;
}
// The double arrow, left-right, turned for the other directions.
void Resize(Design& design, double degrees, double scale) {
    design.Add(Fill::Body, 0, {RoundRect(9.0, 14.7, 23.0, 17.3, 0.8), Polygon(ResizeLeft), Polygon(ResizeRight)});
    design.Turn(degrees, scale);
    design.hot = {16.0, 16.0};
}
// Badges, at the bottom right: their disc joins the silhouette.
void Forbidden(Design& design) {
    design.Add(Fill::White, 0, {Circle({23.0, 23.0}, 6.2)});
    design.Add(Fill::Red, 0, {Ring({23.0, 23.0}, 4.25, 1.0), Cut(Capsule({20.0, 20.0}, {26.0, 26.0}, 1.0), {23.0, 23.0}, 5.25)});
}
void Busy(Design& design) {
    design.Add(Fill::Dark, 0, {Circle({23.0, 23.0}, 6.2)});
    design.Add(Fill::LightBody, 0, {Polygon(GlassTop), Polygon(GlassBottom)});
    design.Add(Fill::Sand, 0, {Polygon(SandPile)});
    design.Add(Fill::Body, 0, {RoundRect(19.8, 18.3, 26.2, 19.5, 0.5), RoundRect(19.8, 26.5, 26.2, 27.7, 0.5)});
}
Design DesignOf(Shape shape) {
    Design design;
    switch (shape) {
    case Shape::Hand: case Shape::HandForbidden: case Shape::HandBusy: PointingHand(design); break;
    case Shape::Beam: Beam(design); break;
    case Shape::Open: OpenHand(design); break;
    case Shape::Closed: ClosedHand(design); break;
    case Shape::ResizeEW: Resize(design, 0, 1.0); break;
    case Shape::ResizeNS: Resize(design, 90, 1.0); break;
    case Shape::ResizeNWSE: Resize(design, 45, 0.94); break;
    case Shape::ResizeNESW: Resize(design, -45, 0.94); break;
    default: Arrow(design); break;  // the arrows, and the arrow for an unknown value
    }
    const int shapeParts = design.count;
    if (shape == Shape::ArrowForbidden || shape == Shape::HandForbidden) Forbidden(design);
    if (shape == Shape::ArrowBusy || shape == Shape::HandBusy) Busy(design);
    design.silhouette = design.count > shapeParts ? shapeParts + 1 : shapeParts;
    return design;
}

// ---------------------------------------------------------------- painting
// The palette as the fills use it.
struct Colours {
    Rgb light, mid, deep, outline, gemLight, gemMid, gemDeep, dark, shade;
    explicit Colours(const Palette& palette)
        : light(Unpack(palette.light)), mid(Unpack(palette.mid)), deep(Unpack(palette.deep)), outline(Unpack(palette.outline)),
          gemLight(Unpack(palette.gemLight)), gemMid(Unpack(palette.gemMid)), gemDeep(Unpack(palette.gemDeep)),
          dark(Mix(outline, White, 0.08)), shade{deep.r * 0.85, deep.g * 0.85, deep.b * 0.85} {}
};
// Light, mid then deep along the gradient.
Rgb Ramp(const Rgb& light, const Rgb& mid, const Rgb& deep, double t) {
    return t < 0.5 ? Mix(light, mid, t / 0.5) : Mix(mid, deep, (t - 0.5) / 0.5);
}
double Opacity(Fill fill) { return fill == Fill::Spec ? 0.88 : fill == Fill::White ? 0.94 : 1.0; }
// A fill's colour at a point d inside the part, t along the gradient. Body,
// gem and I-beam fills get a bevel lit from the top-left in a 1.1-unit band.
Rgb Shade(const Part& part, const Colours& colours, double d, double x, double y, double t) {
    Rgb colour;
    switch (part.fill) {
    case Fill::Body: colour = Ramp(colours.light, colours.mid, colours.deep, t); break;
    case Fill::LightBody: colour = Mix(colours.light, colours.mid, t * 0.6); break;
    case Fill::DeepBody: colour = Mix(colours.mid, colours.deep, 0.4 + 0.6 * t); break;
    case Fill::Gem: colour = Ramp(colours.gemLight, colours.gemMid, colours.gemDeep, t); break;
    case Fill::Red: return Red;
    case Fill::Dark: return colours.dark;
    case Fill::Sand: return Sand;
    default: return White;  // the glint and the forbidden badge's disc
    }
    if (d < 0 && d > -1.1) {
        constexpr double e = 0.25;
        double nx = Distance(part, x + e, y) - Distance(part, x - e, y);
        double ny = Distance(part, x, y + e) - Distance(part, x, y - e);
        const double length = Length(nx, ny) + 1e-9;
        nx = nx / length, ny = ny / length;
        const double lit = nx * -0.6 + ny * -0.8;
        const double band = std::clamp(1 - (-d) / 1.1, 0.0, 1.0);
        colour = Mix(colour, White, std::clamp(lit, 0.0, 1.0) * band * 0.55);
        colour = Mix(colour, colours.shade, std::clamp(-lit, 0.0, 1.0) * band * 0.35);
    }
    return colour;
}
// "Over" in straight alpha, as the reference composes its layers.
struct Paint {
    Rgb colour;
    double alpha = 0;
    void Over(const Rgb& top, double opacity) {
        if (opacity <= 0) return;
        const double keep = alpha * (1 - opacity), out = opacity + keep, scale = std::max(out, 1e-6);
        colour = {(top.r * opacity + colour.r * keep) / scale, (top.g * opacity + colour.g * keep) / scale,
            (top.b * opacity + colour.b * keep) / scale};
        alpha = out;
    }
};
}

Palette PaletteOf(Colour colour, uint32_t themeAccent, uint32_t themeDark) {
    switch (colour) {
    case Colour::Theme: return ThemePalette(themeAccent, themeDark);
    case Colour::Emerald: return Fixed(0xC8F7DA, 0x3FD47A, 0x138A4A, 0x06240F, GoldGem);
    case Colour::Silver: return Fixed(0xFFFFFF, 0xDDE2EC, 0x8E97AB, 0x1B1D32, EmeraldGem);
    default: return Fixed(0xFFF3C4, 0xFFD336, 0xB97A12, 0x2A1806, EmeraldGem);  // « Or », and Or for an unknown value
    }
}

// Pixel (i, j) from the top-left is sampled at the grid point ((i + ½)/s, (j + ½)/s),
// s = size/32, and covered by cov(d) = clamp(½ − d·s, 0, 1). Layers, bottom to
// top: the shadow (the silhouette moved by (0.9, 1.3), soft over 3 units, 35 %
// black), the outline (the silhouette grown by 1.25 units, 1 for the I-beam),
// then each part's seam and fill.
Image Render(Shape shape, const Palette& palette, int size) {
    Image image;
    size = std::clamp(size, MinSize, MaxSize);
    const Design design = DesignOf(shape);
    const Colours colours(palette);
    const double scale = size / 32.0, unit = 1.0 / scale;
    image.size = size;
    image.hotX = static_cast<int>(std::floor(design.hot.x * scale));
    image.hotY = static_cast<int>(std::floor(design.hot.y * scale));
    image.rgba.assign(static_cast<size_t>(size) * size * 4, 0);
    const auto centre = [unit](int index) { return (index + 0.5) * unit; };
    const auto cover = [unit](double d) { return std::clamp(0.5 - d / unit, 0.0, 1.0); };
    // Beyond `reach` of a part none of its layers shows, nor the outline;
    // beyond 1.5 units the silhouette casts no shadow. The pixels to paint lie
    // in the parts' boxes widened by these margins.
    double maxSeam = 0;
    for (int k = 0; k < design.count; ++k) maxSeam = std::max(maxSeam, design.parts[k].seam);
    const double reach = std::max(design.outline, maxSeam) + 0.5 * unit;
    double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
    for (int k = 0; k < design.count; ++k) {
        const Part& part = design.parts[k];
        const double margin = reach * part.reach + 1e-6, shadow = 1.5 * part.reach + 1e-6;
        x0 = std::min(x0, part.x0 - margin), y0 = std::min(y0, part.y0 - margin);
        x1 = std::max(x1, part.x1 + margin), y1 = std::max(y1, part.y1 + margin);
        if (k >= design.silhouette) continue;
        x0 = std::min(x0, part.x0 + 0.9 - shadow), y0 = std::min(y0, part.y0 + 1.3 - shadow);
        x1 = std::max(x1, part.x1 + 0.9 + shadow), y1 = std::max(y1, part.y1 + 1.3 + shadow);
    }
    // Pixel i's centre is at (i + ½)/s: from the first that can lie in [x0, x1] to the last.
    const auto index = [size](double at) { return static_cast<int>(std::clamp(at, 0.0, size - 1.0)); };
    const int left = index(std::floor(x0 * scale - 0.5)), right = index(std::ceil(x1 * scale - 0.5));
    const int top = index(std::floor(y0 * scale - 0.5)), bottom = index(std::ceil(y1 * scale - 0.5));

    // The fills' gradient runs over the box of the pixels whose centre is
    // inside the silhouette: light at its top-left, deep at its bottom-right.
    int insideLeft = size, insideTop = size, insideRight = -1, insideBottom = -1;
    for (int j = top; j <= bottom; ++j)
        for (int i = left; i <= right; ++i) {
            const double x = centre(i), y = centre(j);
            for (int k = 0; k < design.silhouette; ++k)
                if (Measure(design.parts[k], x, y, 0) < 0) {
                    insideLeft = std::min(insideLeft, i), insideRight = std::max(insideRight, i);
                    insideTop = std::min(insideTop, j), insideBottom = std::max(insideBottom, j);
                    break;
                }
        }
    if (insideRight < 0) insideLeft = insideTop = 0, insideRight = insideBottom = size - 1;
    const double gx0 = insideLeft * unit, gy0 = insideTop * unit;
    const double span = ((insideBottom + 1) * unit - gy0) + 0.35 * ((insideRight + 1) * unit - gx0) + 1e-6;

    for (int j = top; j <= bottom; ++j)
        for (int i = left; i <= right; ++i) {
            const double x = centre(i), y = centre(j);
            // Layers: 0 the shadow, 1 the outline, then each part's seam (2 + 2k)
            // and fill (3 + 2k). An opaque layer replaces whatever lies under it,
            // so the parts are measured from the top down to the first opaque one.
            double distance[MaxParts];
            int start = 0;
            for (int k = design.count - 1; k >= 0 && !start; --k) {
                const Part& part = design.parts[k];
                distance[k] = Measure(part, x, y, reach);
                if (cover(distance[k]) * Opacity(part.fill) == 1) start = 3 + 2 * k;
                else if (part.seam > 0 && cover(distance[k] - part.seam) == 1) start = 2 + 2 * k;
            }
            double silhouette = Far;
            if (!start) {
                for (int k = 0; k < design.silhouette; ++k) silhouette = std::min(silhouette, distance[k]);
                if (cover(silhouette - design.outline) == 1) start = 1;
            }
            Paint paint;
            if (start == 0) {
                const double sx = x - 0.9, sy = y - 1.3;
                double shadow = Far;
                for (int k = 0; k < design.silhouette; ++k) shadow = std::min(shadow, Measure(design.parts[k], sx, sy, 1.5));
                paint.Over(Black, 0.35 * std::clamp((1.5 - shadow) / 3.0, 0.0, 1.0));
            }
            if (start <= 1) paint.Over(colours.outline, cover(silhouette - design.outline));
            const double t = std::clamp(((y - gy0) + 0.35 * (x - gx0)) / span, 0.0, 1.0);
            for (int k = std::max(0, (start - 2) / 2); k < design.count; ++k) {
                const Part& part = design.parts[k];
                if (part.seam > 0 && 2 + 2 * k >= start) paint.Over(colours.outline, cover(distance[k] - part.seam));
                const double opacity = cover(distance[k]) * Opacity(part.fill);
                if (opacity > 0) paint.Over(Shade(part, colours, distance[k], x, y, t), opacity);
            }
            // Straight alpha, transparent pixels black, rows bottom-up.
            const uint8_t alpha = Byte(paint.alpha * 255);
            if (!alpha) continue;
            uint8_t* pixel = &image.rgba[(static_cast<size_t>(size - 1 - j) * size + i) * 4];
            pixel[0] = Byte(paint.colour.r), pixel[1] = Byte(paint.colour.g), pixel[2] = Byte(paint.colour.b), pixel[3] = alpha;
        }
    return image;
}
}
