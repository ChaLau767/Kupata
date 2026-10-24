// Checks the engine and the analysis without JUCE. Run by the automatic build.

#include "KupataEngine.h"
#include <cstdio>
#include <random>

using namespace kupata;

static int failures = 0;
static void check (bool ok, const char* what, double a = 0.0, double b = 0.0)
{
    std::printf ("%s  %s  (%.3f, %.3f)\n", ok ? "ok  " : "FAIL", what, a, b);
    if (! ok) ++failures;
}

constexpr double SR = 48000.0;
constexpr double PI = 3.14159265358979;

static std::shared_ptr<Sample> makeKick (double seconds = 1.0, double f0 = 49.0, double tau = 0.25)
{
    auto s = std::make_shared<Sample>();
    s->sampleRate = SR;
    const int len = (int) (SR * seconds);
    s->left.resize ((size_t) len);
    double ph = 0.0;
    for (int i = 0; i < len; ++i)
    {
        const double t = i / SR;
        ph += 2.0 * PI * f0 * (1.0 + 0.4 * std::exp (-t / 0.015)) / SR;
        s->left[(size_t) i] = (float) (0.8 * std::exp (-t / tau) * std::sin (ph));
    }
    s->right = s->left;
    return s;
}

static std::shared_ptr<Sample> makeNoise (double seconds, double tau, unsigned seed = 3)
{
    auto s = std::make_shared<Sample>();
    s->sampleRate = SR;
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> d (-1.0f, 1.0f);
    const int len = (int) (SR * seconds);
    s->left.resize ((size_t) len);
    for (int i = 0; i < len; ++i) s->left[(size_t) i] = (float) (0.5 * std::exp (-(i / SR) / tau)) * d (rng);
    s->right = s->left;
    return s;
}

static std::shared_ptr<const Sound> soundOf (std::shared_ptr<Sample> smp)
{
    const auto m = analysis::mono (*smp);
    return analysis::makeSound (smp, m, 0, smp->length());
}

struct Out
{
    std::vector<std::vector<float>> ch;
    float peak (int c, int from = 0, int to = -1) const
    {
        float p = 0.0f;
        const auto& x = ch[(size_t) c];
        if (to < 0) to = (int) x.size();
        for (int i = from; i < to && i < (int) x.size(); ++i) p = std::max (p, std::abs (x[(size_t) i]));
        return p;
    }
    double rms (int c, int from, int to) const
    {
        double s = 0.0;
        const auto& x = ch[(size_t) c];
        for (int i = from; i < to; ++i) s += (double) x[(size_t) i] * x[(size_t) i];
        return std::sqrt (s / std::max (1, to - from));
    }
    bool finite() const
    {
        for (const auto& c : ch) for (float v : c) if (! std::isfinite (v)) return false;
        return true;
    }
};

// Renders `frames` samples, with events at the given block offsets of the first block.
static Out render (Engine& e, const Params& p, int frames, std::vector<Event> ev = {}, int numOut = 2, int block = 512)
{
    Out o;
    o.ch.assign ((size_t) numOut, std::vector<float> ((size_t) frames, 0.0f));
    std::vector<float*> ptr ((size_t) numOut);
    for (int done = 0; done < frames; done += block)
    {
        const int n = std::min (block, frames - done);
        std::vector<Event> here;
        for (const auto& x : ev)
            if (x.offset >= done && x.offset < done + n) { auto y = x; y.offset -= done; here.push_back (y); }
        for (int c = 0; c < numOut; ++c) ptr[(size_t) c] = o.ch[(size_t) c].data() + done;
        e.process (ptr.data(), numOut, n, p, here.data(), (int) here.size());
    }
    return o;
}

static Event on (int note, int offset = 0, float vel = 1.0f) { Event e; e.type = Event::noteOn; e.note = note; e.offset = offset; e.velocity = vel; return e; }
static Event off (int note, int offset) { Event e; e.type = Event::noteOff; e.note = note; e.offset = offset; return e; }

static double db (double x) { return 20.0 * std::log10 (std::max (1.0e-12, x)); }

// time (in seconds) until the output falls below `below` dB of its peak, for good
static double lengthOf (const Out& o, double below = -40.0)
{
    const float pk = o.peak (0);
    const auto& x = o.ch[0];
    int last = 0;
    for (int i = 0; i < (int) x.size(); ++i) if (std::abs (x[(size_t) i]) > pk * std::pow (10.0, below / 20.0)) last = i;
    return last / SR;
}

