// Kupata — 16-pad drum sampler engine (no JUCE: it can be tested on its own)
// Copyright (C) 2026 Lautaro Martín Vieyra
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Sixteen pads, each with a sound and six knobs: Shape, Tune, Tone, Compression,
// Volume and Pan. Shape and Compression work from the sound's own envelope, measured
// when it is loaded, so they behave musically with any sound and keep its loudness
// steady. Tone reaches further on dark sounds than on bright ones. Every pad can be
// trimmed, gated, muted, soloed, put in a choke group and sent to its own outputs.

#pragma once

#include "Analysis.h"
#include <cstdint>

namespace kupata
{

constexpr int numPads    = 16;
constexpr int maxVoices  = 64;
constexpr int maxPerPad  = 6;     // overlapping hits of one pad
constexpr int maxOutputs = 64;    // output channels
constexpr int numChokes  = 8;     // choke groups (0 = none)
constexpr int firstNote  = 36;    // C2 (middle C = C4 = 60) plays pad 1

// Settings of a pad that the host sees (automatable).
struct PadParams
{
    float shape  = 0.0f;     // -100 … +100
    float tune   = 0.0f;     // semitones, -24 … +24
    float tone   = 0.0f;     // -100 (dark) … +100 (bright)
    float comp   = 0.0f;     // 0 … 100
    float volume = 0.0f;     // dB, -60 … +6
    float pan    = 0.0f;     // -100 … +100
    float start  = 0.0f;     // % of the sound
    float end    = 100.0f;   // % of the sound
    bool gate = false, mute = false, solo = false;
};

// Settings of a pad that belong to the kit (stored with the session).
struct PadRouting
{
    int  note = firstNote;   // MIDI note that plays it
    int  out = 0;            // first output channel (0 = channel 1)
    bool stereo = true;      // a pair (out, out + 1) or a single channel
    int  choke = 0;          // 0 none, 1 … numChokes
};

struct Params
{
    std::array<PadParams, numPads> pads {};
    float gain = 0.0f;       // dB, the whole kit
    float velocity = 70.0f;  // % of velocity sensitivity
};

struct Event
{
    enum Type { noteOn, noteOff, allNotesOff, padOn, padOff } type = noteOn;
    int offset = 0;
    int note = 0;            // noteOn / noteOff
    int pad = 0;             // padOn / padOff (from the interface)
    float velocity = 1.0f;
};

struct Telemetry
{
    float padLevel[numPads] {};    // how loud each pad sounds now, 0 … 1
    float padPos[numPads] {};      // where its newest hit is, 0 … 1 of the sound; -1 silent
    float previewPos = -1.0f;
    float outPeak = 0.0f;
};

//==============================================================================
namespace detail
{
    inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }

    inline float hermite (const float* d, int lo, int hi, int i, float t) noexcept
    {
        auto at = [&] (int k) { return d[std::clamp (k, lo, hi - 1)]; };
        const float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

    // State-variable filter (trapezoidal), one per channel.
    struct Svf
    {
        float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f, k = 1.414f;
        float s1[2] {}, s2[2] {};
        void set (float fc, float q, double sr) noexcept
        {
            fc = std::clamp (fc, 5.0f, (float) (0.45 * sr));
            const float g = std::tan (3.14159265f * fc / (float) sr);
            k = 1.0f / std::max (0.3f, q);
            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }
        // returns low, band and high outputs
        inline void tick (int ch, float x, float& lp, float& bp, float& hp) noexcept
        {
            const float v3 = x - s2[ch];
            const float v1 = a1 * s1[ch] + a2 * v3;
            const float v2 = s2[ch] + a2 * s1[ch] + a3 * v3;
            s1[ch] = 2.0f * v1 - s1[ch];
            s2[ch] = 2.0f * v2 - s2[ch];
            lp = v2; bp = v1; hp = x - k * v1 - v2;
        }
        void reset() noexcept { s1[0] = s1[1] = s2[0] = s2[1] = 0.0f; }
    };

    // What the Tone knob does to a sound of a given brightness.
    struct ToneSettings { float lp, lpQ, hp, hpQ, shelfHz, shelfGain; };
    inline ToneSettings toneFor (float tone, float centroid, double sr) noexcept
    {
        ToneSettings s { std::min (20000.0f, (float) (0.45 * sr)), 0.707f, 8.0f, 0.707f, 3000.0f, 1.0f };
        const float t = std::clamp (tone * 0.01f, -1.0f, 1.0f);
        if (t < 0.0f)
        {
            const float a = -t;
            const float lpMin = std::clamp (0.3f * centroid, 90.0f, 2500.0f);
            s.lp = s.lp * std::pow (lpMin / s.lp, std::pow (a, 0.8f));
            s.lpQ = 0.707f + 0.9f * a * a;          // the cutoff gets a little accent as it closes
        }
        else if (t > 0.0f)
        {
            const float a = t;
            const float hpMax = std::clamp (1.2f * centroid, 250.0f, 6000.0f);
            s.hp = 8.0f * std::pow (hpMax / 8.0f, std::pow (a, 1.1f));
            s.hpQ = 0.707f + 0.4f * a;
            s.shelfHz = std::clamp (2.5f * centroid, 1500.0f, 8000.0f);
            s.shelfGain = std::pow (10.0f, 9.0f * a / 20.0f);   // up to +9 dB of air
        }
        return s;
    }

    inline void panGains (float pan, float& l, float& r) noexcept   // balance: the centre is at full level
    {
        const float p = std::clamp (pan * 0.01f, -1.0f, 1.0f);
        l = p > 0.0f ? std::cos (p * 1.5707963f) : 1.0f;
        r = p < 0.0f ? std::cos (-p * 1.5707963f) : 1.0f;
    }
}

//==============================================================================
// One sounding hit.
class Voice
{
public:
    bool active = false;
    int pad = 0, note = -1;
    uint32_t age = 0;
    bool releasing = false;

