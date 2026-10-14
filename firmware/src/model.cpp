#include "model.h"
#include "board.h"
#include <algorithm>
#include <freertos/semphr.h>

namespace {

SemaphoreHandle_t g_lock = nullptr;

struct Lock {
  Lock()  { xSemaphoreTakeRecursive(g_lock, portMAX_DELAY); }
  ~Lock() { xSemaphoreGiveRecursive(g_lock); }
};

Session  g_sessions[MAX_SESSIONS];
Approval g_approval;

// The last answered approval, so a poll that arrives just after the press
// still reads the answer.
char     g_answeredId[24];
Decision g_answered = Decision::None;

char     g_pinnedSid[40];   // "" while following activity
uint32_t g_lastActivityMs = 0;

// A session whose terminal was closed without a goodbye: gone after three
// missed heartbeats (the plugin sends one every 60 s).
constexpr uint32_t SESSION_TTL_MS = 3 * 60 * 1000;
constexpr uint32_t DONE_DECAY_MS  = 45 * 1000;

void copyStr(char* dst, size_t cap, const char* src) {
  if (!src) src = "";
  strncpy(dst, src, cap - 1);
  dst[cap - 1] = '\0';
}

PetState parseState(const char* s) {
  if (!s) return PetState::Idle;
  if (!strcmp(s, "thinking")) return PetState::Thinking;
  if (!strcmp(s, "working"))  return PetState::Working;
  if (!strcmp(s, "waiting"))  return PetState::Waiting;
  if (!strcmp(s, "done"))     return PetState::Done;
  if (!strcmp(s, "error"))    return PetState::Error;
  if (!strcmp(s, "sleep"))    return PetState::Sleep;
  return PetState::Idle;
}

int8_t percentOrUnknown(JsonVariantConst v) {
  if (v.isNull()) return -1;
  int p = v.as<int>();
  return (int8_t)constrain(p, 0, 100);
}

Session* find(const char* sid) {
  for (auto& s : g_sessions)
    if (s.isUsed && !strcmp(s.sid, sid)) return &s;
  return nullptr;
}

Session* findOrAdd(const char* sid) {
  if (Session* s = find(sid)) return s;
  Session* slot = nullptr;
  for (auto& s : g_sessions)
    if (!s.isUsed) { slot = &s; break; }
  if (!slot) {  // full: evict the stalest
    slot = &g_sessions[0];
    for (auto& s : g_sessions)
      if (s.updatedMs < slot->updatedMs) slot = &s;
  }
  memset(slot, 0, sizeof(*slot));
  slot->isUsed = true;
  copyStr(slot->sid, sizeof(slot->sid), sid);
  slot->ctx = slot->five = slot->week = -1;
  slot->createdMs = slot->stateSinceMs = slot->activityMs = millis();
  // A colour and a body no other live session has, so two sessions never
  // look alike (the plugin can still ask for a colour of its own).
  bool isColorUsed[8] = {}, isBodyUsed[BODY_COUNT] = {};
  int live = 0;
  for (auto& o : g_sessions) {
    if (!o.isUsed || &o == slot) continue;
    isColorUsed[o.color & 7] = true;
    isBodyUsed[o.body % BODY_COUNT] = true;
    live++;
  }
  slot->color = live % 8;
  for (int c = 0; c < 8; c++) if (!isColorUsed[c]) { slot->color = c; break; }
  slot->body = live % BODY_COUNT;
  for (int b = 0; b < BODY_COUNT; b++) if (!isBodyUsed[b]) { slot->body = b; break; }
  return slot;
}

int urgency(const Session& s) {
  if (g_approval.isActive && !strcmp(g_approval.sid, s.sid)) return 6;
  switch (s.state) {
    case PetState::Waiting:  return 5;
    case PetState::Error:    return 4;
    case PetState::Working:
    case PetState::Thinking: return 3;
    case PetState::Done:     return 2;
    default:                 return 1;
  }
}

// A second session in the same folder shows as "palesa 2", a third as
// "palesa 3": the lowest number no live session of that base holds.
void setBase(Session* s, const char* base) {
  // Same base again: keep its number unless a lower one has come free (the
  // first session of that folder ended), so "palesa 2" becomes "palesa".
  if (!strcmp(s->base, base) && s->dup == 1) return;
  copyStr(s->base, sizeof(s->base), base);
  uint8_t dup = 1;
  for (bool isTaken = true; isTaken; ) {
    isTaken = false;
    for (auto& o : g_sessions)
      if (o.isUsed && &o != s && o.dup == dup && !strcmp(o.base, base)) { isTaken = true; dup++; break; }
  }
  s->dup = dup;
  if (dup == 1) copyStr(s->name, sizeof(s->name), base);
  else snprintf(s->name, sizeof(s->name), "%.20s %u", base, dup);
}

// The session to show: one needing an approval always; else the one the
// person picked; else the most urgent, the freshest among equals.
Session* focused() {
  Session* best = nullptr;
  for (auto& s : g_sessions) {
    if (!s.isUsed) continue;
    if (!best || urgency(s) > urgency(*best) ||
        (urgency(s) == urgency(*best) && s.activityMs > best->activityMs))
      best = &s;
  }
  if (g_pinnedSid[0]) {
    Session* pinned = find(g_pinnedSid);
    if (pinned && !(best && urgency(*best) == 6 && best != pinned)) return pinned;
  }
  return best;
}

}  // namespace

