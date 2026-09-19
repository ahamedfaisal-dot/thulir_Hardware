/*
 * ============================================================
 *  TULIR — Audio Manager Implementation
 *  AudioManager.cpp
 * ============================================================
 */

#include "AudioManager.h"
#include <DFRobotDFPlayerMini.h>

AudioManager::AudioManager()
    : _serial(1)  // UART1
    , _player(nullptr)
    , _available(false)
    , _playing(false)
    , _lastPlayTime(0)
    , _lastTrack(0)
{
}

AudioManager::~AudioManager() {
    if (_player) {
        delete _player;
        _player = nullptr;
    }
}

bool AudioManager::begin() {
    // Initialize UART1 at 9600 baud for DFPlayer Mini
    _serial.begin(9600, SERIAL_8N1, DFPLAYER_RX_PIN, DFPLAYER_TX_PIN);

    _player = new DFRobotDFPlayerMini();

    // Give DFPlayer time to initialize after power-on
    delay(500);

    if (!_player->begin(_serial, /*isACK=*/true, /*doReset=*/true)) {
        Serial.println("[AUDIO] WARNING: DFPlayer Mini not detected");
        Serial.println("[AUDIO] Check wiring: ESP32 TX→DFPlayer RX, DFPlayer TX→ESP32 RX");
        Serial.println("[AUDIO] System will continue without audio announcements");
        _available = false;
        return false;
    }

    _player->volume(DFPLAYER_VOLUME);
    _player->outputDevice(DFPLAYER_DEVICE_SD);

    _available = true;
    Serial.printf("[AUDIO] DFPlayer Mini initialized. Volume: %d\n", DFPLAYER_VOLUME);

    return true;
}

void AudioManager::update() {
    if (!_available || !_player) return;

    // Check if playback has finished (non-blocking)
    if (_playing && (millis() - _lastPlayTime > AUDIO_MIN_INTERVAL)) {
        // Read DFPlayer status to check if still playing
        // The DFPlayer doesn't provide a reliable "done" signal,
        // so we use a minimum interval between announcements.
        _playing = false;
    }

    // Check for DFPlayer errors (non-blocking read)
    if (_player->available()) {
        uint8_t type = _player->readType();
        if (type == DFPlayerError) {
            Serial.printf("[AUDIO] DFPlayer error: %d\n", _player->read());
        }
    }
}

void AudioManager::announceSystemStart() {
    playTrack(AUDIO_SYSTEM_START);
}

void AudioManager::announceStepStart(uint8_t step) {
    if (step >= 1 && step <= 5) {
        // Step 1 = track 2, Step 2 = track 3, etc.
        playTrack(AUDIO_STEP1 + (step - 1));
    }
}

void AudioManager::announceProcessComplete() {
    playTrack(AUDIO_PROCESS_COMPLETE);
}

void AudioManager::announceEmergencyStop() {
    playTrack(AUDIO_EMERGENCY_STOP);
}

void AudioManager::announceSensorError() {
    playTrack(AUDIO_SENSOR_ERROR);
}

void AudioManager::announceOverTemp() {
    playTrack(AUDIO_OVER_TEMP);
}

void AudioManager::announceRampStarted() {
    playTrack(AUDIO_RAMP_STARTED);
}

void AudioManager::announceMinus20Reached() {
    playTrack(AUDIO_MINUS20_REACHED);
}

void AudioManager::playTrack(uint16_t track) {
    if (!_available || !_player) return;

    // Enforce minimum interval between announcements
    unsigned long now = millis();
    if (_playing && (now - _lastPlayTime) < AUDIO_MIN_INTERVAL) {
        Serial.printf("[AUDIO] Skipping track %d (too soon after %d)\n",
                      track, _lastTrack);
        return;
    }

    _player->playMp3Folder(track);
    _lastPlayTime = now;
    _lastTrack = track;
    _playing = true;

    Serial.printf("[AUDIO] Playing track %d\n", track);
}

void AudioManager::setVolume(uint8_t vol) {
    if (!_available || !_player) return;
    vol = (vol > 30) ? 30 : vol;
    _player->volume(vol);
}

bool AudioManager::isAvailable() const {
    return _available;
}

bool AudioManager::isPlaying() const {
    return _playing;
}