    void start (const Sound& s, const PadParams& p, float velocity, double engineSr, int padIndex, int noteNumber, uint32_t serial) noexcept
    {
        snd = &s;
        pad = padIndex; note = noteNumber; age = serial;
        active = true; releasing = false;
        sr = engineSr;
        const int len = s.regionLength();
        startFrame = s.regionStart + (int) (std::clamp (p.start, 0.0f, 100.0f) * 0.01f * len);
        endFrame = s.regionStart + (int) (std::clamp (p.end, 0.0f, 100.0f) * 0.01f * len);
        endFrame = std::clamp (endFrame, std::min (s.regionEnd, startFrame + 64), s.regionEnd);
        pos = startFrame;
        fadeIn = startFrame > s.regionStart + 8;
        vel = velocity;
        release = 1.0f; releaseStep = 0.0f;
        gr = 0.0f;
        lp.reset(); hp.reset(); shelf.reset();
        first = true;
        level = 0.0f;
    }

    void stop (float seconds) noexcept   // fades out
    {
        if (! active) return;
        const float step = 1.0f / std::max (1.0f, seconds * (float) sr);
        if (releasing) { releaseStep = std::max (releaseStep, step); return; }
        releasing = true;
        releaseStep = step;
    }

    // Adds samples [from, to) to the outputs. mutedNow: the pad is muted (or another one is soloed).
    void render (float* const* out, int numOut, int from, int to, const PadParams& p, const PadRouting& r,
                 float velocityAmount, bool mutedNow) noexcept
    {
        if (! active || snd == nullptr) return;
        const Sound& s = *snd;
        const Sample& smp = *s.sample;
        const float* L = smp.left.data();
        const float* R = smp.right.data();
        const double srcSr = smp.sampleRate;
        const double inc = srcSr / sr * std::pow (2.0, std::clamp (p.tune, -48.0f, 48.0f) / 12.0);

        // output channels
        int chL = r.out, chR = r.stereo ? r.out + 1 : r.out;
        if (chL < 0 || chR >= numOut) { chL = 0; chR = numOut > 1 ? 1 : 0; }
        const bool mono = chL == chR;

        const float shape = std::clamp (p.shape * 0.01f, -1.0f, 1.0f);
        const float shapeComp = analysis::lookup (s.shapeComp, shape, -1.0f, 1.0f);
        const float compAmount = std::clamp (p.comp * 0.01f, 0.0f, 1.0f);
        shaping::Comp comp;
        comp.set (compAmount, 20.0f * std::log10 (s.peak));
        const float makeup = analysis::lookup (s.compMakeup, compAmount, 0.0f, 1.0f);
        const float vs = std::clamp (velocityAmount * 0.01f, 0.0f, 1.0f);
        const float velGain = (1.0f - vs) + vs * std::pow (std::clamp (vel, 0.0f, 1.0f), 1.5f);
        const float volGain = p.volume <= -59.9f ? 0.0f : detail::dbToGain (p.volume);
        float panL, panR;
        detail::panGains (mono ? 0.0f : p.pan, panL, panR);
        if (mono) panL = panR = 1.0f;

        const auto tone = detail::toneFor (p.tone, s.centroid, sr);
        lp.set (tone.lp, tone.lpQ, sr);
        hp.set (tone.hp, tone.hpQ, sr);
        shelf.set (tone.shelfHz, 0.707f, sr);
        const float shelfExtra = tone.shelfGain - 1.0f;
        const bool useLp = p.tone < 0.0f, useHp = p.tone > 0.0f;

        const float muteTarget = mutedNow ? 0.0f : 1.0f;
        const float dtChunk = (float) sub / (float) sr;
        const float att = 1.0f - std::exp (-dtChunk / shaping::compAttack), rel = 1.0f - std::exp (-dtChunk / shaping::compRelease);
        const int fadeOutLen = (int) (srcSr * 0.003), fadeInLen = (int) (srcSr * 0.0015);

        auto controlGain = [&] (double position) {
            const float t = (float) ((position - startFrame) / srcSr);
            const float e = s.envAt (position);
            const bool beforePeak = (position - s.regionStart) / s.hop < s.peakIndex;
            const float gs = shaping::shapeGain (shape, t, e, beforePeak, s.length) * shapeComp;
            const float levelDb = 20.0f * std::log10 (std::max (1.0e-7f, e * s.peak * gs));
            const float target = comp.reductionDb (levelDb);
            gr += (target - gr) * (target < gr ? att : rel);
            float edge = 1.0f;
            const double toEnd = endFrame - position;
            if (toEnd < fadeOutLen) edge = (float) std::max (0.0, toEnd / fadeOutLen);
            if (fadeIn && position - startFrame < fadeInLen) edge *= (float) ((position - startFrame) / fadeInLen);
            return gs * (compAmount > 0.0f ? detail::dbToGain (gr) * makeup : 1.0f) * edge;
        };

        int i = from;
        while (i < to && active)
        {
            const int m = std::min (sub, to - i);
            // gain at the start and at the end of this piece, ramped in between
            if (first) muteGain = muteTarget;
            const float gA = first ? controlGain (pos) : lastGain;
            const float gB = controlGain (pos + inc * m);
            first = false;
            lastGain = gB;
            muteGain += (muteTarget - muteGain) * 0.12f;
            const float common = velGain * volGain * muteGain;

            for (int k = 0; k < m; ++k)
            {
                const double ipos = std::floor (pos);
                const int idx = (int) ipos;
                if (idx >= endFrame) { active = false; break; }
                const float frac = (float) (pos - ipos);
                float xl = detail::hermite (L, startFrame, endFrame, idx, frac);
                float xr = detail::hermite (R, startFrame, endFrame, idx, frac);

                const float g = (gA + (gB - gA) * (float) k / (float) m) * common * release;
                xl *= g; xr *= g;
                float l1, b1, h1, l2, b2, h2, l3, b3, h3;
                // at the centre the sound passes untouched; each side brings in its own filter
                lp.tick (0, xl, l1, b1, h1);
                if (useLp) xl = l1;
                hp.tick (0, xl, l2, b2, h2);
                if (useHp) { shelf.tick (0, h2, l3, b3, h3); xl = h2 + shelfExtra * h3; }
                lp.tick (1, xr, l1, b1, h1);
                if (useLp) xr = l1;
                hp.tick (1, xr, l2, b2, h2);
                if (useHp) { shelf.tick (1, h2, l3, b3, h3); xr = h2 + shelfExtra * h3; }
                const float yl = xl, yr = xr;

                if (mono) out[chL][i + k] += 0.5f * (yl + yr);
                else { out[chL][i + k] += yl * panL; out[chR][i + k] += yr * panR; }
                level = std::max (level * 0.9995f, std::abs (yl) + std::abs (yr));

                pos += inc;
                if (releasing)
                {
                    release -= releaseStep;
                    if (release <= 0.0f) { active = false; break; }
                }
            }
            i += m;
        }
        if (! std::isfinite (lastGain)) active = false;
    }