const char* stateName(PetState s) {
  switch (s) {
    case PetState::Thinking: return "thinking";
    case PetState::Working:  return "working";
    case PetState::Waiting:  return "waiting";
    case PetState::Done:     return "done";
    case PetState::Error:    return "error";
    case PetState::Sleep:    return "sleep";
    default:                 return "idle";
  }
}

const char* decisionName(Decision d) {
  switch (d) {
    case Decision::Pending: return "pending";
    case Decision::Allow:   return "allow";
    case Decision::Deny:    return "deny";
    default:                return "expired";
  }
}

namespace model {

void begin() {
  g_lock = xSemaphoreCreateRecursiveMutex();
  memset(g_sessions, 0, sizeof(g_sessions));
  memset(&g_approval, 0, sizeof(g_approval));
  g_lastActivityMs = millis();
}

bool upsertSession(JsonVariantConst b, String& err) {
  const char* sid = b["sid"];
  if (!sid || !sid[0]) { err = "sid is required"; return false; }
  Lock l;
  Session* s = findOrAdd(sid);
  if (b["name"].is<const char*>())  setBase(s, b["name"]);
  uint32_t now = millis();
  bool isActive = false;   // a heartbeat repeats what the pet knows; only change counts
  if (b["label"].is<const char*>() && strncmp(s->label, b["label"], sizeof(s->label) - 1)) {
    copyStr(s->label, sizeof(s->label), b["label"]);
    isActive = true;
  }
  if (b["color"].is<int>()) s->color = (uint8_t)(b["color"].as<int>() & 7);
  if (b["body"].is<int>())  s->body = (uint8_t)(b["body"].as<int>() % BODY_COUNT);
  if (b["state"].is<const char*>()) {
    PetState next = parseState(b["state"]);
    if (next != s->state) { s->state = next; s->stateSinceMs = now; isActive = true; }
  }
  if (!b["ctx"].isUnbound())  s->ctx  = percentOrUnknown(b["ctx"]);
  if (!b["five"].isUnbound()) s->five = percentOrUnknown(b["five"]);
  if (!b["week"].isUnbound()) s->week = percentOrUnknown(b["week"]);
  if (b["model"].is<const char*>())  copyStr(s->model, sizeof(s->model), b["model"]);
  if (b["effort"].is<const char*>()) copyStr(s->effort, sizeof(s->effort), b["effort"]);
  if (b["pats"].is<int>())           s->pats = (uint16_t)b["pats"].as<int>();
  if (b["helpers"].is<JsonArrayConst>()) {
    s->helperCount = 0;
    for (JsonObjectConst h : b["helpers"].as<JsonArrayConst>()) {
      if (s->helperCount >= MAX_HELPERS) break;
      Helper& dst = s->helpers[s->helperCount++];
      copyStr(dst.id, sizeof(dst.id), h["id"] | "");
      copyStr(dst.name, sizeof(dst.name), h["name"] | "agent");
      copyStr(dst.label, sizeof(dst.label), h["label"] | "");
      dst.color = (uint8_t)((h["color"] | 0) & 7);
      dst.body = (uint8_t)((h["body"] | 0) % BODY_COUNT);
      dst.state = (h["done"] | false) ? PetState::Done : parseState(h["state"] | "working");
    }
  }
  s->updatedMs = now;
  if (isActive) {
    s->activityMs = now;
    g_lastActivityMs = now;   // keeps the screen awake; heartbeats do not
  }
  return true;
}

void endSession(const char* sid) {
  Lock l;
  if (Session* s = find(sid)) s->isUsed = false;
  if (g_approval.isActive && !strcmp(g_approval.sid, sid)) g_approval.isActive = false;
}

bool openApproval(JsonVariantConst b, String& err) {
  const char* id  = b["id"];
  const char* sid = b["sid"];
  if (!id || !id[0] || !sid) { err = "id and sid are required"; return false; }
  Lock l;
  memset(&g_approval, 0, sizeof(g_approval));
  g_approval.isActive = true;
  copyStr(g_approval.id, sizeof(g_approval.id), id);
  copyStr(g_approval.sid, sizeof(g_approval.sid), sid);
  copyStr(g_approval.tool, sizeof(g_approval.tool), b["tool"] | "Tool");
  copyStr(g_approval.summary, sizeof(g_approval.summary), b["summary"] | "");
  g_approval.startedMs = millis();
  g_approval.timeoutMs = constrain((int)(b["timeout"] | 20), 3, 600) * 1000UL;
  g_lastActivityMs = g_approval.startedMs;
  return true;
}

void cancelApproval(const char* id) {
  Lock l;
  if (g_approval.isActive && !strcmp(g_approval.id, id)) g_approval.isActive = false;
}

Decision decisionFor(const char* id) {
  Lock l;
  if (g_approval.isActive && !strcmp(g_approval.id, id)) return Decision::Pending;
  if (g_answered != Decision::None && !strcmp(g_answeredId, id)) return g_answered;
  return Decision::Expired;
}

bool answerApproval(bool isAllowed) {
  Lock l;
  if (!g_approval.isActive) return false;
  copyStr(g_answeredId, sizeof(g_answeredId), g_approval.id);
  g_answered = isAllowed ? Decision::Allow : Decision::Deny;
  g_approval.isActive = false;
  g_lastActivityMs = millis();
  return true;
}

void pin(const char* sid) {
  Lock l;
  copyStr(g_pinnedSid, sizeof(g_pinnedSid), find(sid) ? sid : "");
}

void tick() {
  Lock l;
  uint32_t now = millis();
  for (auto& s : g_sessions) {
    if (!s.isUsed) continue;
    if (now - s.updatedMs > SESSION_TTL_MS) { s.isUsed = false; continue; }
    if (s.state == PetState::Done && now - s.stateSinceMs > DONE_DECAY_MS) {
      s.state = PetState::Idle;
      s.stateSinceMs = now;
    }
  }
  if (g_pinnedSid[0] && !find(g_pinnedSid)) g_pinnedSid[0] = '\0';
  if (g_approval.isActive && now - g_approval.startedMs > g_approval.timeoutMs) {
    copyStr(g_answeredId, sizeof(g_answeredId), g_approval.id);
    g_answered = Decision::Expired;
    g_approval.isActive = false;
  }
}

View view() {
  Lock l;
  View v{};
  Session* order[MAX_SESSIONS];
  int n = 0;
  for (auto& s : g_sessions)
    if (s.isUsed) order[n++] = &s;
  std::sort(order, order + n, [](Session* a, Session* b) { return a->createdMs < b->createdMs; });

  Session* f = focused();
  for (int i = 0; i < n; i++) {
    Entry& e = v.entries[i];
    copyStr(e.sid, sizeof(e.sid), order[i]->sid);
    copyStr(e.name, sizeof(e.name), order[i]->name[0] ? order[i]->name : "claude");
    e.color = order[i]->color;
    e.body = order[i]->body;
    e.state = order[i]->state;
    e.isAsking = g_approval.isActive && !strcmp(g_approval.sid, order[i]->sid);
    if (order[i] == f) v.index = i;
  }
  v.count = n;
  if (f) {
    v.hasSession = true;
    v.session = *f;
    v.isPinned = g_pinnedSid[0] && !strcmp(g_pinnedSid, f->sid);
  }
  if (g_approval.isActive) { v.hasApproval = true; v.approval = g_approval; }
  return v;
}

String infoJson() {
  Lock l;
  JsonDocument d;
  d["fw"] = FW_VERSION;
  d["uptime"] = millis() / 1000;
  d["heap"] = ESP.getFreeHeap();
  JsonArray arr = d["sessions"].to<JsonArray>();
  for (auto& s : g_sessions) {
    if (!s.isUsed) continue;
    JsonObject o = arr.add<JsonObject>();
    o["sid"] = s.sid;
    o["name"] = s.name;
    o["state"] = stateName(s.state);
    o["label"] = s.label;
    o["model"] = s.model;
    o["effort"] = s.effort;
    JsonArray hs = o["helpers"].to<JsonArray>();
    for (int i = 0; i < s.helperCount; i++) {
      JsonObject h = hs.add<JsonObject>();
      h["name"] = s.helpers[i].name;
      h["state"] = stateName(s.helpers[i].state);
      h["label"] = s.helpers[i].label;
    }
  }
  d["approval"] = g_approval.isActive ? g_approval.id : nullptr;
  String out;
  serializeJson(d, out);
  return out;
}

uint32_t lastActivityMs() { Lock l; return g_lastActivityMs; }
void touchActivity()      { Lock l; g_lastActivityMs = millis(); }

}  // namespace model
