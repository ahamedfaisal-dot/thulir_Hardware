/*
 * ============================================================
 *  THULIR — Audio Manager Implementation
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
    , _lang(LANG_ENGLISH)
    , _pendingTrack(0)
    , _pendingSince(0)
    , _lastDeviationAnnounce(0)
    , _lastSendMs(0)
    , _deferredTrack(0)
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

    // Check for DFPlayer events (non-blocking read)
    if (_player->available()) {
        uint8_t type = _player->readType();
        if (type == DFPlayerError) {
            Serial.printf("[AUDIO] DFPlayer error: %d\n", _player->read());
        } else if (type == DFPlayerPlayFinished) {
            _playing = false;
            if (_pendingTrack) {                    // queued announcement
                uint8_t t = _pendingTrack;
                _pendingTrack = 0;
                playTrack(t);
            }
        }
    }

    // A track that arrived too soon after the previous command is sent now
    if (_deferredTrack && (millis() - _lastSendMs) >= 350UL) {
        uint8_t t = _deferredTrack;
        _deferredTrack = 0;
        playTrack(t);
    }

    // Safety net: if the "finished" event never arrives, play the queued
    // track anyway after 40 s so it is not lost.
    if (_pendingTrack && (millis() - _pendingSince > 40000UL)) {
        uint8_t t = _pendingTrack;
        _pendingTrack = 0;
        playTrack(t);
    }
}

void AudioManager::setLanguage(uint8_t lang) {
    if (lang < 1 || lang > LANG_COUNT) lang = LANG_ENGLISH;
    _lang = lang;
    _pendingTrack = 0;
    Serial.printf("[AUDIO] Language set: %s (folder %02d)\n", languageName(lang), lang);
}

const char* AudioManager::languageName(uint8_t lang) {
    switch (lang) {
        case LANG_ENGLISH:   return "English";
        case LANG_HINDI:     return "Hindi";
        case LANG_TAMIL:     return "Tamil";
        case LANG_KANNADA:   return "Kannada";
        case LANG_MALAYALAM: return "Malayalam";
        case LANG_TELUGU:    return "Telugu";
        default:             return "?";
    }
}

// Tracks that are missing from a language folder on the SD card.
// Telugu (06) has no 0006 (deep freeze initiated) and no 0015 (system ready).
bool AudioManager::trackExists(uint8_t lang, uint8_t track) {
    if (lang == LANG_TELUGU && (track == AUDIO_DEEPFREEZE || track == AUDIO_SYS_READY)) {
        return false;
    }
    return true;
}

void AudioManager::announceWelcome() {
    playTrack(AUDIO_WELCOME);
}

void AudioManager::announceSystemStart() {
    announceWelcome();
}

void AudioManager::announceSystemReady() {
    queueTrack(AUDIO_SYS_READY);
}

void AudioManager::announceStepStart(uint8_t step) {
    switch (step) {
        case 1: playTrack(AUDIO_PRECONDITION); break;   // precondition step
        case 2: playTrack(AUDIO_LOADING);      break;   // loading solution
        case 3: playTrack(AUDIO_LEA);          break;   // LEA protein
        case 4: playTrack(AUDIO_PVS2);         break;   // PVS2 solution
        case 5: playTrack(AUDIO_DEEPFREEZE);   break;   // deep freeze initiated
        default: break;
    }
}

void AudioManager::announceWizard(uint8_t step) {
    switch (step) {
        case 1: playTrack(AUDIO_DURATION_1); break;
        case 2: playTrack(AUDIO_DURATION_2); break;
        case 3: playTrack(AUDIO_DURATION_3); break;
        case 4: playTrack(AUDIO_DURATION_4); break;
        default: break;
    }
}

void AudioManager::announceProcessComplete() {
    playTrack(AUDIO_CRYO_DONE);
}

void AudioManager::announceEmergencyStop() {
    playTrack(AUDIO_TEMP_DROPPING);
}

void AudioManager::announceSensorError() {
    announceEmergencyStop();
}

void AudioManager::announceOverTemp() {
    announceEmergencyStop();
}

void AudioManager::announceTempDeviation() {
    // At most once a minute so a wobbling temperature does not chatter
    if (_lastDeviationAnnounce && (millis() - _lastDeviationAnnounce) < 60000UL) return;
    _lastDeviationAnnounce = millis();
    playTrack(AUDIO_TEMP_DEVIATION);
}

void AudioManager::announceRampStarted() {
    playTrack(AUDIO_DEEPFREEZE);
}

void AudioManager::announceMinus20Reached() {
    // -20 reached - no dedicated audio track on SD card
}

void AudioManager::announcePipetting() {
    playTrack(AUDIO_PIPETTING);
}

void AudioManager::queueTrack(uint8_t track) {
    if (!_available || !_player) return;
    if (_playing) {                 // wait for the current track to finish
        _pendingTrack = track;
        _pendingSince = millis();
    } else {
        playTrack(track);
    }
}

void AudioManager::playTrack(uint8_t track) {
    if (!_available || !_player) return;

    // DFPlayer needs ~300 ms between commands (else it clicks). Never block the
    // control loop for that: park the track and let update() send it.
    if (_lastSendMs && (millis() - _lastSendMs) < 350UL) {
        _deferredTrack = track;
        return;
    }

    uint8_t folder = _lang;
    if (!trackExists(folder, track)) {
        Serial.printf("[AUDIO] %s track %04d not on SD card - using English\n",
                      languageName(folder), track);
        folder = LANG_ENGLISH;
    }
    audio_playFolder(*_player, folder, track);
    _lastSendMs = millis();
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
