#include "ui.h"
#include "board.h"
#include "display.h"
#include "net.h"
#include "pet.h"
#include "touch.h"

namespace {

// The stage: one canvas for the pet, or the lead and its team of subagents.
constexpr int STAGE_Y = 26, STAGE_H = 156;
constexpr int HELP_Y = 158;                    // where an approval panel starts
constexpr int LABEL_Y = 182, LABEL_H = 18;
constexpr uint32_t NARRATION_TURN_MS = 3000;   // team view: each member's line in turn
constexpr int BARS_Y = 202;
// The case's bezel hides the panel's outer rows: keep content off the bottom edge.
constexpr int SAFE_BOTTOM = 228;
constexpr uint32_t SLEEP_AFTER_MS = 120 * 1000;
constexpr int MAX_DOTS = 6;   // more sessions than this: "3/9" instead of dots

Arduino_Canvas* g_stage = nullptr;
Arduino_Canvas* g_labelCanvas = nullptr;

enum class Mode : uint8_t { None, Empty, Session, Asking, Picking };

Mode     g_mode = Mode::None;
char     g_header[64];
char     g_approvalId[24];
int8_t   g_bars[3] = {-2, -2, -2};
int      g_dots[2] = {-1, -1};
uint32_t g_pettedUntilMs = 0;
uint32_t g_labelStartMs = 0;
char     g_label[96];
char     g_patSid[40];
uint16_t g_pats = 0;

Arduino_GFX* G() { return display::gfx(); }

// Name on the left; model over effort on the right; between them a dot per
// live session (filled for the one shown, yellow for one that needs you) and
// a pin when the person chose what is shown.
void drawHeader(const char* name, uint16_t color, const char* model, const char* effort, const View& v) {
  char key[64];
  int n = snprintf(key, sizeof(key), "%.20s|%u|%s|%s|%d|%d|", name, color, model, effort, v.index, v.isPinned);
  for (int i = 0; i < v.count && n < (int)sizeof(key) - 2; i++)
    key[n++] = (v.entries[i].isAsking || v.entries[i].state == PetState::Waiting) ? 'w' : 'o';
  key[n] = '\0';
  if (!strcmp(g_header, key)) return;
  strncpy(g_header, key, sizeof(g_header) - 1);

  G()->fillRect(0, 0, TFT_W, 24, C_PANEL);
  G()->setTextSize(1);
  int right = TFT_W - 8;
  int modelW = display::textWidth(model, 1), effortW = display::textWidth(effort, 1);
  G()->setTextColor(C_TEXT, C_PANEL);
  G()->setCursor(right - modelW, 3);
  G()->print(model);
  G()->setTextColor(C_DIM, C_PANEL);
  G()->setCursor(right - effortW, 13);
  G()->print(effort);

  int x = right - max(modelW, effortW) - 8;
  int dotsLeft;
  if (v.count <= MAX_DOTS) {
    for (int i = v.count - 1; i >= 0; i--, x -= 9) {
      bool needsYou = v.entries[i].isAsking || v.entries[i].state == PetState::Waiting;
      if (i == v.index) G()->fillCircle(x, 12, 3, needsYou ? C_WARN : C_ACCENT);
      else if (needsYou) G()->fillCircle(x, 12, 3, C_WARN);
      else G()->drawCircle(x, 12, 2, C_DIM);
    }
    dotsLeft = v.count > 0 ? x + 6 : right - max(modelW, effortW);
  } else {
    // Too many for dots: "3/9", and one yellow dot when another needs you.
    bool otherNeedsYou = false;
    for (int i = 0; i < v.count; i++)
      if (i != v.index && (v.entries[i].isAsking || v.entries[i].state == PetState::Waiting)) otherNeedsYou = true;
    char pos[8];
    snprintf(pos, sizeof(pos), "%d/%d", v.index + 1, v.count);
    int w = display::textWidth(pos, 1);
    G()->setTextColor(C_TEXT, C_PANEL);
    G()->setCursor(x - w + 3, 9);
    G()->print(pos);
    x -= w + 2;
    if (otherNeedsYou) {
      G()->fillCircle(x - 2, 12, 3, C_WARN);
      x -= 9;
    }
    dotsLeft = x + 2;
  }
  if (v.isPinned && v.count > 0) {
    // A pushpin: head and needle.
    G()->fillCircle(x - 1, 9, 3, C_ACCENT);
    G()->drawLine(x - 1, 12, x - 1, 18, C_ACCENT);
    dotsLeft = x - 5;
  }

  // The name gets the room left of the dots: the big font when it fits, the
  // small one when not, and cut short with ".." only past that.
  int room = dotsLeft - 8 - 6;
  char shown[40];
  strncpy(shown, name, sizeof(shown) - 1);
  shown[sizeof(shown) - 1] = '\0';
  uint8_t size = display::textWidth(shown, 2) <= room ? 2 : 1;
  if (size == 1) {
    int fit = max(3, room / 6);
    if ((int)strlen(shown) > fit) strcpy(shown + fit - 2, "..");
  }
  G()->setTextSize(size);
  G()->setTextColor(color, C_PANEL);
  G()->setCursor(8, size == 2 ? 5 : 9);
  G()->print(shown);
}

uint16_t barColor(int pct) { return pct < 60 ? C_GOOD : pct < 85 ? C_WARN : C_BAD; }

void drawBar(int x, int y, int w, const char* name, int8_t pct) {
  G()->setTextSize(1);
  G()->setTextColor(C_DIM, C_BG);
  G()->setCursor(x, y);
  G()->print(name);
  int bx = x + (int)strlen(name) * 6 + 4, bw = w - (bx - x) - 28;
  G()->fillRect(bx, y, bw, 7, C_PANEL);
  if (pct >= 0) G()->fillRect(bx, y, max(1, bw * pct / 100), 7, barColor(pct));
  char num[6];
  if (pct >= 0) snprintf(num, sizeof(num), "%3d%%", pct);
  else strcpy(num, "  --");
  G()->setTextColor(pct >= 0 ? C_TEXT : C_DIM, C_BG);
  G()->setCursor(x + w - 24, y);
  G()->print(num);
}

void drawBars(const Session& s) {
  if (g_bars[0] == s.ctx && g_bars[1] == s.five && g_bars[2] == s.week) return;
  g_bars[0] = s.ctx; g_bars[1] = s.five; g_bars[2] = s.week;
  G()->fillRect(0, BARS_Y, TFT_W, SAFE_BOTTOM - BARS_Y, C_BG);
  drawBar(12, BARS_Y + 2, 216, "context", s.ctx);
  drawBar(12, BARS_Y + 14, 106, "5h", s.five);
  drawBar(122, BARS_Y + 14, 106, "7d", s.week);
}

void drawLabel(const char* text, uint16_t color, uint32_t now) {
  if (strcmp(g_label, text)) {
    strncpy(g_label, text, sizeof(g_label) - 1);
    g_labelStartMs = now;
  }
  Arduino_Canvas* c = g_labelCanvas;
  c->fillScreen(C_BG);
  c->setTextSize(2);
  c->setTextColor(color);
  int w = display::textWidth(text, 2);
  int x;
  if (w <= TFT_W - 8) {
    x = (TFT_W - w) / 2;
  } else {
    // Hold the start for a second, then scroll at 40 px/s and wrap.
    uint32_t el = now - g_labelStartMs;
    int span = w + 60;
    int off = el < 1000 ? 0 : (int)(((el - 1000) * 40 / 1000) % span);
    x = 4 - off;
    c->setCursor(x + span, 1);
    c->print(text);
  }
  c->setCursor(x, 1);
  c->print(text);
  c->flush();
}

// Sends the stage to the panel; an approval keeps the rows its panel covers.
void flushStage(bool isAsking) {
  int rows = isAsking ? HELP_Y - STAGE_Y : STAGE_H;
  G()->draw16bitRGBBitmap(0, STAGE_Y, g_stage->getFramebuffer(), TFT_W, rows);
}

void drawSolo(const pet::Mood& m, uint32_t now, bool isAsking) {
  g_stage->fillScreen(C_BG);
  pet::draw(g_stage, (TFT_W - 128) / 2, 2, 4, m, now);
  flushStage(isAsking);
}

// The lead on the left, a crown on its head; up to four subagents in a 2x2
// grid on the right, each with its name underneath.
void drawTeam(const Session& s, pet::Mood lead, uint32_t now) {
  Arduino_Canvas* c = g_stage;
  c->fillScreen(C_BG);
  lead.isLead = true;
  pet::draw(c, 2, 30, 3, lead, now);

  int n = s.helperCount;
  int shown = n > 4 ? 3 : n;
  c->setTextSize(1);
  for (int i = 0; i < shown; i++) {
    const Helper& h = s.helpers[i];
    int x0 = 100 + (i % 2) * 70, y0 = (i / 2) * 78;
    pet::Mood m{h.state, h.color, false, false, false, false, h.body, (uint8_t)(i + 1)};
    pet::draw(c, x0 + 3, y0, 2, m, now);
    char name[12];
    strncpy(name, h.name, 11);
    name[11] = '\0';
    c->setTextColor(h.state == PetState::Done ? C_DIM : pet::bodyColor(h.color));
    c->setCursor(x0 + (70 - display::textWidth(name, 1)) / 2, y0 + 66);
    c->print(name);
  }
  if (n > 4) {
    char more[8];
    snprintf(more, sizeof(more), "+%d", n - 3);
    c->setTextSize(2);
    c->setTextColor(C_DIM);
    c->setCursor(170 + (70 - display::textWidth(more, 2)) / 2, 78 + 26);
    c->print(more);
  }
  flushStage(false);
}

// Team view narration: the lead's line and each busy helper's, in turn.
void teamLine(const Session& s, uint32_t now, char* out, size_t cap) {
  const Helper* busy[MAX_HELPERS];
  int nb = 0;
  for (int i = 0; i < s.helperCount; i++)
    if (s.helpers[i].state != PetState::Done && s.helpers[i].label[0]) busy[nb++] = &s.helpers[i];
  int turn = (int)((now / NARRATION_TURN_MS) % (nb + 1));
  if (turn == 0) snprintf(out, cap, "%s", s.label[0] ? s.label : stateName(s.state));
  else snprintf(out, cap, "%s: %s", busy[turn - 1]->name, busy[turn - 1]->label);
}

// Word-wraps `text` into lines of at most `cols` characters.
int wrap(const char* text, int cols, char out[][40], int maxLines) {
  int n = 0, len = strlen(text), i = 0;
  while (i < len && n < maxLines) {
    int end = min(len, i + cols);
    if (end < len) {
      int sp = end;
      while (sp > i && text[sp] != ' ') sp--;
      if (sp > i) end = sp;
    }
    int l = min(end - i, 39);
    memcpy(out[n], text + i, l);
    out[n][l] = '\0';
    n++;
    i = end;
    while (i < len && text[i] == ' ') i++;
  }
  if (i < len && n > 0) {
    int l = strlen(out[n - 1]);
    if (l > 3) strcpy(out[n - 1] + l - 3, "...");
  }
  return n;
}

void drawApprovalPanel(const Approval& a) {
  G()->fillRect(0, HELP_Y, TFT_W, TFT_H - HELP_Y, C_BG);
  G()->fillRoundRect(6, HELP_Y + 2, TFT_W - 12, SAFE_BOTTOM - HELP_Y - 2, 8, C_PANEL);
  G()->setTextSize(2);
  G()->setTextColor(C_WARN, C_PANEL);
  G()->setCursor(12, HELP_Y + 10);
  char tool[17];
  strncpy(tool, a.tool, 16);
  tool[16] = '\0';
  G()->print(tool);
  char lines[3][40];
  int n = wrap(a.summary, 36, lines, 3);
  G()->setTextSize(1);
  G()->setTextColor(C_TEXT, C_PANEL);
  for (int i = 0; i < n; i++) {
    G()->setCursor(12, HELP_Y + 26 + i * 10);
    G()->print(lines[i]);
  }
}

void drawApprovalFooter(const Approval& a, uint32_t now) {
  int y = SAFE_BOTTOM - 14;
  float hold = touch::holdProgress();
  G()->fillRect(10, y - 2, TFT_W - 20, 12, C_PANEL);
  if (hold > 0) {
    G()->fillRect(10, y - 2, (int)((TFT_W - 20) * hold), 12, C_BAD);
    G()->setTextColor(C_TEXT);
  } else {
    G()->setTextColor(C_DIM);
  }
  G()->setTextSize(1);
  G()->setCursor(14, y);
  G()->print("TAP = allow");
  G()->setCursor(TFT_W - 14 - 12 * 6, y);
  G()->print("HOLD = deny");
  // Time left, as a shrinking line under the header of the panel.
  uint32_t el = now - a.startedMs;
  float left = el >= a.timeoutMs ? 0 : 1.0f - (float)el / a.timeoutMs;
  int w = (int)((TFT_W - 24) * left);
  G()->fillRect(12, HELP_Y + 6, TFT_W - 24, 2, C_PANEL);
  G()->fillRect(12, HELP_Y + 6, w, 2, C_WARN);
}

// ---- session picker -------------------------------------------------------
// Row 0 is "Auto" (follow activity); rows 1.. are the sessions in the order
// they started.
constexpr uint32_t PICK_AFTER_MS = 2000;
constexpr int PICK_Y = 30, PICK_ROW = 26, PICK_ROWS = 7;   // rows on screen at once

struct Picker {
  bool     isOpen;
  int      highlight;
  uint32_t lastInputMs;
  char     drawnKey[96];
};
Picker g_picker;

const char* shortState(PetState s, bool isAsking) {
  if (isAsking) return "asking";
  return stateName(s);
}

void drawPickerRows(const View& v) {
  // A window of PICK_ROWS rows that keeps the highlight in view.
  int total = v.count + 1;
  int first = constrain(g_picker.highlight - PICK_ROWS / 2, 0, max(0, total - PICK_ROWS));
  int last = min(total, first + PICK_ROWS);
  char key[96];
  int n = snprintf(key, sizeof(key), "%d|%d|%d|", g_picker.highlight, v.count, first);
  for (int i = 0; i < v.count && n < (int)sizeof(key) - 4; i++)
    n += snprintf(key + n, sizeof(key) - n, "%c%c", 'a' + (int)v.entries[i].state, v.entries[i].isAsking ? '!' : '.');
  if (!strcmp(g_picker.drawnKey, key)) return;
  strncpy(g_picker.drawnKey, key, sizeof(g_picker.drawnKey) - 1);

  G()->fillRect(0, PICK_Y - 4, TFT_W, PICK_ROWS * PICK_ROW + 4, C_BG);
  // Small arrows at the right edge when rows are out of view.
  if (first > 0) G()->fillTriangle(TFT_W - 6, PICK_Y - 3, TFT_W - 10, PICK_Y + 1, TFT_W - 2, PICK_Y + 1, C_DIM);
  if (last < total) {
    int yb = PICK_Y + PICK_ROWS * PICK_ROW - 2;
    G()->fillTriangle(TFT_W - 6, yb + 1, TFT_W - 10, yb - 3, TFT_W - 2, yb - 3, C_DIM);
  }
  for (int row = first; row < last; row++) {
    int y = PICK_Y + (row - first) * PICK_ROW;
    bool isOn = row == g_picker.highlight;
    uint16_t bg = isOn ? C_PANEL : C_BG;
    if (isOn) {
      G()->fillRoundRect(4, y, TFT_W - 8, PICK_ROW - 2, 6, C_PANEL);
      G()->fillRect(4, y + 4, 3, PICK_ROW - 10, C_ACCENT);
    }
    const char* label;
    const char* state = "";
    uint16_t stateColor = C_DIM;
    if (row == 0) {
      G()->drawCircle(18, y + 12, 5, C_DIM);
      G()->drawCircle(18, y + 12, 2, C_DIM);
      label = "Auto";
      state = "follow";
    } else {
      const Entry& e = v.entries[row - 1];
      G()->fillCircle(18, y + 12, 5, pet::bodyColor(e.color));
      label = e.name;
      state = shortState(e.state, e.isAsking);
      if (e.isAsking || e.state == PetState::Waiting) stateColor = C_WARN;
      else if (e.state == PetState::Working || e.state == PetState::Thinking) stateColor = C_GOOD;
    }
    char name[12];
    strncpy(name, label, 11);
    name[11] = '\0';
    G()->setTextSize(2);
    G()->setTextColor(isOn ? C_TEXT : C_DIM, bg);
    G()->setCursor(30, y + 5);
    G()->print(name);
    G()->setTextSize(1);
    G()->setTextColor(stateColor, bg);
    G()->setCursor(TFT_W - 16 - display::textWidth(state, 1), y + 9);
    G()->print(state);
  }
}

// The countdown to the automatic pick, as a shrinking bar under the list.
void drawPickerCountdown(uint32_t now) {
  uint32_t el = now - g_picker.lastInputMs;
  float left = el >= PICK_AFTER_MS ? 0 : 1.0f - (float)el / PICK_AFTER_MS;
  int y = SAFE_BOTTOM - 8;
  G()->fillRect(12, y, TFT_W - 24, 3, C_PANEL);
  G()->fillRect(12, y, (int)((TFT_W - 24) * left), 3, C_ACCENT);
}

void setMode(Mode m) {
  if (g_mode == m) return;
  g_mode = m;
  G()->fillScreen(C_BG);
  g_header[0] = '\0';
  g_dots[0] = g_dots[1] = -1;
  g_bars[0] = g_bars[1] = g_bars[2] = -2;
  g_approvalId[0] = '\0';
  g_label[0] = '\0';
}

}  // namespace

