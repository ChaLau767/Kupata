// Development test: loads the built VST3 the way a DAW does, checks the output layouts,
// plays the factory kit, opens the interface and saves the session.
// Playback is tested further with kupata_snapshot --play.
// Only built with -DKUPATA_SNAPSHOT=ON.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include <cstdio>

static int failures = 0;
static const bool unbuffered = [] { std::setvbuf (stdout, nullptr, _IONBF, 0); return true; }();
static void check (bool ok, const juce::String& what)
{
    std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
    if (! ok) ++failures;
}

struct PlayHead : juce::AudioPlayHead
{
    double ppq = 0.0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying (playing);
        p.setBpm (100.0);
        p.setPpqPosition (ppq);
        p.setTimeSignature (TimeSignature { 4, 4 });
        return p;
    }
};

[[maybe_unused]] static juce::File writeLoop()
{
    const double sr = 48000.0;
    const int len = (int) (sr * 2.4);
    juce::AudioBuffer<float> buf (1, len);
    buf.clear();
    for (int h = 0; h < 8; ++h)
        for (int i = 0; i < len / 8; ++i)
        {
            const double t = i / sr;
            buf.setSample (0, h * len / 8 + i, (float) (0.7 * std::exp (-t * 20.0) * std::sin (2.0 * juce::MathConstants<double>::pi * (100.0 + 40.0 * h) * t)));
        }
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("kupata-hosttest.wav");
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), sr, 1, 16, {}, 0));
    if (w != nullptr) w->writeFromAudioSampleBuffer (buf, 0, len);
    return f;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 2) { std::printf ("usage: kupata_hosttest <path to Kupata.vst3>\n"); return 2; }

    juce::AudioPluginFormatManager formats;
    formats.addFormat (new juce::VST3PluginFormat());
    juce::OwnedArray<juce::PluginDescription> found;
    juce::VST3PluginFormat vst3;
    vst3.findAllTypesForFile (found, argv[1]);
    check (found.size() == 1, "plugin found in the VST3 bundle");
    if (found.isEmpty()) return 1;
    check (found[0]->isInstrument, "it is an instrument");

    juce::String error;
    auto plugin = formats.createPluginInstance (*found[0], 48000.0, 512, error);
    check (plugin != nullptr, "plugin instance created " + error);
    if (plugin == nullptr) return 1;

    // output layouts: mono, stereo, named speaker sets and 64 channels (a 7th-order ambisonic arrangement)
    juce::Array<juce::AudioChannelSet::ChannelType> namedTypes;
    for (int t = juce::AudioChannelSet::left; t < juce::AudioChannelSet::discreteChannel0; ++t)
    {
        const auto type = (juce::AudioChannelSet::ChannelType) t;
        if (type >= juce::AudioChannelSet::ambisonicACN0 && type <= juce::AudioChannelSet::ambisonicACN35) continue;
        if (juce::AudioChannelSet::getAbbreviatedChannelTypeName (type).isNotEmpty()) namedTypes.add (type);
    }
    for (int outs : { 1, 2, 4, 8, 16, 24, 64 })
    {
        juce::AudioChannelSet set;
        if (outs == 1) set = juce::AudioChannelSet::mono();
        else if (outs == 2) set = juce::AudioChannelSet::stereo();
        else if (outs == 64) set = juce::AudioChannelSet::ambisonic (7);
        else for (int i = 0; i < outs; ++i) set.addChannel (namedTypes[i]);
        juce::AudioProcessor::BusesLayout layout;
        layout.outputBuses.add (set);
        check (plugin->setBusesLayout (layout) && plugin->getTotalNumOutputChannels() == outs,
               "layout with " + juce::String (outs) + " outputs accepted (" + juce::String (plugin->getTotalNumOutputChannels()) + ")");
    }
    {
        juce::AudioProcessor::BusesLayout layout;
        layout.outputBuses.add (juce::AudioChannelSet::stereo());
        check (plugin->setBusesLayout (layout) && plugin->getTotalNumOutputChannels() == 2, "back to stereo");
    }
    check (plugin->getParameters().size() >= 2 + 16 * 11, "every pad has its own parameters (" + juce::String (plugin->getParameters().size()) + ")");

    PlayHead head;
    plugin->setPlayHead (&head);
    plugin->prepareToPlay (48000.0, 512);

    // the factory kit plays at once, with no file of its own
    {
        juce::AudioBuffer<float> buf (2, 512);
        float peak = 0.0f;
        bool finite = true;
        for (int b = 0; b < 40; ++b)
        {
            buf.clear();
            juce::MidiBuffer midi;
            if (b == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 120), 3);
            plugin->processBlock (buf, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i) { const float v = buf.getSample (ch, i); if (! std::isfinite (v)) finite = false; peak = juce::jmax (peak, std::abs (v)); }
        }
        check (finite && peak > 0.3f, "C2 plays the factory kick (peak " + juce::String (peak, 2) + ")");
    }

    // notes far from the pads, and the transport: no sound and no crash
    {
        juce::AudioBuffer<float> buf (2, 512);
        bool finite = true;
        float peak = 0.0f;
        for (int b = 0; b < 200; ++b)
        {
            buf.clear();
            juce::MidiBuffer midi;
            if (b == 0) midi.addEvent (juce::MidiMessage::allSoundOff (1), 0);   // the kick above still rings
            if (b % 10 == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 90 + b % 24, (juce::uint8) 100), 3);
            if (b % 10 == 5) midi.addEvent (juce::MidiMessage::noteOn (1, 12 + (b / 10) % 8, (juce::uint8) 100), 0);
            plugin->processBlock (buf, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i) { const float v = buf.getSample (ch, i); if (! std::isfinite (v)) finite = false; if (b >= 2) peak = juce::jmax (peak, std::abs (v)); }
            head.ppq += 100.0 / 60.0 * 512 / 48000.0;
        }
        check (finite && peak < 1.0e-6f, "notes without pads stay silent");
    }

    // the interface opens and closes inside the host
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (plugin->createEditorIfNeeded());
        check (editor != nullptr && editor->getWidth() > 500, "the interface opens");
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
        editor.reset();
    }

    // the session survives a save and reload through the VST3 format
    {
        juce::MemoryBlock saved;
        plugin->getStateInformation (saved);
        plugin->setStateInformation (saved.getData(), (int) saved.getSize());
        check (saved.getSize() > 100, "the session saves and reloads (" + juce::String ((int) saved.getSize()) + " bytes)");
    }

    plugin->releaseResources();
    plugin.reset();
    std::printf (failures == 0 ? "\nall host tests passed\n" : "\n%d host tests failed\n", failures);
    return failures == 0 ? 0 : 1;
}
