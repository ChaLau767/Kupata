// Kupata — what a sound is made of: its envelope, peak, brightness and pitch,
// where its hits are, and the tables that keep its loudness steady when the
// Shape and Compression knobs turn. No JUCE: it can be tested on its own.
// Copyright (C) 2026 Lautaro Martín Vieyra
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <memory>
#include <vector>

namespace kupata
{

struct Sample
{
    std::vector<float> left, right;
    double sampleRate = 44100.0;
    int length() const noexcept { return (int) left.size(); }
};

// The Shape and Compression knobs are computed from the sound's envelope, so their
// loudness compensation can be worked out once, when the sound is loaded.
constexpr int shapeSteps = 33;     // Shape -100 … +100 in steps of 6.25
constexpr int compSteps  = 17;     // Compression 0 … 100 in steps of 6.25

// Everything the engine needs to play one pad. Built on the message thread, then
// handed to the engine, which never changes it.
struct Sound
{
    std::shared_ptr<const Sample> sample;
    int regionStart = 0, regionEnd = 0;   // the part that plays (frames of the sample)
    int hop = 48;                         // frames per envelope value (1 ms)
    std::vector<float> env;               // smoothed amplitude from regionStart, 0 … 1 (1 = the peak)
    float peak = 1.0f;                    // linear peak of the region
    int peakIndex = 0;                    // envelope index of the loudest moment
    float length = 0.2f;                  // seconds until the envelope falls 40 dB below its peak
    float centroid = 1000.0f;             // Hz: how bright it is
    float pitch = 0.0f;                   // Hz, 0 when it has no clear pitch
    std::array<float, shapeSteps> shapeComp {};   // gain that keeps the loudness steady, per Shape step
    std::array<float, compSteps> compMakeup {};   // make-up gain of the compressor, per Compression step

    int regionLength() const noexcept { return regionEnd - regionStart; }
    float envAt (double frame) const noexcept   // frame: position in the sample
    {
        if (env.empty()) return 0.0f;
        const double x = (frame - regionStart) / (double) hop;
        if (x <= 0.0) return env.front();
        const int i = (int) x;
        if (i >= (int) env.size() - 1) return env.back();
        const float t = (float) (x - i);
        return env[(size_t) i] + t * (env[(size_t) i + 1] - env[(size_t) i]);
    }
};

//==============================================================================
// The two knobs that are computed from the envelope, shared by the engine and the analysis.
namespace shaping
{
    // Shape: -1 … +1. Left shortens the sound with a fade that gets faster and faster (at the
    // end only a click remains); right lifts its tail (more sustain) and, past the middle,
    // softens its attack. t: seconds since the start, e: envelope (1 = peak), length: Sound::length.
    inline float shapeGain (float shape, float t, float e, bool beforePeak, float length) noexcept
    {
        if (shape < 0.0f)
        {
            const float a = std::min (1.0f, -shape);
            const float tau = std::max (0.003f, std::max (0.03f, length) * std::pow (10.0f, -2.4f * std::pow (a, 0.8f)));
            const float hold = 0.002f;
            return t <= hold ? 1.0f : std::exp (-(t - hold) / tau);
        }
        if (shape > 0.0f)
        {
            const float a = std::min (1.0f, shape);
            float g = 1.0f;
            if (! beforePeak)
            {
                const float k = 0.9f * std::pow (a, 1.3f);
                const float maxGain = std::pow (10.0f, 30.0f * a / 20.0f);
                g = std::min (maxGain, std::pow (std::max (e, 1.0e-4f), -k));
            }
            if (a > 0.5f)
            {
                const float attack = 0.04f * std::pow ((a - 0.5f) * 2.0f, 1.5f);
                if (t < attack) g *= t / attack;
            }
            return g;
        }
        return 1.0f;
    }

