/*
 * ============================================================
 *  TULIR — Audio Manager Implementation
 *  AudioManager.cpp
 * ============================================================
 */

#include "AudioManager.h"
#include "Config.h"

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

    // Using isACK=false ensures non-blocking command execution and tolerance for clone chips
    if (!_player->begin(_serial, /*isACK=*/false, /*doReset=*/true)) {
        Serial.println("[AUDIO] WARNING: DFPlayer Mini not detected");
        Serial.println("[AUDIO] Check wiring: ESP32 TX(GPIO 1) -> DFPlayer RX, DFPlayer TX -> ESP32 RX(GPIO 2)");
        Serial.println("[AUDIO] System will continue without audio announcements");
        _available = false;
        return false;
    }

    _available = true;
    audio_setVolume(*_player, DFPLAYER_VOLUME);
    _player->outputDevice(DFPLAYER_DEVICE_SD);

    Serial.printf("[AUDIO] DFPlayer Mini initialized. Volume: %d\n", DFPLAYER_VOLUME);
    return true;
}

void AudioManager::update() {
    if (!_available || !_player) return;

    // Check if estimated playback interval has elapsed
    if (_playing && (millis() - _lastPlayTime > AUDIO_MIN_INTERVAL)) {
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

void AudioManager::announceWelcome() {
    playTrack(AUDIO_WELCOME);
}

void AudioManager::announceSystemStart() {
    announceWelcome();
}

void AudioManager::announceStepStart(uint8_t step) {
    switch (step) {
        case 1: playTrack(AUDIO_STEP1); break;
        case 2: playTrack(AUDIO_STEP2); break;
        case 3: playTrack(AUDIO_STEP3); break;
        case 4: playTrack(AUDIO_STEP4); break;
        default: break;
    }
}

void AudioManager::announceWizard(uint8_t step) {
    switch (step) {
        case 1: playTrack(AUDIO_WIZARD_1); break;
        case 2: playTrack(AUDIO_WIZARD_2); break;
        case 3: playTrack(AUDIO_WIZARD_3); break;
        case 4: playTrack(AUDIO_WIZARD_4); break;
        default: break;
    }
}

void AudioManager::announceProcessComplete() {
    playTrack(AUDIO_COMPLETE);
}

void AudioManager::announceEmergencyStop() {
    playTrack(AUDIO_EMERGENCY);
}

void AudioManager::announceSensorError() {
    announceEmergencyStop();
}

void AudioManager::announceOverTemp() {
    announceEmergencyStop();
}

void AudioManager::announceRampStarted() {
    // Step 5 ramp - no dedicated audio track on SD card
}

void AudioManager::announceMinus20Reached() {
    // -20 reached - no dedicated audio track on SD card
}

void AudioManager::playTrack(uint8_t track) {
    if (!_available || !_player) return;

    audio_play(*_player, track);
    _lastPlayTime = millis();
    _lastTrack = track;
    _playing = true;
}

void AudioManager::stop() {
    if (!_available || !_player) return;
    audio_stop(*_player);
    _playing = false;
}

void AudioManager::setVolume(uint8_t vol) {
    if (!_available || !_player) return;
    audio_setVolume(*_player, vol);
}

bool AudioManager::isAvailable() const {
    return _available;
}

bool AudioManager::isPlaying() const {
    return _playing;
}
