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
        // two bars at 112 BPM: kick, snare, hats, clap, cowbell and a sub
        KupataProcessor proc;
        proc.setPlayConfigDetails (0, 2, 48000.0, 256);
        proc.prepareToPlay (48000.0, 256);
        const double sr = 48000.0, step = 60.0 / 112.0 / 4.0;
        const char* pattern[] {
            "x.......x.x.....x.......x...x...",   // 36 kick long
            "....x.......x.......x.......x..x",   // 38 snare
            "x.x.x.x.x.x.x.xxx.x.x.x.x.x.x.x.",   // 42 closed hat
            "..............x...............x.",   // 43 open hat
            "............x...............x...",   // 40 clap
            "......x.........x.....x.........",   // 48 cowbell
            "x...............x.......x.......",   // 51 sub
            "...x......x.........x.....x.....",   // 41 rim
        };
        const int notes[] { 36, 38, 42, 43, 40, 48, 51, 41 };
        proc.setParam ("gain", -7.0f);
        proc.setParam ("p16_volume", -9.0f);
        proc.setParam ("p13_volume", -8.0f);
        proc.setParam ("p7_volume", -4.0f);
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
        check (b.padInfo (6).routing.choke == 1 && b.padInfo (7).routing.choke == 1, "the hats keep their choke group");

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
        check (d.padInfo (11).loaded() && std::abs (d.padParams (11).shape + 40.0f) < 0.01f && d.padInfo (4).loaded() && d.padInfo (4).factory == 11,
               "a pad moves with its knobs and the other comes back");
        d.setHit (11, 0);
        check (d.hitsToPads (11) == 4 && d.padInfo (14).hit == 4, "hits to pads: four pads filled");
        std::printf (failures == 0 ? "\nall state tests passed\n" : "\n%d state tests failed\n", failures);
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
            auto [ok, pk] = play (36 + pad, 20);
            check (ok && pk[0] > 0.05f, "factory pad " + juce::String (pad + 1) + " sounds (peak " + juce::String (pk[0], 2) + ")");
        }
        proc.setMultiOut (true);
        auto [ok2, pk2] = play (38);
        check (ok2 && pk2[4] > 0.05f && pk2[0] < 1.0e-4f, "multi-out: pad 3 on outputs 5-6 only");
        auto [ok3, pk3] = play (40);
        check (ok3 && pk3[0] > 0.05f, "multi-out: pad 5 falls back to 1-2 when the track has no 9-10");
        proc.setMultiOut (false);
        proc.playPad (0, 1.0f);
        auto [ok4, pk4] = play (-1);
        check (ok4 && pk4[0] > 0.05f, "a click on a pad plays it");
        play (-1, 300);
        auto [ok5, pk5] = play (-1, 20);
        check (ok5 && pk5[0] < 1.0e-4f, "then silence");
        proc.startLearn (5);
        play (70, 2);
        check (proc.pollLearn() && proc.padInfo (5).routing.note == 70, "note learn: pad 6 now plays on 70");
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
    proc->setParam ("p8_mute", 1.0f);
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
