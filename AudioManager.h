/*
 * ============================================================
 *  TULIR — Audio Manager
 *  AudioManager.h
 * ============================================================
 *  Non-blocking DFPlayer Mini voice announcement system.
 *  Uses hardware UART1 on ESP32-S3.
 *
 *  Audio files on microSD (/mp3/0001.mp3 – /mp3/0011.mp3):
 *    0001 = Step 1 ("Step One — Starting")
 *    0002 = Step 2 ("Step Two — Cooling")
 *    0003 = Step 3 ("Step Three — Deep Freeze")
 *    0004 = Step 4 ("Step Four — Stabilizing")
 *    0005 = Emergency ("Warning — Temperature Dropping")
 *    0006 = Process complete ("Process Completed Successfully")
 *    0007 = Welcome ("Welcome to THULIR CryoLab")
 *    0008 = Wizard 1 ("Enter Step One Duration")
 *    0009 = Wizard 2 ("Enter Step Two Duration")
 *    0010 = Wizard 3 ("Enter Step Three Duration")
 *    0011 = Wizard 4 ("Enter Step Four Duration")
 * ============================================================
 */

#pragma once

#include <Arduino.h>
#include "hmi_audio.h"

class AudioManager {
public:
    AudioManager();
    ~AudioManager();

    // Initialize DFPlayer. Returns false if not detected.
    bool begin();

    // Non-blocking update — call every loop iteration
    void update();

    // --- Voice announcement functions ---
    void announceWelcome();
    void announceSystemStart();             // Alias for announceWelcome()
    void announceStepStart(uint8_t step);   // Steps 1–4
    void announceWizard(uint8_t step);      // Wizard 1–4 (enter duration)
    void announceProcessComplete();
    void announceEmergencyStop();
    void announceSensorError();
    void announceOverTemp();
    void announceRampStarted();             // Kept for backward compatibility
    void announceMinus20Reached();          // Kept for backward compatibility

    // Playback control
    void playTrack(uint8_t track);
    void stop();
    void setVolume(uint8_t vol);

    // Direct module access (for code passing DFRobotDFPlayerMini reference)
    DFRobotDFPlayerMini* getPlayer() { return _player; }
    DFRobotDFPlayerMini& dfPlayer() { return *_player; }

    // Status queries
    bool isAvailable() const;
    bool isPlaying() const;

private:
    HardwareSerial       _serial;
    DFRobotDFPlayerMini* _player;
    bool                 _available;
    bool                 _playing;
    unsigned long        _lastPlayTime;
    uint8_t              _lastTrack;
};
