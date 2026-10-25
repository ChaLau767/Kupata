// Development tool: opens the interface with the factory kit, plays a few pads and saves a PNG.
// "--play" tests playback as a host drives it; "--state" checks that sessions and kits restore.
// "--demo out.wav" renders a short pattern with the factory kit.
// Only built with -DKUPATA_SNAPSHOT=ON.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdio>

static int failures = 0;
static void check (bool ok, const juce::String& what)
{
    std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
    if (! ok) ++failures;
}

static juce::File writeTestFile (const juce::String& name, int hits, double seconds)
{
    const double sr = 44100.0;
    const int len = (int) (sr * seconds);
    juce::AudioBuffer<float> buf (2, len);
    buf.clear();
    for (int h = 0; h < hits; ++h)
    {
        const int a = h * len / hits;
        for (int i = 0; i < len / hits && a + i < len; ++i)
        {
            const double t = i / sr;
            const float v = (float) (0.8 * std::exp (-t * 12.0) * std::sin (2.0 * juce::MathConstants<double>::pi * (70.0 + 50.0 * h) * t));
            buf.setSample (0, a + i, v);
            buf.setSample (1, a + i, v * 0.9f);
        }
    }
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (name);
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), sr, 2, 24, {}, 0));
    if (w != nullptr) w->writeFromAudioSampleBuffer (buf, 0, len);
    return f;
}

//==============================================================================
// A small imitation of REAPER's extension API: a project with tracks, names, channels and sends.
JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wshadow", "-Wnon-virtual-dtor", "-Wshadow-field-in-constructor", "-Wzero-as-null-pointer-constant", "-Wunused-parameter")
#include <pluginterfaces/base/funknown.h>
namespace fake
{
    struct Send { int src = 0, dst = 0, mode = 0, midi = 0; void* dest = nullptr; };
    struct Track { std::string name, mark; int nchan = 2, folder = 0; bool mainSend = true; std::vector<Send> sends; };
    std::vector<std::unique_ptr<Track>> tracks;
    int undoBlocks = 0;

    int indexOf (void* t) { for (size_t i = 0; i < tracks.size(); ++i) if (tracks[i].get() == t) return (int) i; return -1; }
    Track& T (void* t) { return *static_cast<Track*> (t); }

    double getVal (void* t, const char* p)
    {
        const std::string k (p);
        if (k == "I_NCHAN") return T (t).nchan;
        if (k == "I_FOLDERDEPTH") return T (t).folder;
        if (k == "B_MAINSEND") return T (t).mainSend ? 1 : 0;
        if (k == "IP_TRACKNUMBER") return indexOf (t) + 1;
        return 0;
    }
    bool setVal (void* t, const char* p, double v)
    {
        const std::string k (p);
        if (k == "I_NCHAN") T (t).nchan = (int) v;
        else if (k == "I_FOLDERDEPTH") T (t).folder = (int) v;
        else if (k == "B_MAINSEND") T (t).mainSend = v != 0;
        return true;
    }
    bool str (void* t, const char* p, char* buf, bool set)
    {
        std::string& f = std::string (p) == "P_NAME" ? T (t).name : T (t).mark;
        if (set) f = buf; else std::snprintf (buf, 1024, "%s", f.c_str());
        return true;
    }
    int numSends (void* t, int cat) { return cat == 0 ? (int) T (t).sends.size() : 0; }
    double sendVal (void* t, int, int i, const char* p)
    {
        const auto& s = T (t).sends[(size_t) i];
        const std::string k (p);
        return k == "I_SRCCHAN" ? s.src : k == "I_DSTCHAN" ? s.dst : k == "I_SENDMODE" ? s.mode : k == "I_MIDIFLAGS" ? s.midi : 0;
    }
    bool setSendVal (void* t, int, int i, const char* p, double v)
    {
        auto& s = T (t).sends[(size_t) i];
        const std::string k (p);
        if (k == "I_SRCCHAN") s.src = (int) v; else if (k == "I_DSTCHAN") s.dst = (int) v;
        else if (k == "I_SENDMODE") s.mode = (int) v; else if (k == "I_MIDIFLAGS") s.midi = (int) v;
        return true;
    }
    void* sendInfo (void* t, int, int i, const char* p, void*) { return std::string (p) == "P_DESTTRACK" ? T (t).sends[(size_t) i].dest : nullptr; }
    void insert (int idx, bool) { tracks.insert (tracks.begin() + idx, std::make_unique<Track>()); }
    void* getTrack (void*, int i) { return i >= 0 && i < (int) tracks.size() ? tracks[(size_t) i].get() : nullptr; }
    int createSend (void* t, void* d) { T (t).sends.push_back ({ 0, 0, 0, 0, d }); return (int) T (t).sends.size() - 1; }
    void undoBegin (void*) { ++undoBlocks; }
    void undoEnd (void*, const char*, int) {}
    void adjust (bool) {}
    void update() {}