static double loud30 (const Out& o)   // the loudest 30 ms
{
    const int w = (int) (SR * 0.03);
    double best = 0.0;
    for (int a = 0; a + w < (int) o.ch[0].size(); a += w / 4) best = std::max (best, o.rms (0, a, a + w));
    return best;
}

int main()
{
    // --- analysis -----------------------------------------------------------
    {
        auto kick = soundOf (makeKick());
        check (std::abs (kick->pitch - 49.0f) < 3.0f, "pitch of a 49 Hz kick", kick->pitch, 49.0);
        check (kick->centroid < 300.0f, "a kick is dark", kick->centroid, 300.0);
        check (kick->length > 0.5f && kick->length < 1.3f, "kick length to -40 dB", kick->length, 0.0);
        auto hat = soundOf (makeNoise (0.3, 0.03));
        check (hat->pitch == 0.0f, "noise has no pitch", hat->pitch, 0.0);
        check (hat->centroid > 5000.0f, "noise is bright", hat->centroid, 5000.0);
        float minComp = 10.0f, maxComp = 0.0f;
        for (float g : kick->shapeComp) { minComp = std::min (minComp, g); maxComp = std::max (maxComp, g); }
        check (minComp > 0.2f && maxComp < 5.0f && std::abs (kick->shapeComp[shapeSteps / 2] - 1.0f) < 0.01f, "shape compensation is moderate and 1 at the centre", minComp, maxComp);
        check (kick->compMakeup[0] > 0.99f && kick->compMakeup[0] < 1.01f && kick->compMakeup[compSteps - 1] > 1.5f, "compressor make-up grows", kick->compMakeup[0], kick->compMakeup[compSteps - 1]);

        // a loop of four hits
        auto loop = std::make_shared<Sample>();
        loop->sampleRate = SR;
        loop->left.assign ((size_t) (SR * 2.0), 0.0f);
        for (int h = 0; h < 4; ++h)
        {
            auto k = makeKick (0.3, 60.0 + 30.0 * h, 0.05);
            for (int i = 0; i < k->length(); ++i) loop->left[(size_t) (h * SR * 0.5 + i)] = k->left[(size_t) i];
        }
        loop->right = loop->left;
        const auto hits = analysis::hits (analysis::mono (*loop), SR);
        check (hits.size() == 4, "four hits found in a loop", (double) hits.size(), 4.0);

        // silence before a sound is skipped
        auto late = std::make_shared<Sample>();
        late->sampleRate = SR;
        late->left.assign ((size_t) (SR * 0.2), 0.0f);
        auto k = makeKick (0.5);
        late->left.insert (late->left.end(), k->left.begin(), k->left.end());
        late->left.insert (late->left.end(), (size_t) (SR * 0.3), 0.0f);
        late->right = late->left;
        const auto [a, b] = analysis::trim (analysis::mono (*late), 0, late->length(), SR);
        check (std::abs (a - (int) (SR * 0.2)) < (int) (SR * 0.002) && b < late->length() - (int) (SR * 0.2), "silence trimmed at both ends", a / SR, b / SR);
    }

    Engine e;
    e.prepare (SR);
    e.sounds[0] = soundOf (makeKick());
    e.sounds[1] = soundOf (makeNoise (0.4, 0.15, 5));     // an "open hat"
    e.sounds[2] = soundOf (makeNoise (0.05, 0.01, 7));    // a "closed hat"
    Params p;
    p.velocity = 0.0f;   // full level whatever the velocity

    // --- plain playback ------------------------------------------------------
    const Out plain = render (e, p, (int) SR, { on (36) });
    check (plain.finite() && std::abs (db (plain.peak (0)) - db (0.8)) < 1.0, "pad 1 plays at its own level", db (plain.peak (0)), db (0.8));
    check (plain.peak (0) == plain.peak (1), "centred: both channels equal", plain.peak (0), plain.peak (1));
    const double plainLen = lengthOf (plain);

    // --- Shape -----------------------------------------------------------------
    {
        Params q = p; q.pads[0].shape = -100.0f;
        e.reset();
        const Out s = render (e, q, (int) SR, { on (36) });
        check (lengthOf (s) < 0.06, "Shape fully left: a click", lengthOf (s), plainLen);
        check (std::abs (db (loud30 (s) / loud30 (plain))) < 6.0, "Shape left: loudness kept near", db (loud30 (s) / loud30 (plain)));
        q.pads[0].shape = -50.0f;
        e.reset();
        const Out h = render (e, q, (int) SR, { on (36) });
        check (lengthOf (h) < plainLen * 0.6 && lengthOf (h) > lengthOf (s), "Shape half left: shorter", lengthOf (h), plainLen);
        q.pads[0].shape = 100.0f;
        e.reset();
        const Out l = render (e, q, (int) SR, { on (36) });
        const int t = (int) (SR * 0.5);
        const double tailGain = db (l.rms (0, t, t + 2400) / l.peak (0)) - db (plain.rms (0, t, t + 2400) / plain.peak (0));
        check (tailGain > 10.0, "Shape fully right: the tail comes up", tailGain);
        check (l.peak (0, 0, (int) (SR * 0.005)) < 0.5f * plain.peak (0, 0, (int) (SR * 0.005)), "Shape fully right: softer attack",
               l.peak (0, 0, (int) (SR * 0.005)), plain.peak (0, 0, (int) (SR * 0.005)));
    }

    // --- Compression -----------------------------------------------------------
    {
        Params q = p; q.pads[0].comp = 100.0f;
        e.reset();
        const Out c = render (e, q, (int) SR, { on (36) });
        const int t = (int) (SR * 0.3);
        const double body = db (c.rms (0, t, t + 4800) / c.peak (0)) - db (plain.rms (0, t, t + 4800) / plain.peak (0));
        check (body > 6.0, "Compression: more body relative to the attack", body);
        check (std::abs (db (loud30 (c) / loud30 (plain))) < 3.0, "Compression: loudness kept", db (loud30 (c) / loud30 (plain)));
        check (c.peak (0) < 1.6f, "Compression: no runaway peak", c.peak (0));
    }

    // --- Tone ----------------------------------------------------------------------
    {
        auto hf = [] (const Out& o) { double s = 0.0; const auto& x = o.ch[0]; for (size_t i = 1; i < x.size(); ++i) { const double d = x[i] - x[i - 1]; s += d * d; } return s; };
        auto lf = [] (const Out& o) { double s = 0.0, y = 0.0; for (float v : o.ch[0]) { y += 0.01 * (v - y); s += y * y; } return s; };
        e.reset();
        const Out flat = render (e, p, (int) (SR * 0.5), { on (37) });
        Params q = p; q.pads[1].tone = -100.0f;
        e.reset();
        const Out dark = render (e, q, (int) (SR * 0.5), { on (37) });
        q.pads[1].tone = 100.0f;
        e.reset();
        const Out bright = render (e, q, (int) (SR * 0.5), { on (37) });
        check (hf (dark) < 0.05 * hf (flat), "Tone left: darker", db (hf (dark) / hf (flat)) / 2.0);
        check (lf (bright) < 0.3 * lf (flat), "Tone right: lows gone", db (lf (bright) / lf (flat)) / 2.0);
        check (dark.finite() && bright.finite(), "Tone: finite");
    }

    // --- Tune, Volume, Pan, trims ---------------------------------------------------
    {
        Params q = p; q.pads[0].tune = 12.0f;
        e.reset();
        const Out up = render (e, q, (int) SR, { on (36) });
        check (std::abs (lengthOf (up) / plainLen - 0.5) < 0.05, "Tune +12: half as long", lengthOf (up), plainLen);
        q = p; q.pads[0].volume = -12.0f; q.pads[0].pan = 100.0f;
        e.reset();
        const Out v = render (e, q, (int) (SR * 0.3), { on (36) });
        check (std::abs (db (v.peak (1)) - (db (plain.peak (1)) - 12.0)) < 0.5 && v.peak (0) < 1.0e-4f, "Volume -12 dB, hard right", db (v.peak (1)), db (v.peak (0)));
        q = p; q.pads[0].start = 50.0f;
        e.reset();
        const Out late = render (e, q, (int) SR, { on (36) });
        check (late.peak (0) < 0.2f * plain.peak (0), "Start at 50 %: begins in the tail", late.peak (0), plain.peak (0));
        q = p; q.pads[0].end = 10.0f;
        e.reset();
        const Out early = render (e, q, (int) SR, { on (36) });
        check (lengthOf (early, -80.0) < 0.105, "End at 10 %: stops early", lengthOf (early, -80.0));
    }

    // --- velocity -------------------------------------------------------------
    {
        Params q = p; q.velocity = 100.0f;
        e.reset();
        const Out soft = render (e, q, (int) (SR * 0.2), { on (36, 0, 0.25f) });
        check (db (soft.peak (0)) < db (plain.peak (0)) - 12.0, "a soft note plays softer", db (soft.peak (0)), db (plain.peak (0)));
    }

    // --- choke, gate, mute, solo -------------------------------------------------
    {
        e.routing[1].choke = 1;
        e.routing[2].choke = 1;
        e.reset();
        const int hit = (int) (SR * 0.1);
        const Out ch = render (e, p, (int) (SR * 0.3), { on (37), on (38, hit) });
        Params q = p;
        e.reset();
        const Out alone = render (e, q, (int) (SR * 0.3), { on (37) });
        const int after = hit + (int) (SR * 0.06);
        check (ch.rms (0, after, after + 2400) < 0.05 * alone.rms (0, after, after + 2400), "choke: the closed hat cuts the open one",
               ch.rms (0, after, after + 2400), alone.rms (0, after, after + 2400));
        e.routing[1].choke = 0; e.routing[2].choke = 0;

        q = p; q.pads[0].gate = true;
        e.reset();
        const Out g = render (e, q, (int) SR, { on (36), off (36, (int) (SR * 0.1)) });
        check (g.peak (0, (int) (SR * 0.15), (int) SR) < 1.0e-4f, "gate: note-off stops the pad", g.peak (0, (int) (SR * 0.15), (int) SR));
        q = p;
        e.reset();
        const Out ng = render (e, q, (int) SR, { on (36), off (36, (int) (SR * 0.1)) });
        check (ng.peak (0, (int) (SR * 0.15), (int) SR) > 0.05f, "without gate the pad rings on", ng.peak (0, (int) (SR * 0.15), (int) SR));

        q = p; q.pads[0].mute = true;
        e.reset();
        const Out m = render (e, q, (int) (SR * 0.3), { on (36) });
        check (m.peak (0, 512) < 1.0e-3f, "mute", m.peak (0, 512));
        q = p; q.pads[1].solo = true;
        e.reset();
        const Out so = render (e, q, (int) (SR * 0.3), { on (36), on (37) });
        e.reset();
        const Out hatOnly = render (e, p, (int) (SR * 0.3), { on (37) });
        check (std::abs (so.rms (0, 4800, 9600) - hatOnly.rms (0, 4800, 9600)) < 0.02 * hatOnly.rms (0, 4800, 9600) + 1.0e-4, "solo: only the soloed pad sounds");
    }

    // --- outputs ---------------------------------------------------------------------
    {
        e.routing[0].out = 2;
        e.reset();
        const Out o = render (e, p, (int) (SR * 0.2), { on (36) }, 4);
        check (o.peak (2) > 0.5f && o.peak (3) > 0.5f && o.peak (0) < 1.0e-6f, "pad 1 on outputs 3-4", o.peak (2), o.peak (0));
        e.routing[0].stereo = false; e.routing[0].out = 3;
        e.reset();
        const Out m = render (e, p, (int) (SR * 0.2), { on (36) }, 4);
        check (m.peak (3) > 0.5f && m.peak (2) < 1.0e-6f, "pad 1 on output 4 alone", m.peak (3), m.peak (2));
        e.routing[0].out = 10;
        e.reset();
        const Out fb = render (e, p, (int) (SR * 0.2), { on (36) }, 4);
        check (fb.peak (3) < 1.0e-6f && fb.peak (0) > 0.5f, "a missing output falls back to 1-2", fb.peak (0));
        e.routing[0].out = 0; e.routing[0].stereo = true;
    }

    // --- many hits, any block size, offline picture ---------------------------------
    {
        std::vector<Event> ev;
        for (int k = 0; k < 200; ++k) ev.push_back (on (36 + k % 3, k * 240, 0.5f + 0.5f * (k % 2)));
        e.reset();
        const Out busy = render (e, p, (int) (SR * 2.0), ev, 2, 97);
        check (busy.finite() && busy.peak (0) < 8.0f, "200 fast hits: finite, bounded", busy.peak (0));
        std::vector<float> pic;
        PadParams pp; pp.shape = -40.0f;
        Engine::renderOffline (*e.sounds[0], pp, SR, (int) SR, pic);
        check (! pic.empty() && (int) pic.size() <= (int) SR, "offline picture of a pad", (double) pic.size());
    }

    std::printf (failures == 0 ? "\nall engine tests passed\n" : "\n%d engine tests failed\n", failures);
    return failures == 0 ? 0 : 1;
}