namespace ui {

bool begin() {
  g_stage = new Arduino_Canvas(TFT_W, STAGE_H, G(), 0, STAGE_Y);
  g_labelCanvas = new Arduino_Canvas(TFT_W, LABEL_H, G(), 0, LABEL_Y);
  return g_stage->begin(GFX_SKIP_OUTPUT_BEGIN) && g_labelCanvas->begin(GFX_SKIP_OUTPUT_BEGIN);
}

void pet() { g_pettedUntilMs = millis() + 1600; }

void invalidate() { g_mode = Mode::None; }

bool isPickerOpen() { return g_picker.isOpen; }

void openPicker(const View& v) {
  g_picker.isOpen = true;
  g_picker.highlight = v.isPinned ? v.index + 1 : 0;
  g_picker.lastInputMs = millis();
  g_picker.drawnKey[0] = '\0';
}

void pickerNext(const View& v) {
  g_picker.highlight = (g_picker.highlight + 1) % (v.count + 1);
  g_picker.lastInputMs = millis();
}

bool isPickDue() { return g_picker.isOpen && millis() - g_picker.lastInputMs >= PICK_AFTER_MS; }

String pickerChoice(const View& v) {
  g_picker.isOpen = false;
  int row = min(g_picker.highlight, v.count);
  return row == 0 ? String("") : String(v.entries[row - 1].sid);
}

void closePicker() { g_picker.isOpen = false; }

void render(const View& v, uint32_t now) {
  if (!v.hasSession) {
    setMode(Mode::Empty);
    drawHeader("claude pet", C_DIM, "", "", v);
    bool isPetted = (int32_t)(g_pettedUntilMs - now) > 0;
    drawSolo(pet::Mood{PetState::Sleep, 0, false, isPetted, true, false, 0, 0}, now, false);
    String where = net::isConnected() ? String(HOSTNAME ".local  ") + net::ip()
                                      : String("Wi-Fi: join ") + AP_SSID;
    drawLabel(where.c_str(), C_DIM, now);
    return;
  }

  // An approval closes the picker: it is the one thing that cannot wait.
  if (g_picker.isOpen && v.hasApproval) g_picker.isOpen = false;
  if (g_picker.isOpen) {
    setMode(Mode::Picking);
    drawHeader("show", C_ACCENT, "", "", v);
    drawPickerRows(v);
    drawPickerCountdown(now);
    return;
  }

  const Session& s = v.session;

  // A please or thanks in the prompt pets the pet.
  if (strcmp(g_patSid, s.sid)) {
    strncpy(g_patSid, s.sid, sizeof(g_patSid) - 1);
    g_pats = s.pats;
  } else if (s.pats != g_pats) {
    g_pats = s.pats;
    pet();
  }
  bool isPetted = (int32_t)(g_pettedUntilMs - now) > 0;

  bool isAsking = v.hasApproval && !strcmp(v.approval.sid, s.sid);
  setMode(isAsking ? Mode::Asking : Mode::Session);

  drawHeader(s.name[0] ? s.name : "claude", isAsking ? C_WARN : C_TEXT, s.model, s.effort, v);

  PetState st = s.state;
  if (st == PetState::Idle && now - s.stateSinceMs > SLEEP_AFTER_MS) st = PetState::Sleep;
  pet::Mood m{st, s.color, isAsking, isPetted, false, false, s.body, 0};

  if (isAsking) {
    drawSolo(m, now, true);
    if (strcmp(g_approvalId, v.approval.id)) {
      strncpy(g_approvalId, v.approval.id, sizeof(g_approvalId) - 1);
      drawApprovalPanel(v.approval);
    }
    drawApprovalFooter(v.approval, now);
    return;
  }

  char line[96];
  if (s.helperCount > 0) {
    drawTeam(s, m, now);
    teamLine(s, now, line, sizeof(line));
  } else {
    drawSolo(m, now, false);
    snprintf(line, sizeof(line), "%s", s.label[0] ? s.label : stateName(st));
  }
  uint16_t color = st == PetState::Waiting ? C_WARN : st == PetState::Error ? C_BAD
                 : st == PetState::Done ? C_GOOD : C_TEXT;
  drawLabel(line, color, now);
  drawBars(s);
}

}  // namespace ui