    float position01() const noexcept   // where the hit is, in its sound
    {
        if (snd == nullptr || snd->regionLength() <= 0) return 0.0f;
        return (float) std::clamp ((pos - snd->regionStart) / snd->regionLength(), 0.0, 1.0);
    }
    float currentLevel() const noexcept { return std::min (1.0f, level * 0.5f) * release; }
    const Sound* sound() const noexcept { return snd; }

private:
    static constexpr int sub = 16;   // the knobs' gains are computed every 16 samples
    const Sound* snd = nullptr;
    double sr = 44100.0, pos = 0.0;
    int startFrame = 0, endFrame = 0;
    bool fadeIn = false, first = true;
    float vel = 1.0f, release = 1.0f, releaseStep = 0.0f, gr = 0.0f, lastGain = 1.0f, muteGain = 1.0f, level = 0.0f;
    detail::Svf lp, hp, shelf;
};

//==============================================================================
class Engine
{
public:
    std::array<std::shared_ptr<const Sound>, numPads> sounds {};   // change with the engine locked
    std::array<PadRouting, numPads> routing {};

    Engine()
    {
        for (int p = 0; p < numPads; ++p) routing[(size_t) p].note = firstNote + p;
    }

    void prepare (double sampleRate)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        reset();
    }

    void reset() noexcept
    {
        for (auto& v : voices) v.active = false;
        previewVoice.active = false;
        smoothedGain = -1.0f;
    }

