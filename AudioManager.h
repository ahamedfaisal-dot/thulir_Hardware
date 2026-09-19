/*
 * ============================================================
 *  TULIR — Audio Manager
 *  AudioManager.h
 * ============================================================
 *  Non-blocking DFPlayer Mini voice announcement system.
 *  Uses hardware UART1 on ESP32-S3.
 *
 *  Audio files on microSD (/mp3/0001.mp3 – /mp3/0012.mp3):
 *    0001 = System starting
 *    0002 = Step 1
 *    0003 = Step 2
 *    0004 = Step 3
 *    0005 = Step 4
 *    0006 = Step 5
 *    0007 = Process complete
 *    0008 = Emergency stop
 *    0009 = Temperature sensor error
 *    0010 = Over temperature fault
 *    0011 = Ramp started
 *    0012 = Minus twenty degrees reached
 * ============================================================
 */

#pragma once

#include <Arduino.h>
#include "Config.h"

// Forward declaration — actual DFPlayer library included in .cpp
class DFRobotDFPlayerMini;

class AudioManager {
public:
    AudioManager();
    ~AudioManager();

    // Initialize DFPlayer. Returns false if not detected.
    bool begin();

    // Non-blocking update — call every loop iteration
    void update();

    // --- Announcement functions ---
    void announceSystemStart();
    void announceStepStart(uint8_t step);    // 1–5
    void announceProcessComplete();
    void announceEmergencyStop();
    void announceSensorError();
    void announceOverTemp();
    void announceRampStarted();
    void announceMinus20Reached();

    // Play a specific track number (1-based)
    void playTrack(uint16_t track);

    // Set volume (0–30)
    void setVolume(uint8_t vol);

    // Is the player available?
    bool isAvailable() const;

    // Is a track currently playing?
    bool isPlaying() const;

private:
    HardwareSerial      _serial;
    DFRobotDFPlayerMini* _player;
    bool                _available;
    bool                _playing;
    unsigned long       _lastPlayTime;
    uint16_t            _lastTrack;
    uint16_t            _pendingTrack;  // 0 = none; a track throttled by
                                         // AUDIO_MIN_INTERVAL that should
                                         // still play once the window clears
};
