/*
 * ============================================================
 *  THULIR — Audio Manager
 *  AudioManager.h
 * ============================================================
 *  Non-blocking DFPlayer Mini voice announcement system, multi-language.
 *  Uses hardware UART1 on ESP32-S3.
 *
 *  SD card: one folder per language (01 English, 02 Hindi, 03 Tamil,
 *  04 Kannada, 05 Malayalam, 06 Telugu), each with tracks 0001-0015.
 *  See hmi_audio.h for the track list. The language is chosen on the
 *  language-select screen shown after the splash (setLanguage()).
 *
 *  Tracks missing from a language folder fall back to the English folder
 *  (currently Telugu 0006 and 0015 are missing on the SD card).
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

    // --- Language ---
    void    setLanguage(uint8_t lang);          // 1..LANG_COUNT (SD folder number)
    uint8_t getLanguage() const { return _lang; }
    static const char* languageName(uint8_t lang);

    // --- Voice announcement functions ---
    void announceWelcome();
    void announceSystemStart();             // Alias for announceWelcome()
    void announceSystemReady();             // 0015 (queued after the welcome)
    void announceStepStart(uint8_t step);   // Steps 1–4 (step 5 = announceRampStarted)
    void announceWizard(uint8_t step);      // Duration prompts 1–4
    void announceProcessComplete();
    void announceEmergencyStop();
    void announceSensorError();
    void announceOverTemp();
    void announceTempDeviation();           // 0007 (rate-limited)
    void announceRampStarted();             // 0006 deep freeze initiated (Step 5)
    void announceMinus20Reached();          // no dedicated track
    void announcePipetting();               // 0014 automatic pipetting

    // Playback control
    void playTrack(uint8_t track);          // plays in the selected language
    void queueTrack(uint8_t track);         // plays when the current track ends
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

    uint8_t              _lang;            // 1..LANG_COUNT
    uint8_t              _pendingTrack;    // 0 = none
    unsigned long        _pendingSince;
    unsigned long        _lastDeviationAnnounce;
    unsigned long        _lastSendMs;      // when the last play command was sent
    uint8_t              _deferredTrack;   // track waiting out the 350 ms command gap

    static bool trackExists(uint8_t lang, uint8_t track);
};