    // Call (with the engine locked) before replacing or freeing the sound of a pad.
    void stopVoicesOf (int pad) noexcept
    {
        for (auto& v : voices) if (v.active && v.pad == pad) v.active = false;
    }

    // The interface's file browser: plays a sound once on outputs 1-2 (null stops it).
    void setPreview (std::shared_ptr<const Sound> s) noexcept
    {
        previewVoice.active = false;
        preview = std::move (s);
        previewPending = preview != nullptr;
    }
    std::shared_ptr<const Sound> takePreview() noexcept { previewVoice.active = false; return std::move (preview); }

    void process (float* const* out, int numOut, int n, const Params& p, const Event* events, int numEvents)
    {
        numOut = std::min (numOut, maxOutputs);
        for (int ch = 0; ch < numOut; ++ch) std::fill (out[ch], out[ch] + n, 0.0f);
        if (n <= 0 || numOut <= 0) return;
        params = p;
        anySolo = false;
        for (const auto& pp : params.pads) anySolo = anySolo || pp.solo;

        int done = 0, ei = 0;
        while (done < n)
        {
            int next = n;
            if (ei < numEvents) next = std::clamp (events[ei].offset, done, n);
            renderVoices (out, numOut, done, next);
            done = next;
            while (ei < numEvents && std::clamp (events[ei].offset, 0, n) <= done) handle (events[ei++]);
        }
        while (ei < numEvents) handle (events[ei++]);

        // the kit's output level, smoothed across the block
        const float target = detail::dbToGain (p.gain);
        if (smoothedGain < 0.0f) smoothedGain = target;
        const float step = (target - smoothedGain) / (float) n;
        float peak = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float g = smoothedGain + step * (float) (i + 1);
            for (int ch = 0; ch < numOut; ++ch)
            {
                float& s = out[ch][i];
                s *= g;
                if (! std::isfinite (s)) s = 0.0f;
                peak = std::max (peak, std::abs (s));
            }
        }
        smoothedGain = target;
        peakSinceSnapshot = std::max (peakSinceSnapshot, peak);
    }

    void snapshot (Telemetry& t) noexcept
    {
        for (int pd = 0; pd < numPads; ++pd) { t.padLevel[pd] = 0.0f; t.padPos[pd] = -1.0f; }
        uint32_t newest[numPads] {};
        for (const auto& v : voices)
        {
            if (! v.active) continue;
            t.padLevel[v.pad] = std::max (t.padLevel[v.pad], v.currentLevel());
            if (t.padPos[v.pad] < 0.0f || v.age - newest[v.pad] < 0x80000000u)
            {
                newest[v.pad] = v.age;
                t.padPos[v.pad] = v.position01();
            }
        }
        t.previewPos = previewVoice.active ? previewVoice.position01() : -1.0f;
        t.outPeak = std::max (t.outPeak, peakSinceSnapshot);
        peakSinceSnapshot = 0.0f;
    }

    // Renders a pad offline (mono, no volume or pan), for the interface's picture of it.
    static void renderOffline (const Sound& s, PadParams p, double sampleRate, int maxFrames, std::vector<float>& dest)
    {
        dest.clear();
        Voice v;
        p.volume = 0.0f; p.pan = 0.0f; p.mute = false; p.gate = false;
        v.start (s, p, 1.0f, sampleRate, 0, -1, 0);
        PadRouting r;
        r.out = 0; r.stereo = false;
        std::vector<float> buf (256);
        float* ptr[1] { buf.data() };
        while (v.active && (int) dest.size() < maxFrames)
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            v.render (ptr, 1, 0, (int) buf.size(), p, r, 0.0f, false);
            dest.insert (dest.end(), buf.begin(), buf.end());
        }
        if ((int) dest.size() > maxFrames) dest.resize ((size_t) maxFrames);
    }

