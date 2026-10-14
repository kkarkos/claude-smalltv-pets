#include "pet.h"
#include "display.h"

namespace pet {
namespace {

const uint8_t PALETTE[8][3] = {
  {217, 119, 87},   // orange
  {96, 150, 235},   // blue
  {110, 190, 110},  // green
  {160, 120, 220},  // purple
  {235, 130, 170},  // pink
  {80, 190, 190},   // teal
  {230, 190, 80},   // yellow
  {220, 90, 90},    // red
};

constexpr uint16_t C_OUTLINE = rgb(28, 22, 34);
constexpr uint16_t C_EYE     = rgb(24, 18, 30);
constexpr uint16_t C_WHITE   = rgb(250, 248, 240);
constexpr uint16_t C_BLUSH   = rgb(255, 120, 140);
constexpr uint16_t C_HEART   = rgb(240, 70, 90);
constexpr uint16_t C_METAL   = rgb(150, 150, 165);
constexpr uint16_t C_METAL_D = rgb(95, 95, 110);
constexpr uint16_t C_SPARK   = rgb(255, 230, 120);
constexpr uint16_t C_TONGUE  = rgb(230, 90, 100);

uint16_t scale(uint8_t idx, int pct, int add) {
  const uint8_t* p = PALETTE[idx & 7];
  auto ch = [&](int v) { return (uint8_t)constrain(v * pct / 100 + add, 0, 255); };
  return rgb(ch(p[0]), ch(p[1]), ch(p[2]));
}

// One drawing pass: the canvas, where the grid sits, and the cell size.
struct Pen {
  Arduino_GFX* c;
  int ox, oy, cell;
  void px(int x, int y, uint16_t col) const {
    c->fillRect(ox + x * cell, oy + y * cell, cell, cell, col);
  }
  void rect(int x, int y, int w, int h, uint16_t col) const {
    c->fillRect(ox + x * cell, oy + y * cell, w * cell, h * cell, col);
  }
};

// How far each of the first/last rows of the body is pulled in: a soft blob.
const uint8_t TOP_INDENT[] = {4, 2, 1, 1};
const uint8_t BOT_INDENT[] = {2, 1};

void blob(const Pen& p, int left, int right, int top, int bottom, uint16_t col) {
  int h = bottom - top + 1;
  for (int i = 0; i < h; i++) {
    int indent = 0;
    if (i < 4) indent = TOP_INDENT[i];
    int fromBottom = h - 1 - i;
    if (fromBottom < 2) indent = max(indent, (int)BOT_INDENT[fromBottom]);
    p.rect(left + indent, top + i, right - left + 1 - 2 * indent, 1, col);
  }
}

// Small 3x3 'z' at (x, y).
void zee(const Pen& p, int x, int y, uint16_t col) {
  p.rect(x, y, 3, 1, col);
  p.px(x + 1, y + 1, col);
  p.rect(x, y + 2, 3, 1, col);
}

void heart(const Pen& p, int x, int y, uint16_t col) {
  p.px(x + 1, y, col); p.px(x + 3, y, col);
  p.rect(x, y + 1, 5, 1, col);
  p.rect(x + 1, y + 2, 3, 1, col);
  p.px(x + 2, y + 3, col);
}

void sparkle(const Pen& p, int x, int y, uint16_t col) {
  p.px(x, y - 1, col); p.px(x - 1, y, col); p.px(x, y, col);
  p.px(x + 1, y, col); p.px(x, y + 1, col);
}

enum class Eyes : uint8_t { Open, Closed, Happy, Wide, Cross, Down };
enum class Mouth : uint8_t { Smile, Flat, Oh, Open, Wavy, None };

}  // namespace

uint16_t bodyColor(uint8_t idx) { return scale(idx, 100, 0); }

void draw(Arduino_GFX* c, int ox, int oy, int cell, const Mood& m, uint32_t t) {
  Pen p{c, ox, oy, cell};
  t += m.phase * 337u;

  uint16_t body  = m.isEmpty ? rgb(120, 116, 130) : scale(m.color, 100, 0);
  uint16_t shade = m.isEmpty ? rgb(90, 86, 100)   : scale(m.color, 72, 0);
  uint16_t light = m.isEmpty ? rgb(160, 156, 170) : scale(m.color, 80, 70);

  PetState st = m.isEmpty ? PetState::Sleep : m.state;

  // Shape parameters, then per-mood tweaks.
  int bob = 0, squash = 0, lookX = 0, lookY = 0;
  Eyes eyes = Eyes::Open;
  Mouth mouth = Mouth::Smile;
  int armL = 0, armR = 0;          // 0 rest, 1 up, 2 raised high, 3 on keys
  bool blush = false;

  bool isBlink = (t % 3700) < 140;
  uint32_t breath = (t / 900) % 2;

  switch (st) {
    case PetState::Idle: {
      bob = breath;
      uint32_t look = (t / 2600) % 5;
      lookX = (look == 1) ? -1 : (look == 3) ? 1 : 0;
      eyes = isBlink ? Eyes::Closed : Eyes::Open;
      break;
    }
    case PetState::Thinking: {
      bob = breath;
      lookX = 1; lookY = -1;
      eyes = isBlink ? Eyes::Closed : Eyes::Open;
      mouth = Mouth::Flat;
      break;
    }
    case PetState::Working: {
      bob = (t / 180) % 2;
      lookY = 1;
      eyes = isBlink ? Eyes::Closed : Eyes::Down;
      mouth = Mouth::Flat;
      armL = armR = 3;
      break;
    }
    case PetState::Waiting: {
      bob = (t / 400) % 2;
      eyes = Eyes::Wide;
      mouth = Mouth::Oh;
      armR = ((t / 260) % 2) ? 2 : 1;   // waving
      break;
    }
    case PetState::Done: {
      // Hop: up for 300 ms, squash on landing, rest; repeat.
      uint32_t ph = t % 900;
      if (ph < 320) bob = -(int)(4 * sin(PI * ph / 320.0f));
      else if (ph < 420) squash = 1;
      eyes = Eyes::Happy;
      mouth = Mouth::Open;
      armL = armR = (ph < 320) ? 2 : 1;
      blush = true;
      break;
    }
    case PetState::Error: {
      lookX = ((t / 120) % 4 == 0) ? 1 : ((t / 120) % 4 == 2) ? -1 : 0;   // shake
      eyes = Eyes::Cross;
      mouth = Mouth::Wavy;
      break;
    }
    case PetState::Sleep: {
      bob = (t / 1500) % 2;
      eyes = Eyes::Closed;
      mouth = Mouth::None;
      break;
    }
  }

  if (m.isAsking) {
    eyes = isBlink ? Eyes::Closed : Eyes::Wide;
    lookY = -1;
    mouth = Mouth::Oh;
    armL = armR = 2;
    bob = 0;
  }
  if (m.isPetted) {
    eyes = Eyes::Happy;
    mouth = Mouth::Smile;
    blush = true;
  }

  // Body box (grid coords); squash widens and flattens it.
  int left = 7 - squash, right = 24 + squash;
  int top = 11 + bob + squash * 2, bottom = 26;
  int cx = 16;

  // Body: outline one pixel larger, then the fill, shade and highlight.
  blob(p, left - 1, right + 1, top - 1, bottom, C_OUTLINE);
  blob(p, left, right, top, bottom - 1, body);
  p.rect(left + 1, bottom - 2, right - left - 1, 1, shade);
  p.rect(right - 1, top + 3, 1, bottom - top - 5, shade);
  p.rect(left + 2, top + 2, 2, 1, light);
  p.px(left + 2, top + 3, light);

  // What sits on top of the head: the one thing that tells sessions apart
  // besides colour.
  switch (m.body % 4) {
    case 1: {   // antenna, its tip lit while busy
      bool isLit = (st == PetState::Working || st == PetState::Thinking) && (t / 300) % 2;
      p.rect(cx, top - 4, 1, 4, C_OUTLINE);
      p.rect(cx - 1, top - 7, 3, 3, C_OUTLINE);
      p.px(cx, top - 6, isLit ? C_SPARK : light);
      break;
    }
    case 2: {   // cat ears: outlined triangles, pink inside
      for (int side = 0; side < 2; side++) {
        int ex = side == 0 ? left + 3 : right - 3;   // the ear's centre column
        p.px(ex, top - 5, C_OUTLINE);
        p.rect(ex - 1, top - 4, 3, 1, C_OUTLINE);
        p.rect(ex - 2, top - 3, 5, 3, C_OUTLINE);
        p.px(ex, top - 4, C_BLUSH);
        p.rect(ex - 1, top - 3, 3, 3, body);
        p.rect(ex, top - 3, 1, 2, C_BLUSH);
      }
      break;
    }
    case 3: {   // bunny ears, the right one flopped over
      p.rect(left + 2, top - 8, 4, 8, C_OUTLINE);
      p.rect(left + 3, top - 7, 2, 7, body);
      p.rect(left + 3, top - 6, 1, 5, C_BLUSH);
      p.rect(right - 5, top - 5, 4, 5, C_OUTLINE);
      p.rect(right - 4, top - 4, 2, 4, body);
      p.rect(right - 3, top - 7, 5, 4, C_OUTLINE);
      p.rect(right - 2, top - 6, 3, 2, body);
      p.rect(right - 2, top - 5, 2, 1, C_BLUSH);
      break;
    }
    default:    // little ear nubs
      p.rect(left + 3, top - 3, 3, 2, C_OUTLINE);
      p.rect(right - 5, top - 3, 3, 2, C_OUTLINE);
      p.px(left + 4, top - 2, light);
      p.px(right - 4, top - 2, light);
      break;
  }

  // Feet.
  int footShift = (st == PetState::Working) ? (int)((t / 180) % 2) : 0;
  p.rect(cx - 6, bottom + 1, 4, 1, C_OUTLINE);
  p.rect(cx + 2 + footShift, bottom + 1, 4, 1, C_OUTLINE);

  // Arms: a small outlined paw on each side, at rest, up, or raised high.
  auto paw = [&](int x, int y) {
    p.rect(x, y, 4, 4, C_OUTLINE);
    p.rect(x + 1, y + 1, 2, 2, body);
  };
  auto arm = [&](int side, int pose) {
    int x = side < 0 ? left - 4 : right + 1;
    int y = top + 8;
    switch (pose) {
      case 0: paw(side < 0 ? left - 2 : right - 1, y); break;
      case 1: paw(x, y - 4); break;
      case 2: paw(x + (side < 0 ? 1 : -1), y - 9); break;
      default: break;
    }
  };
  if (armL != 3) arm(-1, armL);
  if (armR != 3) arm(1, armR);

  // Face.
  int ex1 = cx - 5 + lookX, ex2 = cx + 3 + lookX;
  int ey = top + 5 + lookY;
  auto eye = [&](int ex) {
    switch (eyes) {
      case Eyes::Open:
        p.rect(ex, ey, 2, 3, C_EYE); p.px(ex, ey, C_WHITE); break;
      case Eyes::Down:
        p.rect(ex, ey + 1, 2, 2, C_EYE); break;
      case Eyes::Wide:
        p.rect(ex - 1, ey - 1, 3, 4, C_EYE); p.rect(ex - 1, ey - 1, 2, 2, C_WHITE); break;
      case Eyes::Closed:
        p.rect(ex - 1, ey + 2, 3, 1, C_EYE); break;
      case Eyes::Happy:
        p.px(ex - 1, ey + 2, C_EYE); p.rect(ex, ey + 1, 1, 1, C_EYE);
        p.px(ex + 1, ey + 2, C_EYE); break;
      case Eyes::Cross:
        p.px(ex - 1, ey, C_EYE); p.px(ex + 1, ey, C_EYE); p.px(ex, ey + 1, C_EYE);
        p.px(ex - 1, ey + 2, C_EYE); p.px(ex + 1, ey + 2, C_EYE); break;
    }
  };
  eye(ex1);
  eye(ex2);

  int my = ey + 5, mx = cx - 1 + lookX;
  switch (mouth) {
    case Mouth::Smile: p.px(mx - 1, my, C_EYE); p.rect(mx, my + 1, 2, 1, C_EYE); p.px(mx + 2, my, C_EYE); break;
    case Mouth::Flat:  p.rect(mx, my + 1, 2, 1, C_EYE); break;
    case Mouth::Oh:    p.rect(mx, my, 2, 2, C_EYE); break;
    case Mouth::Open:  p.rect(mx - 1, my, 4, 2, C_EYE); p.rect(mx, my + 1, 2, 1, C_TONGUE); break;
    case Mouth::Wavy:  p.px(mx - 1, my + 1, C_EYE); p.px(mx, my, C_EYE); p.px(mx + 1, my + 1, C_EYE); p.px(mx + 2, my, C_EYE); break;
    case Mouth::None:  break;
  }
  if (blush) {
    p.rect(ex1 - 2, ey + 3, 2, 1, C_BLUSH);
    p.rect(ex2 + 2, ey + 3, 2, 1, C_BLUSH);
  }

  // Props, in front of the body.
  if (st == PetState::Working && !m.isAsking) {
    // A laptop seen from behind, lid glowing, arms tapping on it.
    p.rect(cx - 6, bottom - 6, 12, 6, C_METAL_D);
    p.rect(cx - 5, bottom - 5, 10, 4, C_METAL);
    bool glow = (t / 500) % 2;
    p.rect(cx - 1, bottom - 4, 2, 2, glow ? C_ACCENT : C_METAL_D);
    p.rect(cx - 8, bottom, 16, 1, C_METAL_D);
    int up = (t / 120) % 2;
    p.rect(cx - 8, bottom - 3 - up, 2, 2, C_OUTLINE);
    p.rect(cx + 6, bottom - 4 + up, 2, 2, C_OUTLINE);
  }

  if (st == PetState::Thinking && !m.isAsking) {
    int dots = (t / 350) % 4;
    p.px(25, 9, C_WHITE);
    p.rect(26, 6, 2, 2, C_WHITE);
    p.rect(24, 0, 8, 5, C_WHITE);
    for (int i = 0; i < dots && i < 3; i++) p.px(25 + i * 2, 2, C_EYE);
  }

  if (st == PetState::Waiting && !m.isAsking) {
    bool on = (t / 350) % 2;
    p.rect(1, 1, 6, 9, C_WHITE);
    p.rect(3, 2, 2, 5, on ? C_ACCENT : C_EYE);
    p.rect(3, 8, 2, 1, on ? C_ACCENT : C_EYE);
  }

  if (m.isAsking) {
    // Holding up a sign with a question mark.
    p.rect(left + 1, 0, right - left - 1, 8, C_OUTLINE);
    p.rect(left + 2, 1, right - left - 3, 6, C_WHITE);
    uint16_t q = ((t / 400) % 2) ? C_ACCENT : C_EYE;
    p.rect(cx - 2, 2, 4, 1, q); p.px(cx + 1, 3, q); p.px(cx, 4, q); p.px(cx, 6, q);
  }

  if (st == PetState::Done && !m.isAsking) {
    const int8_t spots[4][2] = {{3, 6}, {28, 4}, {2, 18}, {29, 16}};
    for (int i = 0; i < 4; i++)
      if (((t / 220) + i) % 3 == 0) sparkle(p, spots[i][0], spots[i][1], C_SPARK);
  }

  if (st == PetState::Error && !m.isAsking) {
    for (int i = 0; i < 3; i++) {
      float a = t / 260.0f + i * 2.094f;
      int sx = cx + (int)(7 * cos(a)), sy = top - 4 + (int)(2 * sin(a));
      p.px(sx, sy, C_SPARK);
    }
  }

  if (st == PetState::Sleep) {
    for (int i = 0; i < 2; i++) {
      uint32_t ph = (t / 90 + i * 22) % 44;            // rises over ~4 s
      int zy = 10 - (int)(ph / 4);
      if (zy >= 0) zee(p, 24 + (int)(ph / 11), zy, ph < 30 ? C_DIM : C_PANEL);
    }
  }

  if (m.isLead) {
    int cy = max(0, top - 6);
    p.rect(cx - 4, cy + 2, 9, 2, C_SPARK);
    p.px(cx - 4, cy + 1, C_SPARK); p.px(cx, cy, C_SPARK); p.px(cx, cy + 1, C_SPARK);
    p.px(cx + 4, cy + 1, C_SPARK);
    p.px(cx, cy + 2, C_BLUSH);
  }

  if (m.isPetted) {
    heart(p, cx - 2, max(0, top - 9 - (int)((t / 120) % 3)), C_HEART);
  }
}

}  // namespace pet