    // Compression: 0 … 1. The threshold falls below the sound's peak and the ratio rises, so
    // the loud start comes down more than the body; with the make-up gain the body rises
    // (more sustain) and the first millisecond of the attack slips through (more punch).
    struct Comp
    {
        float thresholdDb = 0.0f, ratio = 1.0f;
        void set (float amount, float peakDb) noexcept
        {
            const float c = std::clamp (amount, 0.0f, 1.0f);
            thresholdDb = peakDb - 24.0f * c;
            ratio = 1.0f + 5.0f * std::pow (c, 1.2f);
        }
        float reductionDb (float levelDb) const noexcept   // ≤ 0
        {
            const float over = levelDb - thresholdDb;
            return over > 0.0f ? -over * (1.0f - 1.0f / ratio) : 0.0f;
        }
    };
    constexpr float compAttack = 0.0003f, compRelease = 0.08f;   // seconds
}

//==============================================================================
namespace analysis
{
    inline void fft (std::vector<std::complex<float>>& a)
    {
        const size_t n = a.size();
        for (size_t i = 1, j = 0; i < n; ++i)
        {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap (a[i], a[j]);
        }
        for (size_t len = 2; len <= n; len <<= 1)
        {
            const float ang = -2.0f * 3.14159265358979f / (float) len;
            const std::complex<float> w (std::cos (ang), std::sin (ang));
            for (size_t i = 0; i < n; i += len)
            {
                std::complex<float> u (1.0f, 0.0f);
                for (size_t k = 0; k < len / 2; ++k)
                {
                    const auto x = a[i + k], y = a[i + k + len / 2] * u;
                    a[i + k] = x + y;
                    a[i + k + len / 2] = x - y;
                    u *= w;
                }
            }
        }
    }

    inline std::vector<float> mono (const Sample& s)
    {
        std::vector<float> m ((size_t) s.length());
        for (int i = 0; i < s.length(); ++i) m[(size_t) i] = 0.5f * (s.left[(size_t) i] + s.right[(size_t) i]);
        return m;
    }

    // Where the sound really begins and ends: drums recorded with silence before them start on time.
    inline std::pair<int, int> trim (const std::vector<float>& x, int from, int to, double sr)
    {
        from = std::clamp (from, 0, (int) x.size());
        to = std::clamp (to, from, (int) x.size());
        float peak = 0.0f;
        for (int i = from; i < to; ++i) peak = std::max (peak, std::abs (x[(size_t) i]));
        if (peak <= 1.0e-6f || to - from < 64) return { from, to };
        const float startLevel = peak * 0.0032f;   // -50 dB
        const float endLevel = peak * 0.0003f;     // -70 dB
        int a = from;
        while (a < to && std::abs (x[(size_t) a]) < startLevel) ++a;
        a = std::max (from, a - (int) (sr * 0.0005));
        int b = to;
        while (b > a + 1 && std::abs (x[(size_t) b - 1]) < endLevel) --b;
        b = std::min (to, b + (int) (sr * 0.005));
        return { a, std::max (a + 64, b) };
    }