private:
    void renderVoices (float* const* out, int numOut, int from, int to) noexcept
    {
        if (to <= from) return;
        for (auto& v : voices)
        {
            if (! v.active) continue;
            const auto& pp = params.pads[(size_t) v.pad];
            v.render (out, numOut, from, to, pp, routing[(size_t) v.pad], params.velocity, pp.mute || (anySolo && ! pp.solo));
        }
        if (previewPending && preview != nullptr)
        {
            previewPending = false;
            PadParams flat;
            previewVoice.start (*preview, flat, 1.0f, sr, -1, -1, 0);
        }
        if (previewVoice.active)
        {
            PadParams flat;
            flat.volume = -4.0f;
            PadRouting r;
            previewVoice.render (out, numOut, from, to, flat, r, 0.0f, false);
        }
    }

    void trigger (int pad, int note, float velocity) noexcept
    {
        if (pad < 0 || pad >= numPads) return;
        const auto& snd = sounds[(size_t) pad];
        if (snd == nullptr || snd->regionLength() < 2) return;
        const auto& rt = routing[(size_t) pad];

        // choke: this pad silences the others of its group
        if (rt.choke > 0)
            for (auto& v : voices)
                if (v.active && v.pad != pad && routing[(size_t) v.pad].choke == rt.choke) v.stop (0.008f);

        // too many hits of this pad: the oldest fades quickly
        int count = 0;
        Voice* oldest = nullptr;
        for (auto& v : voices)
            if (v.active && v.pad == pad && ! v.releasing)
            {
                ++count;
                if (oldest == nullptr || v.age - oldest->age > 0x80000000u) oldest = &v;
            }
        if (count >= maxPerPad && oldest != nullptr) oldest->stop (0.004f);

        Voice* slot = nullptr;
        for (auto& v : voices) if (! v.active) { slot = &v; break; }
        if (slot == nullptr)
        {
            // every voice is busy: take the oldest of all, preferring those already fading
            for (auto& v : voices)
                if (slot == nullptr || (v.releasing && ! slot->releasing)
                    || (v.releasing == slot->releasing && v.age - slot->age > 0x80000000u)) slot = &v;
        }
        slot->start (*snd, params.pads[(size_t) pad], velocity, sr, pad, note, ++serial);
    }

    void releasePad (int pad, int note) noexcept
    {
        if (pad < 0 || pad >= numPads || ! params.pads[(size_t) pad].gate) return;
        for (auto& v : voices)
            if (v.active && v.pad == pad && (note < 0 || v.note == note)) v.stop (0.02f);
    }

    void handle (const Event& e) noexcept
    {
        switch (e.type)
        {
            case Event::noteOn:
                for (int pd = 0; pd < numPads; ++pd)
                    if (routing[(size_t) pd].note == e.note) trigger (pd, e.note, e.velocity);
                break;
            case Event::noteOff:
                for (int pd = 0; pd < numPads; ++pd)
                    if (routing[(size_t) pd].note == e.note) releasePad (pd, e.note);
                break;
            case Event::padOn:  trigger (e.pad, -2, e.velocity); break;
            case Event::padOff: releasePad (e.pad, -2); break;
            case Event::allNotesOff:
                for (auto& v : voices) v.stop (0.01f);
                previewVoice.stop (0.01f);
                break;
        }
    }

    double sr = 44100.0;
    Params params;
    bool anySolo = false;
    std::array<Voice, maxVoices> voices {};
    Voice previewVoice;
    std::shared_ptr<const Sound> preview;
    bool previewPending = false;
    uint32_t serial = 0;
    float smoothedGain = -1.0f, peakSinceSnapshot = 0.0f;
};

} // namespace kupata
