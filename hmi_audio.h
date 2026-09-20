/*
 * hmi_audio.h — DFPlayer Mini wrapper
 *
 * Track numbers match the user's SD card filenames:
 *   /mp3/0001.mp3  through  /mp3/0011.mp3
 *
 * The dfPlayer object is defined in igem.ino and passed
 * as a reference wherever audio is needed.
 */
#pragma once
#include <Arduino.h>
#include <DFRobotDFPlayerMini.h>

// ─── Track index constants ────────────────────────────────
enum AudioTrack : uint8_t {
  AUDIO_STEP1      =  1,   // "Step One — Starting"
  AUDIO_STEP2      =  2,   // "Step Two — Cooling"
  AUDIO_STEP3      =  3,   // "Step Three — Deep Freeze"
  AUDIO_STEP4      =  4,   // "Step Four — Stabilizing"
  AUDIO_EMERGENCY  =  5,   // "Warning — Temperature Dropping"
  AUDIO_COMPLETE   =  6,   // "Process Completed Successfully"
  AUDIO_WELCOME    =  7,   // "Welcome to THULIR CryoLab"
  AUDIO_WIZARD_1   =  8,   // "Enter Step One Duration"
  AUDIO_WIZARD_2   =  9,   // "Enter Step Two Duration"
  AUDIO_WIZARD_3   = 10,   // "Enter Step Three Duration"
  AUDIO_WIZARD_4   = 11,   // "Enter Step Four Duration"
};

// ─── Wrapper functions ────────────────────────────────────
// DFPlayer Mini produces a 'tic' click if play() is called while it is
// still processing the previous command. We enforce a 300 ms minimum gap.
static uint32_t _audio_lastPlayMs = 0;

inline void audio_play(DFRobotDFPlayerMini& df, uint8_t track) {
  uint32_t now = millis();
  if (now - _audio_lastPlayMs < 300) {
    delay(300 - (now - _audio_lastPlayMs));  // short block to let module settle
  }
  _audio_lastPlayMs = millis();
  df.play(track);
  Serial.printf("[AUDIO] Track %02d\n", track);
}

inline void audio_stop(DFRobotDFPlayerMini& df) {
  df.stop();
}

inline void audio_setVolume(DFRobotDFPlayerMini& df, uint8_t vol) {
  df.volume(vol > 30 ? 30 : vol);
}