    // Finds the hits inside a sample. sensitivity 0 … 100: higher finds more (and softer) hits.
    inline std::vector<int> hits (const std::vector<float>& x, double sr, float sensitivity = 50.0f, int maxCount = 64)
    {
        const int len = (int) x.size();
        std::vector<int> cuts { 0 };
        if (len < 2048) return cuts;
        const float s = std::clamp (sensitivity, 0.0f, 100.0f) * 0.01f;
        const int hop = std::max (64, (int) std::lround (sr * 0.0058));
        const int frame = hop * 2;
        const int frames = (len - frame) / hop;
        if (frames < 4) return cuts;

        std::vector<float> db ((size_t) frames);
        float loudest = -200.0f;
        for (int f = 0; f < frames; ++f)
        {
            double e = 0.0;
            const int a = f * hop;
            for (int i = a; i < a + frame; ++i)
            {
                const float d = x[(size_t) i] - (i > 0 ? 0.95f * x[(size_t) i - 1] : 0.0f);
                e += (double) d * d + 0.15 * (double) x[(size_t) i] * x[(size_t) i];
            }
            db[(size_t) f] = (float) (10.0 * std::log10 (e / frame + 1.0e-12));
            loudest = std::max (loudest, db[(size_t) f]);
        }
        std::vector<float> rise ((size_t) frames, 0.0f);
        for (int f = 2; f < frames; ++f) rise[(size_t) f] = db[(size_t) f] - 0.5f * (db[(size_t) f - 1] + db[(size_t) f - 2]);

        const float threshold = 2.5f + 13.0f * std::pow (1.0f - s, 1.5f);
        const float floorDb = loudest - (24.0f + 20.0f * s);
        const int minGap = std::max (2, (int) (sr * (0.09 - 0.05 * s) / hop));
        struct Onset { int frame; float strength; };
        std::vector<Onset> found;
        int last = -minGap;
        for (int f = 2; f < frames - 1; ++f)
        {
            const float r = rise[(size_t) f];
            if (r < threshold || db[(size_t) f] < floorDb) continue;
            if (r < rise[(size_t) f - 1] || r < rise[(size_t) f + 1]) continue;
            if (f - last < minGap)
            {
                if (! found.empty() && r > found.back().strength) { found.back() = { f, r }; last = f; }
                continue;
            }
            found.push_back ({ f, r });
            last = f;
        }
        if ((int) found.size() > maxCount - 1)
        {
            std::sort (found.begin(), found.end(), [] (const Onset& a, const Onset& b) { return a.strength > b.strength; });
            found.resize ((size_t) maxCount - 1);
            std::sort (found.begin(), found.end(), [] (const Onset& a, const Onset& b) { return a.frame < b.frame; });
        }
        for (const auto& o : found)
        {
            const int a = std::max (0, o.frame * hop - hop), b = std::min (len, o.frame * hop + frame);
            float peak = 0.0f;
            for (int i = a; i < b; ++i) peak = std::max (peak, std::abs (x[(size_t) i]));
            int pos = o.frame * hop;
            for (int i = a; i < b; ++i)
                if (std::abs (x[(size_t) i]) > 0.2f * peak) { pos = i; break; }
            pos = std::max (0, pos - (int) (sr * 0.001));
            if (pos > cuts.back() + (int) (sr * 0.03) && pos < len - (int) (sr * 0.02)) cuts.push_back (pos);
        }
        return cuts;
    }

    // Spectral centroid of the first part of the sound.
    inline float centroid (const std::vector<float>& x, int from, int to, double sr)
    {
        constexpr int n = 8192;
        std::vector<std::complex<float>> a ((size_t) n);
        const int len = std::min (n, to - from);
        if (len < 32) return 1000.0f;
        for (int i = 0; i < len; ++i)
        {
            const float w = 0.5f - 0.5f * std::cos (6.2831853f * (float) i / (float) len);
            a[(size_t) i] = x[(size_t) (from + i)] * w;
        }
        fft (a);
        double num = 0.0, den = 0.0;
        for (int k = 1; k < n / 2; ++k)
        {
            const double m = std::abs (a[(size_t) k]);
            num += m * k * sr / n;
            den += m;
        }
        return den > 1.0e-9 ? (float) (num / den) : 1000.0f;
    }

