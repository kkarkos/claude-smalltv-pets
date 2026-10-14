# Changelog

## 0.1.2 (2026-10-14)

- Fix: with more than six Claude Code sessions open, sessions kept pushing each other off the pet, so it seemed to lag or jump. The pet now holds 12, and when full it drops the session that has been quiet longest.
- The header shows "3/8" instead of dots above six sessions, with a yellow dot when another session needs you; the session picker scrolls.
- Wi-Fi power saving is off, which cuts the pet's response delays (it runs on USB power).

## 0.1.1 (2026-10-14)

- Fix: text with characters the pixel font cannot draw ("Thinking…", curly quotes, umlauts in project names) showed as garbage. The pet now turns it into plain ASCII: "..." for "…", "ae" for "ä", "?" for scripts it has no letters for.
- README: a demo video of the pet on a real device.

## 0.1.0 (2026-10-14)

First release.

- Firmware for the GeekMagic SmallTV Pro, flashable over the stock firmware's `/update` page and updated over Wi-Fi from then on; setup hotspot for Wi-Fi; safe mode after three crashes in a row.
- A procedurally drawn pixel pet with moods for idle, thinking, working, waiting, approval, done, error and sleep; four body types and eight colours, handed out so sessions never look alike.
- One pet per Claude Code session; a session picker on the device (double tap); Auto follows whichever session needs you.
- Agent teams: subagents appear as named helper pets beside a crowned lead, the narration taking turns between them.
- Header with model and effort; context, 5-hour and 7-day usage bars.
- Permission prompts answered with the button (tap allows, hold denies), falling back to the terminal dialog after a timeout.
- The `smalltv-pet` Claude Code plugin with the `/pet` command.
