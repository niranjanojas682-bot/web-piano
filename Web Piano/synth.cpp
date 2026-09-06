#include <emscripten.h>
#include <cmath>
#include <map>
#include <vector>

// A simple polyphonic triangle-wave synth engine written in C++.
// This handles all the audio math (oscillator phase, envelope, mixing).
// JavaScript only feeds it note on/off events and pulls back audio samples.

struct Voice {
    bool active = false;
    double freq = 0.0;
    double phase = 0.0;
    double gain = 0.0;      // current envelope level
    double target = 0.0;    // target level (attack or release)
    bool releasing = false;
};

const int MAX_VOICES = 16;
const double SAMPLE_RATE = 44100.0;
const double ATTACK_RATE = 0.02;   // gain units per sample block step
const double RELEASE_RATE = 0.01;

Voice voices[MAX_VOICES];
std::map<int, int> keyToVoice; // maps a key id -> voice index

extern "C" {

EMSCRIPTEN_KEEPALIVE
void note_on(int keyId, double freq) {
    // Reuse a voice if this key is already sounding
    if (keyToVoice.count(keyId)) {
        int idx = keyToVoice[keyId];
        voices[idx].freq = freq;
        voices[idx].releasing = false;
        voices[idx].target = 0.28;
        return;
    }
    // Find a free voice
    for (int i = 0; i < MAX_VOICES; i++) {
        if (!voices[i].active) {
            voices[i].active = true;
            voices[i].freq = freq;
            voices[i].phase = 0.0;
            voices[i].gain = 0.0;
            voices[i].target = 0.28;
            voices[i].releasing = false;
            keyToVoice[keyId] = i;
            return;
        }
    }
}

EMSCRIPTEN_KEEPALIVE
void note_off(int keyId) {
    if (keyToVoice.count(keyId)) {
        int idx = keyToVoice[keyId];
        voices[idx].releasing = true;
        voices[idx].target = 0.0;
        keyToVoice.erase(keyId);
    }
}

// Fills a buffer of `numSamples` floats with the mixed audio.
// Called continuously from JS via a ScriptProcessor/AudioWorklet.
EMSCRIPTEN_KEEPALIVE
void render(float* outBuffer, int numSamples) {
    for (int s = 0; s < numSamples; s++) {
        double mix = 0.0;
        for (int i = 0; i < MAX_VOICES; i++) {
            Voice& v = voices[i];
            if (!v.active) continue;

            // envelope: ease gain toward target
            if (v.gain < v.target) {
                v.gain += ATTACK_RATE;
                if (v.gain > v.target) v.gain = v.target;
            } else if (v.gain > v.target) {
                v.gain -= RELEASE_RATE;
                if (v.gain < v.target) v.gain = v.target;
            }

            // triangle wave oscillator
            double t = v.phase;
            double tri = 2.0 * fabs(2.0 * (t - floor(t + 0.5))) - 1.0;
            mix += tri * v.gain;

            v.phase += v.freq / SAMPLE_RATE;
            if (v.phase >= 1.0) v.phase -= 1.0;

            // free the voice once fully released
            if (v.releasing && v.gain <= 0.0001) {
                v.active = false;
                v.phase = 0.0;
            }
        }
        // soft clip to avoid harsh clipping when many notes stack
        if (mix > 1.0) mix = 1.0;
        if (mix < -1.0) mix = -1.0;
        outBuffer[s] = (float)mix;
    }
}

} // extern "C"