    // Pitch, by autocorrelation of the body of the sound (after its attack). 0 when unclear.
    inline float pitch (const std::vector<float>& x, int from, int to, double sr)
    {
        const int dec = std::max (1, (int) std::lround (sr / 12000.0));
        const double dsr = sr / dec;
        const int a = from + (int) (sr * 0.03), b = std::min (to, a + (int) (sr * 0.25));
        if (b - a < (int) (sr * 0.05)) return 0.0f;
        std::vector<float> y;
        for (int i = a; i + dec <= b; i += dec)
        {
            float s = 0.0f;
            for (int k = 0; k < dec; ++k) s += x[(size_t) (i + k)];
            y.push_back (s / dec);
        }
        const int n = (int) y.size();
        const int minLag = std::max (2, (int) (dsr / 1000.0)), maxLag = std::min (n / 2, (int) (dsr / 30.0));
        if (maxLag <= minLag + 2) return 0.0f;
        double e0 = 0.0;
        for (float v : y) e0 += (double) v * v;
        if (e0 < 1.0e-9) return 0.0f;
        std::vector<double> r ((size_t) maxLag + 2, 0.0);
        for (int lag = minLag - 1; lag <= maxLag + 1; ++lag)
        {
            double s = 0.0, ea = 0.0, eb = 0.0;
            for (int i = 0; i + lag < n; ++i) { s += (double) y[(size_t) i] * y[(size_t) (i + lag)]; ea += (double) y[(size_t) i] * y[(size_t) i]; eb += (double) y[(size_t) (i + lag)] * y[(size_t) (i + lag)]; }
            r[(size_t) lag] = s / std::sqrt (std::max (1.0e-12, ea * eb));
        }
        // the first strong peak (the shortest period), not merely the highest
        double best = 0.0;
        for (int lag = minLag; lag <= maxLag; ++lag) best = std::max (best, r[(size_t) lag]);
        if (best < 0.6) return 0.0f;
        for (int lag = minLag; lag <= maxLag; ++lag)
            if (r[(size_t) lag] >= 0.9 * best && r[(size_t) lag] >= r[(size_t) lag - 1] && r[(size_t) lag] >= r[(size_t) lag + 1])
            {
                const double y0 = r[(size_t) lag - 1], y1 = r[(size_t) lag], y2 = r[(size_t) lag + 1];
                const double den = y0 - 2.0 * y1 + y2;
                const double shift = std::abs (den) > 1.0e-12 ? 0.5 * (y0 - y2) / den : 0.0;
                return (float) (dsr / (lag + std::clamp (shift, -0.5, 0.5)));
            }
        return 0.0f;
    }

    // Loudness as we hear a drum: the loudest 30 ms of its envelope.
    inline float loudness (const std::vector<float>& e)
    {
        constexpr int w = 30;
        double sum = 0.0, best = 0.0;
        for (size_t i = 0; i < e.size(); ++i)
        {
            sum += (double) e[i] * e[i];
            if (i >= (size_t) w) sum -= (double) e[i - w] * e[i - w];
            best = std::max (best, sum / w);
        }
        return (float) std::sqrt (best);
    }

