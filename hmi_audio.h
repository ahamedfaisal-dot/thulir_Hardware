/*
 * hmi_audio.h — DFPlayer Mini wrapper
 *
 * SD card layout (one folder per language, same 15 tracks in each):
 *   /01 English   /02 Hindi   /03 Tamil   /04 Kannada   /05 Malayalam   /06 Telugu
 *   each folder:  0001-....mp3  ...  0015-....mp3   (only the leading 4-digit
 *   number matters; the rest of the file name is free text)
 *
 * Tracks are played with the DFPlayer "large folder" command, which supports
 * folders 01-15 and 4-digit file numbers (0001-3000).
 *
 * Track meaning (same number in every language folder):
 *   1 welcome to CryoAxis        9  cryo work completed
 *   2 precondition step          10 duration 1   (enter step 1 duration)
 *   3 loading solution           11 duration 2
 *   4 LEA protein                12 duration 3
 *   5 PVS2 solution              13 duration 4
 *   6 deep freeze initiated      14 automatic pipetting
 *   7 temperature deviation      15 system ready
 *   8 temperature dropping
 *
 * Process steps map to announcements:
 *   Step 1 -> 2 precondition   Step 2 -> 3 loading solution
 *   Step 3 -> 4 LEA protein    Step 4 -> 5 PVS2 solution
 *   Step 5 (ramp) -> 6 deep freeze initiated
 */
#pragma once
#include <Arduino.h>
#include <DFRobotDFPlayerMini.h>

// ─── Languages = SD folder numbers ────────────────────────
enum AudioLanguage : uint8_t {
  LANG_ENGLISH   = 1,
  LANG_HINDI     = 2,
  LANG_TAMIL     = 3,
  LANG_KANNADA   = 4,
  LANG_MALAYALAM = 5,
  LANG_TELUGU    = 6,
  LANG_COUNT     = 6
};

// ─── Track index constants (file number inside the language folder) ───
enum AudioTrack : uint8_t {
  AUDIO_WELCOME        =  1,
  AUDIO_PRECONDITION   =  2,
  AUDIO_LOADING        =  3,
  AUDIO_LEA            =  4,
  AUDIO_PVS2           =  5,
  AUDIO_DEEPFREEZE     =  6,
  AUDIO_TEMP_DEVIATION =  7,
  AUDIO_TEMP_DROPPING  =  8,
  AUDIO_CRYO_DONE      =  9,
  AUDIO_DURATION_1     = 10,
  AUDIO_DURATION_2     = 11,
  AUDIO_DURATION_3     = 12,
  AUDIO_DURATION_4     = 13,
  AUDIO_PIPETTING      = 14,
  AUDIO_SYS_READY      = 15,

  // Legacy names kept so existing code keeps compiling
  AUDIO_STEP1     = AUDIO_PRECONDITION,
  AUDIO_STEP2     = AUDIO_LOADING,
  AUDIO_STEP3     = AUDIO_LEA,
  AUDIO_STEP4     = AUDIO_PVS2,
  AUDIO_EMERGENCY = AUDIO_TEMP_DROPPING,
  AUDIO_COMPLETE  = AUDIO_CRYO_DONE,
  AUDIO_WIZARD_1  = AUDIO_DURATION_1,
  AUDIO_WIZARD_2  = AUDIO_DURATION_2,
  AUDIO_WIZARD_3  = AUDIO_DURATION_3,
  AUDIO_WIZARD_4  = AUDIO_DURATION_4,
};

// ─── Wrapper functions ────────────────────────────────────
// DFPlayer Mini produces a 'tic' click if play() is called while it is
// still processing the previous command. AudioManager enforces a minimum gap
// between commands WITHOUT blocking (it defers the track, see playTrack()).
inline void audio_playFolder(DFRobotDFPlayerMini& df, uint8_t folder, uint8_t track) {
  df.playLargeFolder(folder, track);
  Serial.printf("[AUDIO] Folder %02d  Track %04d\n", folder, track);
}

inline void audio_stop(DFRobotDFPlayerMini& df) {
  df.stop();
}

inline void audio_setVolume(DFRobotDFPlayerMini& df, uint8_t vol) {
  df.volume(vol > 30 ? 30 : vol);
}