    struct Host : public Steinberg::FUnknown
    {
        void* parent = nullptr;
        Steinberg::tresult PLUGIN_API queryInterface (const Steinberg::TUID iid, void** obj) override
        {
            const Steinberg::TUID reaper = INLINE_UID (0x79655E36, 0x77EE4267, 0xA573FEF7, 0x4912C27C);
            if (std::memcmp (iid, reaper, sizeof (Steinberg::TUID)) == 0) { *obj = this; return Steinberg::kResultOk; }
            *obj = nullptr; return Steinberg::kNoInterface;
        }
        Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
        Steinberg::uint32 PLUGIN_API release() override { return 1; }
        virtual void* PLUGIN_API getReaperApi (const char* n)
        {
            const std::string k (n);
            if (k == "GetMediaTrackInfo_Value") return (void*) &getVal;
            if (k == "SetMediaTrackInfo_Value") return (void*) &setVal;
            if (k == "GetSetMediaTrackInfo_String") return (void*) &str;
            if (k == "GetTrackNumSends") return (void*) &numSends;
            if (k == "GetTrackSendInfo_Value") return (void*) &sendVal;
            if (k == "SetTrackSendInfo_Value") return (void*) &setSendVal;
            if (k == "GetSetTrackSendInfo") return (void*) &sendInfo;
            if (k == "InsertTrackAtIndex") return (void*) &insert;
            if (k == "GetTrack") return (void*) &getTrack;
            if (k == "CreateTrackSend") return (void*) &createSend;
            if (k == "Undo_BeginBlock2") return (void*) &undoBegin;
            if (k == "Undo_EndBlock2") return (void*) &undoEnd;
            if (k == "TrackList_AdjustWindows") return (void*) &adjust;
            if (k == "UpdateArrange") return (void*) &update;
            return nullptr;
        }
        virtual void* PLUGIN_API getReaperParent (Steinberg::uint32 w) { return w == 1 ? parent : nullptr; }
        virtual void* PLUGIN_API reaperExtended (Steinberg::uint32, void*, void*, void*) { return nullptr; }
    };
}
JUCE_END_IGNORE_WARNINGS_GCC_LIKE