    // Builds a Sound for the part [from, to) of a sample.
    inline std::shared_ptr<Sound> makeSound (std::shared_ptr<const Sample> smp, const std::vector<float>& monoData, int from, int to)
    {
        auto s = std::make_shared<Sound>();
        s->sample = smp;
        const double sr = smp->sampleRate;
        s->regionStart = std::clamp (from, 0, smp->length());
        s->regionEnd = std::clamp (to, s->regionStart, smp->length());
        s->hop = std::max (8, (int) std::lround (sr * 0.001));
        const int len = s->regionLength();

        float peak = 0.0f;
        for (int i = s->regionStart; i < s->regionEnd; ++i) peak = std::max (peak, std::abs (monoData[(size_t) i]));
        s->peak = std::max (peak, 1.0e-6f);

        // envelope, one value per millisecond: the peak of the last 25 ms (long enough to hold
        // over a cycle of a deep kick, so it follows the decay and not each wave), never
        // looking ahead (so the attack stays where it is), then smoothed on the way down
        const int frames = std::max (1, (len + s->hop - 1) / s->hop);
        std::vector<float> blockMax ((size_t) frames, 0.0f), e ((size_t) frames);
        for (int f = 0; f < frames; ++f)
        {
            const int a = s->regionStart + f * s->hop, b = std::min (s->regionEnd, a + s->hop);
            float m = 0.0f;
            for (int i = a; i < b; ++i) m = std::max (m, std::abs (monoData[(size_t) i]));
            blockMax[(size_t) f] = m;
        }
        const int holdFrames = std::max (1, (int) std::lround (0.025 * sr / s->hop));
        const float fall = std::exp (-1.0f / 15.0f);   // 15 ms
        float smooth = 0.0f;
        for (int f = 0; f < frames; ++f)
        {
            float m = 0.0f;
            for (int k = std::max (0, f - holdFrames + 1); k <= f; ++k) m = std::max (m, blockMax[(size_t) k]);
            smooth = m >= smooth ? m : m + (smooth - m) * fall;
            e[(size_t) f] = smooth;
        }
        float top = 1.0e-9f;
        int topIndex = 0;
        for (int f = 0; f < frames; ++f) if (e[(size_t) f] > top) { top = e[(size_t) f]; topIndex = f; }
        for (auto& v : e) v = std::min (1.0f, v / top);
        s->env = std::move (e);
        s->peakIndex = topIndex;

        int end40 = frames - 1;
        for (int f = topIndex; f < frames; ++f) if (s->env[(size_t) f] < 0.01f) { end40 = f; break; }
        s->length = std::max (0.005f, (float) (end40 - topIndex + 1) * s->hop / (float) sr);
        s->centroid = centroid (monoData, s->regionStart, s->regionEnd, sr);
        s->pitch = pitch (monoData, s->regionStart, s->regionEnd, sr);

        // loudness compensation for the Shape knob (partial, so the knob still feels like an envelope)
        const float ref = std::max (1.0e-6f, loudness (s->env));
        std::vector<float> shaped (s->env.size());
        for (int k = 0; k < shapeSteps; ++k)
        {
            const float shape = -1.0f + 2.0f * (float) k / (shapeSteps - 1);
            for (size_t f = 0; f < shaped.size(); ++f)
                shaped[f] = s->env[f] * shaping::shapeGain (shape, (float) f * s->hop / (float) sr, s->env[f], (int) f < topIndex, s->length);
            const float l = std::max (1.0e-6f, loudness (shaped));
            const float db = std::clamp (20.0f * std::log10 (ref / l), -12.0f, 12.0f) * 0.75f;
            s->shapeComp[(size_t) k] = std::pow (10.0f, db / 20.0f);
        }

        // make-up gain for the compressor: the same loudness as without it
        const float peakDb = 20.0f * std::log10 (s->peak);
        const float dt = (float) s->hop / (float) sr;
        const float att = 1.0f - std::exp (-dt / shaping::compAttack), rel = 1.0f - std::exp (-dt / shaping::compRelease);
        for (int k = 0; k < compSteps; ++k)
        {
            shaping::Comp c;
            c.set ((float) k / (compSteps - 1), peakDb);
            float gr = 0.0f;
            for (size_t f = 0; f < shaped.size(); ++f)
            {
                const float levelDb = 20.0f * std::log10 (std::max (1.0e-7f, s->env[f] * s->peak));
                const float target = c.reductionDb (levelDb);
                gr += (target - gr) * (target < gr ? att : rel);
                shaped[f] = s->env[f] * std::pow (10.0f, gr / 20.0f);
            }
            const float l = std::max (1.0e-6f, loudness (shaped));
            s->compMakeup[(size_t) k] = std::pow (10.0f, std::clamp (20.0f * std::log10 (ref / l), 0.0f, 30.0f) / 20.0f);
        }
        return s;
    }

    // Linear interpolation in a table of evenly spaced steps over [lo, hi].
    template <size_t N>
    inline float lookup (const std::array<float, N>& table, float x, float lo, float hi) noexcept
    {
        const float p = std::clamp ((x - lo) / (hi - lo), 0.0f, 1.0f) * (float) (N - 1);
        const int i = std::min ((int) p, (int) N - 2);
        const float t = p - (float) i;
        return table[(size_t) i] + t * (table[(size_t) i + 1] - table[(size_t) i]);
    }
}

} // namespace kupata
