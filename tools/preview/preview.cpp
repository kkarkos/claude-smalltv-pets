// Renders every pet mood at a few moments into one PPM contact sheet.
// Build + run: ./preview.sh  (writes pets.png)
#include <cstdio>
#include "pet.h"
#include "display.h"

int main() {
  const int CELL = 4, TILE = 32 * CELL + 8, COLS = 6;
  struct Row { const char* name; PetState st; bool asking, petted, empty, lead; };
  Row rows[] = {
    {"idle", PetState::Idle, false, false, false, false},
    {"thinking", PetState::Thinking, false, false, false, false},
    {"working", PetState::Working, false, false, false, false},
    {"waiting", PetState::Waiting, false, false, false, false},
    {"approval", PetState::Working, true, false, false, false},
    {"done", PetState::Done, false, false, false, false},
    {"error", PetState::Error, false, false, false, false},
    {"sleep", PetState::Sleep, false, false, false, false},
    {"petted", PetState::Idle, false, true, false, false},
    {"empty", PetState::Sleep, false, false, true, false},
    {"lead", PetState::Working, false, false, false, true},
  };
  const int R = sizeof(rows) / sizeof(rows[0]);
  const uint32_t times[COLS] = {0, 130, 400, 1000, 2700, 3650};
  Arduino_GFX g(COLS * TILE + 250, R * TILE);
  g.fillRect(0, 0, g.w, g.h, C_BG);
  for (int r = 0; r < R; r++)
    for (int c = 0; c < COLS; c++) {
      pet::Mood m{rows[r].st, (uint8_t)(r % 8), rows[r].asking, rows[r].petted, rows[r].empty, rows[r].lead, (uint8_t)(r % 4), 0};
      pet::draw(&g, c * TILE + 4, r * TILE + 4, CELL, m, times[c] + 10000);
    }
  // Team view mock, as the firmware lays it out (240x156 stage).
  int tx = COLS * TILE + 4, ty = 4;
  g.fillRect(tx - 2, ty - 2, 244, 160, rgb(40, 36, 52));
  g.fillRect(tx, ty, 240, 156, C_BG);
  pet::Mood lead{PetState::Thinking, 0, false, false, false, true, 0, 0};
  pet::draw(&g, tx + 2, ty + 30, 3, lead, 10000);
  PetState hs[4] = {PetState::Working, PetState::Working, PetState::Waiting, PetState::Done};
  for (int i = 0; i < 4; i++) {
    pet::Mood m{hs[i], (uint8_t)(i + 1), false, false, false, false, (uint8_t)(i % 4), (uint8_t)(i + 1)};
    pet::draw(&g, tx + 100 + (i % 2) * 70 + 3, ty + (i / 2) * 78, 2, m, 10000);
  }
  FILE* f = fopen("pets.ppm", "wb");
  fprintf(f, "P6 %d %d 255\n", g.w, g.h);
  for (int i = 0; i < g.w * g.h; i++) {
    uint16_t p = g.fb[i];
    unsigned char px[3] = {(unsigned char)((p >> 11) << 3), (unsigned char)(((p >> 5) & 63) << 2), (unsigned char)((p & 31) << 3)};
    fwrite(px, 1, 3, f);
  }
  fclose(f);
  for (int r = 0; r < R; r++) printf("row %d: %s\n", r, rows[r].name);
}