static void wait (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

static void run (KupataProcessor& proc, int blocks, std::vector<std::pair<int, int>> notes = {})
{
    juce::AudioBuffer<float> buf (2, 512);
    for (int b = 0; b < blocks; ++b)
    {
        buf.clear();
        juce::MidiBuffer midi;
        for (auto [block, note] : notes) if (block == b) midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 110), 0);
        proc.processBlock (buf, midi);
        if (b % 4 == 0) juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::String mode = argc > 1 ? juce::String (argv[1]) : juce::String();

    if (mode == "--demo")
    {
        // two bars at 112 BPM: kick, snare, hats, clap, cowbell, low conga and rim
        KupataProcessor proc;
        proc.setPlayConfigDetails (0, 2, 48000.0, 256);
        proc.prepareToPlay (48000.0, 256);
        const double sr = 48000.0, step = 60.0 / 112.0 / 4.0;
        const char* pattern[] {
            "x.......x.x.....x.......x...x...",   // pad 1 kick
            "....x.......x.......x.......x..x",   // pad 2 snare
            "x.x.x.x.x.x.x.xxx.x.x.x.x.x.x.x.",   // pad 3 closed hat
            "..............x...............x.",   // pad 4 open hat
            "............x...............x...",   // pad 12 clap
            "......x.........x.....x.........",   // pad 15 cowbell
            "x.....x.....x.x.....x.....x.x...",   // pad 9 low conga
            "...x......x.........x.....x.....",   // pad 16 rim
        };
        const int notes[] { 36, 38, 42, 46, 39, 56, 64, 37 };   // the kit's own notes (General MIDI, as in Sitala)
        proc.setParam ("gain", -7.0f);
        proc.setParam ("p15_volume", -8.0f);
        proc.setParam ("p9_volume", -4.0f);
        proc.setParam ("p3_volume", -4.0f);
        const int total = (int) (sr * step * 32 * 2) + (int) sr;
        juce::AudioBuffer<float> out (2, total);
        out.clear();
        juce::AudioBuffer<float> buf (2, 256);
        for (int pos = 0; pos < total; pos += 256)
        {
            buf.clear();
            juce::MidiBuffer midi;
            for (int rep = 0; rep < 2; ++rep)
                for (int s = 0; s < 32; ++s)
                {
                    const int at = (int) ((rep * 32 + s) * step * sr);
                    if (at < pos || at >= pos + 256) continue;
                    for (int k = 0; k < 8; ++k)
                        if (pattern[k][s] == 'x') midi.addEvent (juce::MidiMessage::noteOn (1, notes[k], (juce::uint8) (s % 4 == 0 ? 120 : 92)), at - pos);
                }
            proc.processBlock (buf, midi);
            const int n = juce::jmin (256, total - pos);
            for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, buf, ch, 0, n);
        }
        juce::File f = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 2 ? argv[2] : "kupata-demo.wav");
        f.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), sr, 2, 24, {}, 0));
        if (w != nullptr) w->writeFromAudioSampleBuffer (out, 0, total);
        std::printf ("demo written: %s (peak %.2f)\n", f.getFullPathName().toRawUTF8(), (double) out.getMagnitude (0, total));
        return 0;
    }

    if (mode == "--state")
    {
        KupataProcessor a;
        a.setPlayConfigDetails (0, 2, 44100.0, 512);
        a.prepareToPlay (44100.0, 512);
        juce::String error;
        const bool loaded = a.loadFile (4, writeTestFile ("kupata-state.wav", 4, 2.0), error);
        check (loaded && a.padInfo (4).numHits == 4, "a file with four hits loads on pad 5 (" + juce::String (a.padInfo (4).numHits) + " hits)");
        a.setHit (4, 3);
        a.setParam ("p5_shape", -40.0f);
        a.setParam ("p5_tune", 5.5f);
        a.setParam ("p2_comp", 60.0f);
        auto r = a.padInfo (4).routing;
        r.note = 60; r.out = 5; r.stereo = false; r.choke = 3;
        a.setRouting (4, r);
        a.clearPad (9);
        a.loadFactory (8, 15);
        juce::MemoryBlock block;
        a.getStateInformation (block);
        check (block.getSize() > 1000, "the session saves (" + juce::String ((int) block.getSize()) + " bytes)");

        KupataProcessor b;
        b.setStateInformation (block.getData(), (int) block.getSize());
        const auto info = b.padInfo (4);
        check (info.loaded() && info.hit == 3 && info.numHits == 4, "hit 3 of pad 5 restored");
        check (info.routing.note == 60 && info.routing.out == 5 && ! info.routing.stereo && info.routing.choke == 3, "routing of pad 5 restored");
        check (std::abs (b.padParams (4).shape + 40.0f) < 0.01f && std::abs (b.padParams (4).tune - 5.5f) < 0.01f && std::abs (b.padParams (1).comp - 60.0f) < 0.01f,
               "knobs restored");
        check (b.padInfo (0).factory == 0 && b.padInfo (0).loaded(), "factory pads restored");
        check (! b.padInfo (9).loaded(), "the cleared pad stays empty");
        check (b.padInfo (2).routing.choke == 1 && b.padInfo (3).routing.choke == 1, "the hats keep their choke group");
        check (b.padParams (0).mono && ! b.padParams (4).mono, "pads cut themselves, except the cymbal");
        check (b.padInfo (8).routing.note == 64 && b.padInfo (11).routing.note == 39, "the kit's notes are Sitala's (low conga 64, clap 39)");
        check (b.padInfo (8).factory == 15 && b.padInfo (8).loaded(), "a factory sound outside the kit (the sub kick) is restored");

        // the sound travels inside the session: it plays even when the file is gone
        writeTestFile ("kupata-state.wav", 1, 0.1).deleteFile();
        KupataProcessor c;
        c.setStateInformation (block.getData(), (int) block.getSize());
        check (c.padInfo (4).loaded() && ! c.padInfo (4).missing, "the sound comes back from the session without its file");

        // kits
        auto kitFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("kupata-test.kupata");
        check (b.saveKit (kitFile, "Prueba"), "a kit saves");
        KupataProcessor d;
        check (d.loadKit (kitFile), "the kit loads");
        check (d.getKitName() == "Prueba" && d.padInfo (4).hit == 3 && std::abs (d.padParams (4).shape + 40.0f) < 0.01f
               && ! d.padInfo (9).loaded(), "the kit brings its sounds and knobs");

        // moving a pad takes its knobs; hits to pads
        d.movePad (4, 11, false);
        check (d.padInfo (11).loaded() && std::abs (d.padParams (11).shape + 40.0f) < 0.01f && d.padInfo (4).loaded() && d.padInfo (4).factory == kupata::factoryLayout (11),
               "a pad moves with its knobs and the other comes back");
        d.setHit (11, 0);
        check (d.hitsToPads (11) == 4 && d.padInfo (14).hit == 4, "hits to pads: four pads filled");
        // bank B, reverse and the note maps travel with the session
        {
            KupataProcessor e;
            juce::String err;
            check (e.currentNoteMap() == kupata::mapGeneralMidi && e.padInfo (16).routing.note == 72, "the kit starts with Sitala's notes; bank B from C5");
            check (! e.bankInUse (1) && e.loadFile (20, writeTestFile ("kupata-bankb.wav", 1, 0.5), err) && e.bankInUse (1), "a sound on pad 21, in bank B");
            e.setParam ("p21_reverse", 1.0f);
            e.setNotePreset (kupata::mapChromatic, 48);
            check (e.currentNoteMap() == -1 && e.padInfo (0).routing.note == 48 && e.padInfo (31).routing.note == 79, "in a row from C3: pads 1-32 on 48-79");
            juce::MemoryBlock blockB;
            e.getStateInformation (blockB);
            KupataProcessor f;
            f.setStateInformation (blockB.getData(), (int) blockB.getSize());
            check (f.padInfo (20).loaded() && f.padParams (20).reverse && f.padInfo (20).routing.note == 68, "bank B, reverse and notes restored");
            std::vector<float> fwd, rev;
            f.renderPicture (20, rev, 48000);
            f.setParam ("p21_reverse", 0.0f);
            f.renderPicture (20, fwd, 48000);
            auto rmsOf = [] (const std::vector<float>& x, size_t a, size_t b) { double s2 = 0; for (size_t i = a; i < b && i < x.size(); ++i) s2 += x[i] * x[i]; return std::sqrt (s2 / std::max<size_t> (1, b - a)); };
            check (! rev.empty() && rmsOf (rev, 0, 2000) < 0.2 * rmsOf (fwd, 0, 2000), "the reversed pad starts quietly, where the sound ends");
        }
        std::printf (failures == 0 ? "\nall state tests passed\n" : "\n%d state tests failed\n", failures);
        return failures == 0 ? 0 : 1;
    }

    if (mode == "--reaper")
    {
        // 1. Kupata builds the routing itself: a folder with one track per pad
        fake::tracks.clear();
        fake::insert (0, true); fake::tracks[0]->name = "Bass";
        fake::insert (1, true); fake::tracks[1]->name = "Drums";
        fake::insert (2, true); fake::tracks[2]->name = "Vox";
        fake::Host host;
        host.parent = fake::tracks[1].get();
        auto* drums = fake::tracks[1].get();
        {
            KupataProcessor proc;
            proc.setPlayConfigDetails (0, 32, 48000.0, 512);
            proc.prepareToPlay (48000.0, 512);
            check (! proc.reaperAvailable(), "without REAPER the link is off");
            proc.getVST3ClientExtensions()->setIHostApplication (&host);
            check (proc.reaperAvailable(), "REAPER's API is found");
            wait (1500);
            check (proc.numOutputs() == 2, "the track's 2 channels count, not the plugin's 32 (" + juce::String (proc.numOutputs()) + ")");
            const int made = proc.buildReaperTracks();
            check (made == 16 && fake::tracks.size() == 19, "16 tracks created (" + juce::String (made) + ")");
            check (fake::tracks[2]->name == "Kick long" && fake::tracks[17]->name == "Rimshot" && fake::tracks[18]->name == "Vox",
                   "named after the pads, right under Kupata's track");
            check (drums->nchan == 32 && ! drums->mainSend && drums->folder == 1 && fake::tracks[17]->folder == -1, "32 channels, a folder, no double sound");
            bool sends = drums->sends.size() == 16;
            for (int k = 0; k < 16 && sends; ++k)
                sends = drums->sends[(size_t) k].src == k * 2 && drums->sends[(size_t) k].dest == fake::tracks[(size_t) k + 2].get() && drums->sends[(size_t) k].midi == 31;
            check (sends, "track k receives channels 2k-1, 2k, audio only");
            check (proc.padInfo (4).routing.out == 8 && proc.padInfo (15).routing.out == 30, "each pad on its own pair");
            check (fake::undoBlocks == 1, "one undo step");
            check (proc.buildReaperTracks() == 0, "it is not built twice");

            // the names follow the pads, unless the person renamed the track
            fake::tracks[5]->name = "Mi tambor";   // pad 4
            proc.loadFactory (2, 4);                // pad 3 now holds the clap
            proc.loadFactory (3, 5);                // pad 4 the rimshot
            wait (2000);
            check (fake::tracks[4]->name == "Clap", "a pad's new sound renames its track (" + juce::String (fake::tracks[4]->name) + ")");
            check (fake::tracks[5]->name == "Mi tambor", "a track the person renamed keeps its name");
            proc.setLanguage (kupata::Lang::es);
            wait (2000);
            check (fake::tracks[2]->name == "Bombo largo", "the names follow the interface's language");
        }

        // 2. REAPER built the routing (its dialog): Kupata notices, spreads the pads and names the tracks
        fake::tracks.clear();
        fake::insert (0, true);
        fake::tracks[0]->name = "Kupata";
        fake::tracks[0]->nchan = 32;
        for (int k = 0; k < 16; ++k)
        {
            fake::insert (k + 1, true);
            fake::tracks[(size_t) k + 1]->name = (k == 15 ? "Reverb" : "Kupata " + std::to_string (2 * k + 1) + "/" + std::to_string (2 * k + 2));
            fake::createSend (fake::tracks[0].get(), fake::tracks[(size_t) k + 1].get());
            fake::tracks[0]->sends.back().src = 2 * k;
        }
        host.parent = fake::tracks[0].get();
        {
            KupataProcessor proc;
            proc.setPlayConfigDetails (0, 32, 48000.0, 512);
            proc.getVST3ClientExtensions()->setIHostApplication (&host);
            wait (2000);
            check (proc.reaperRouted() && proc.padInfo (6).routing.out == 12, "REAPER's own routing turns multi-out on");
            check (fake::tracks[1]->name == "Kick long" && fake::tracks[7]->name == "Mid tom" && fake::tracks[10]->name == "Mid conga",
                   "REAPER's tracks take the pads' names");
            check (fake::tracks[16]->name == "Reverb", "a track with a name of its own is left alone");
            proc.setMultiOut (false);
            wait (2000);
            check (proc.padInfo (6).routing.out == 0, "turning multi-out off sticks (it is only done once)");
        }
        // 3. with bank B in use, a track for each of the 32 pads, on 64 channels
        fake::tracks.clear();
        fake::insert (0, true);
        host.parent = fake::tracks[0].get();
        {
            KupataProcessor proc;
            proc.setPlayConfigDetails (0, 64, 48000.0, 512);
            proc.getVST3ClientExtensions()->setIHostApplication (&host);
            proc.loadFactory (17, 16);   // a low conga on pad 18
            check (proc.buildReaperTracks() == 32 && fake::tracks[0]->nchan == 64 && fake::tracks[18]->name == "Low conga"
                   && proc.padInfo (17).routing.out == 34, "bank B in use: 32 tracks on 64 channels");
        }
        std::printf (failures == 0 ? "\nall REAPER tests passed\n" : "\n%d REAPER tests failed\n", failures);
        return failures == 0 ? 0 : 1;
    }

    if (mode == "--play")
    {
        KupataProcessor proc;
        juce::AudioProcessor::BusesLayout layout;
        layout.outputBuses.add (juce::AudioChannelSet::discreteChannels (8));
        check (proc.setBusesLayout (layout) && proc.getTotalNumOutputChannels() == 8, "eight output channels");
        proc.prepareToPlay (48000.0, 512);
        auto play = [&] (int note, int blocks = 40) {
            juce::AudioBuffer<float> buf (8, 512);
            std::array<float, 8> peak {};
            bool finite = true;
            if (note >= 0)   // silence whatever still rings, so each check hears one pad
                for (int b = 0; b < 4; ++b)
                {
                    juce::MidiBuffer m;
                    if (b == 0) m.addEvent (juce::MidiMessage::allSoundOff (1), 0);
                    proc.processBlock (buf, m);
                }
            for (int b = 0; b < blocks; ++b)
            {
                buf.clear();
                juce::MidiBuffer midi;
                if (b == 0 && note >= 0) midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 120), 7);
                proc.processBlock (buf, midi);
                for (int ch = 0; ch < 8; ++ch)
                    for (int i = 0; i < 512; ++i)
                    {
                        const float v = buf.getSample (ch, i);
                        if (! std::isfinite (v)) finite = false;
                        peak[(size_t) ch] = juce::jmax (peak[(size_t) ch], std::abs (v));
                    }
            }
            return std::make_pair (finite, peak);
        };
        for (int pad = 0; pad < 16; ++pad)
        {
            auto [ok, pk] = play (proc.padInfo (pad).routing.note, 20);
            check (ok && pk[0] > 0.05f, "factory pad " + juce::String (pad + 1) + " sounds (peak " + juce::String (pk[0], 2) + ")");
        }
        proc.setMultiOut (true);
        auto [ok2, pk2] = play (42);   // pad 3, the closed hat
        check (ok2 && pk2[4] > 0.05f && pk2[0] < 1.0e-4f, "multi-out: pad 3 on outputs 5-6 only");
        auto [ok3, pk3] = play (49);   // pad 5, the cymbal
        check (ok3 && pk3[0] > 0.05f, "multi-out: pad 5 falls back to 1-2 when the track has no 9-10");
        proc.setMultiOut (false);
        proc.playPad (0, 1.0f);
        auto [ok4, pk4] = play (-1);
        check (ok4 && pk4[0] > 0.05f, "a click on a pad plays it");
        play (-1, 300);
        auto [ok5, pk5] = play (-1, 20);
        check (ok5 && pk5[0] < 1.0e-4f, "then silence");
        proc.startLearn (5);
        play (90, 2);
        check (proc.pollLearn() && proc.padInfo (5).routing.note == 90, "note learn: pad 6 now plays on 90");

        // the MIDI monitor, and a pad played by MIDI
        play (42, 2);
        check (proc.lastMidi().note == 42 && proc.lastMidi().velocity == 120 && proc.takeMidiPad() == 2, "the monitor shows the note (F#2) and the pad it played (3)");
        // pads 1-32 in a row from the next key
        proc.startBaseLearn();
        play (60, 2);
        check (proc.pollLearn() && proc.padInfo (0).routing.note == 60 && proc.padInfo (15).routing.note == 75, "in a row from the key played: pad 1 on C4");
        proc.setNotePreset (kupata::mapGeneralMidi);
        // bank B plays on its own notes and outputs
        juce::String err2;
        proc.loadFactory (16, 0);
        proc.setMultiOut (true, 64);
        juce::AudioProcessor::BusesLayout wide;
        wide.outputBuses.add (juce::AudioChannelSet::discreteChannels (64));
        check (proc.setBusesLayout (wide), "64 outputs");
        {
            juce::AudioBuffer<float> buf (64, 512);
            float peak33 = 0.0f;
            for (int b = 0; b < 20; ++b)
            {
                buf.clear();
                juce::MidiBuffer midi;
                if (b == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 72, (juce::uint8) 120), 0);
                proc.processBlock (buf, midi);
                peak33 = juce::jmax (peak33, buf.getMagnitude (32, 0, 512));
            }
            check (peak33 > 0.1f, "pad 17 (bank B) on C5, out of 33-34 (peak " + juce::String (peak33, 2) + ")");
        }
        // Program Change switches kits
        proc.setKitByProgram (true);
        proc.clearPad (0);
        {
            juce::AudioBuffer<float> buf (64, 512);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::programChange (1, 0), 0);
            proc.processBlock (buf, midi);
        }
        wait (400);
        check (proc.padInfo (0).loaded() && ! proc.padInfo (16).loaded(), "Program Change 0 brings back the analog kit");
        std::printf (failures == 0 ? "\nall playback tests passed\n" : "\n%d playback tests failed\n", failures);
        return failures == 0 ? 0 : 1;
    }

    const juce::String out = argc > 1 ? argv[1] : "kupata.png";
    const bool spanish = argc > 2 && juce::String (argv[2]) == "es";
    const int width  = argc > 3 ? juce::String (argv[3]).getIntValue() : 1180;
    const int height = argc > 4 ? juce::String (argv[4]).getIntValue() : 720;
    const juce::String variant = argc > 5 ? juce::String (argv[5]) : juce::String();

    auto proc = std::make_unique<KupataProcessor>();
    proc->setPlayConfigDetails (0, 2, 44100.0, 512);
    if (spanish) proc->setLanguage (kupata::Lang::es);
    proc->prepareToPlay (44100.0, 512);
    proc->setSelectedPad (0);
    proc->setParam ("p1_shape", 35.0f);
    proc->setParam ("p1_comp", 40.0f);
    proc->setParam ("p1_end", 82.0f);
    if (variant == "bankb")
    {
        proc->setBank (1);
        proc->loadFactory (16, 16); proc->loadFactory (17, 17); proc->loadFactory (18, 18); proc->loadFactory (20, 1);
        proc->setSelectedPad (20);
        proc->setParam ("p21_reverse", 1.0f);
    }
    if (variant == "hits")
    {
        juce::String error;
        proc->loadFile (4, writeTestFile ("kupata-hits.wav", 6, 3.0), error);
        proc->setSelectedPad (4);
        proc->setHit (4, 2);
    }
    if (variant == "empty") { proc->clearPad (0); proc->clearPad (5); }

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc->createEditor());
    editor->setSize (width, height);
    run (*proc, 40);
    run (*proc, 12, { { 0, 36 }, { 2, 42 }, { 4, 38 } });
    run (*proc, 2, { { 0, variant == "bankb" ? 76 : 38 } });   // the last note, for the MIDI monitor
    for (int i = 0; i < 6; ++i)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
        run (*proc, 1);
    }
    auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
    juce::File f = juce::File::getCurrentWorkingDirectory().getChildFile (out);
    f.deleteFile();
    juce::FileOutputStream os (f);
    juce::PNGImageFormat().writeImageToStream (img, os);
    editor.reset();
    return 0;
}
