// pet.h: the pixel pet, drawn procedurally on a 32x32 grid so every mood is
// a few parameters (bob, squash, eyes, mouth, arms, props) rather than frames.
#pragma once
#include <Arduino_GFX_Library.h>
#include "model.h"

namespace pet {

struct Mood {
  PetState state;
  uint8_t  color;          // palette index 0..7
  bool     isAsking;       // an approval is pending for this pet
  bool     isPetted;       // the button was just tapped
  bool     isEmpty;        // no session: a grey, sleeping pet
  bool     isLead;         // wears a crown: the lead of an agent team
  uint8_t  body;           // 0 ear nubs, 1 antenna, 2 cat ears, 3 bunny ears
  uint8_t  phase;          // offsets the animation clock so a team is not in lockstep
};

constexpr int GRID = 32;

// Draws the pet with its grid's top-left at (ox, oy), each grid pixel `cell`
// screen pixels wide. `t` is millis().
void draw(Arduino_GFX* c, int ox, int oy, int cell, const Mood& m, uint32_t t);

uint16_t bodyColor(uint8_t idx);

}  // namespace pet
