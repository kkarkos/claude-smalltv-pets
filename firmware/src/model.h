// model.h: what the device knows about Claude Code sessions and the pending
// approval. Written by the web handlers (async_tcp task), read by the render
// loop (Arduino loop task); every access goes through the lock.
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

enum class PetState : uint8_t { Idle, Thinking, Working, Waiting, Done, Error, Sleep };

constexpr int MAX_SESSIONS = 6;
constexpr int MAX_HELPERS  = 5;
constexpr int BODY_COUNT   = 4;

// A subagent: one more pet in the team view.
struct Helper {
  char     id[24];
  char     name[12];      // short agent type, e.g. "Explore"
  char     label[48];     // what it is doing now
  uint8_t  color;
  uint8_t  body;          // body variant 0..3
  PetState state;         // Working, Waiting or Done
};

struct Session {
  bool     isUsed;
  char     sid[40];
  char     base[24];            // the name the plugin sent (the project folder)
  char     name[24];            // as shown: base, or "base 2" for a second session of it
  uint8_t  dup;                 // 1 for the first session of a base, 2, 3... after
  char     label[96];
  uint8_t  color;               // palette index; the device picks a free one unless told
  uint8_t  body;                // body variant 0..3, picked the same way
  PetState state;
  char     model[16];           // e.g. "opus 5.5"
  char     effort[8];           // e.g. "high"
  int8_t   ctx, five, week;     // percent, -1 when unknown
  uint16_t pats;                // bumps when the prompt said please/thanks
  Helper   helpers[MAX_HELPERS];
  uint8_t  helperCount;
  uint32_t createdMs;           // orders the session list
  uint32_t updatedMs;           // last message from the plugin (heartbeats too)
  uint32_t activityMs;          // last real change: a new state or a new line
  uint32_t stateSinceMs;        // when `state` last changed
};

enum class Decision : uint8_t { None, Pending, Allow, Deny, Expired };

struct Approval {
  bool     isActive;
  char     id[24];
  char     sid[40];
  char     tool[32];
  char     summary[200];
  uint32_t startedMs;
  uint32_t timeoutMs;
};

// One row of the session list: the header's dots and the picker.
struct Entry {
  char     sid[40];
  char     name[24];
  uint8_t  color;
  uint8_t  body;
  PetState state;
  bool     isAsking;   // has the pending approval
};

// The render loop reads one focused session at a time, by value, and the
// list of all of them in the order they started.
struct View {
  bool     hasSession;
  Session  session;
  int      count;      // live sessions
  int      index;      // focused one's position among them
  bool     isPinned;   // the person chose this session; else it follows activity
  Entry    entries[MAX_SESSIONS];
  bool     hasApproval;
  Approval approval;
};

namespace model {
  void begin();

  // From the plugin. Each returns false with `err` set on a bad body.
  bool upsertSession(JsonVariantConst body, String& err);
  void endSession(const char* sid);
  bool openApproval(JsonVariantConst body, String& err);
  void cancelApproval(const char* id);
  Decision decisionFor(const char* id);

  // From the button.
  bool answerApproval(bool isAllowed);   // false when none is pending
  // Shows `sid` until changed; "" goes back to following activity.
  void pin(const char* sid);

  // Housekeeping: expire stale sessions, decay Done, time out approvals.
  void tick();

  View view();
  String infoJson();
  uint32_t lastActivityMs();
  void touchActivity();
}

const char* stateName(PetState s);
const char* decisionName(Decision d);
