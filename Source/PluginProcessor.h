// Kupata — processor
// Copyright (C) 2026 Lautaro Martín Vieyra
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "KupataEngine.h"
#include "Parameters.h"
#include "ReaperLink.h"

class KupataProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    KupataProcessor();
    ~KupataProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Kupata"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Kupata"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    // Everything below is called from the message thread (the interface).
    struct PadInfo
    {
        std::shared_ptr<const kupata::Sound> sound;
        juce::String name, path;
        int factory = -1;             // index in the factory kit, or -1
        bool missing = false;         // the session named a file that is not there
        int numHits = 1, hit = 0;     // hit 0: the whole sound
        double fileSeconds = 0.0;
        kupata::PadRouting routing;
        bool loaded() const { return sound != nullptr; }
    };

    PadInfo padInfo (int pad);
    bool loadFile (int pad, const juce::File& file, juce::String& error);
    void loadFactory (int pad, int index);
    void loadFactoryKit();
    void clearPad (int pad);
    void movePad (int from, int to, bool copy);   // with its knobs and settings
    void setHit (int pad, int hit);
    int  hitsToPads (int pad);                     // returns how many pads were filled
    void setRouting (int pad, const kupata::PadRouting&);
    void setNotePreset (int preset);               // 0 chromatic, 1 white keys, 2 General MIDI
    void setMultiOut (bool on, int channels = -1);   // channels: how many the track has (-1: as now)
    void resetKnobs (int pad);
    juce::String padName (int pad, kupata::Lang) const;

    void startLearn (int pad) { learnPad = pad; }
    int  learningPad() const { return learnPad.load(); }
    bool pollLearn();                              // applies a learned note; true when it did

    void playPad (int pad, float velocity);        // from the interface
    void releasePad (int pad);
    bool previewFile (const juce::File&);
    void stopPreview();

    juce::File kitsFolder() const;
    bool saveKit (const juce::File&, const juce::String& name);
    bool loadKit (const juce::File&);
    juce::String getKitName() const { return apvts.state.getProperty ("kitName", "").toString(); }

    // The pad as it sounds with its knobs now (mono, at the sound's own rate), for the interface.
    void renderPicture (int pad, std::vector<float>& dest, int maxFrames);
    kupata::PadParams padParams (int pad) const;

    int  getSelectedPad() const { return juce::jlimit (0, kupata::numPads - 1, (int) apvts.state.getProperty ("selected", 0)); }
    void setSelectedPad (int pad) { apvts.state.setProperty ("selected", juce::jlimit (0, kupata::numPads - 1, pad), nullptr); }
    kupata::Lang getLanguage() const;
    void setLanguage (kupata::Lang);
    void setParam (const juce::String& id, float plainValue);

    bool readTelemetry (kupata::Telemetry& dest);
    int getVersion() const { return version.load(); }
    // the channels Kupata can really use: those of the plugin, and in REAPER no more than the track has
    int numOutputs() const
    {
        const int plugin = getTotalNumOutputChannels(), track = reaperChannels.load();
        return track > 0 ? juce::jmax (1, juce::jmin (plugin, track)) : plugin;
    }

    // REAPER (VST3): see ReaperLink.h
    bool reaperAvailable() const { return reaper.available(); }
    bool reaperRouted() const { return reaperHasRoutes.load(); }
    int  buildReaperTracks();
    juce::VST3ClientExtensions* getVST3ClientExtensions() override { return &reaper; }

    juce::AudioProcessorValueTreeState apvts;
    juce::AudioFormatManager formats;

private:
    struct Meta
    {
        std::shared_ptr<const kupata::Sample> sample;
        std::vector<float> mono;
        std::vector<int> hits { 0 };
        int hit = 0;
        juce::String name, path;
        int factory = -1;
        bool missing = false;
        juce::MemoryBlock embedded;     // the audio as FLAC, saved with the session
        juce::ValueTree saved;          // what the session said, kept while the file is missing
    };

    static BusesProperties buses();
    kupata::Params readParams() const noexcept;
    std::shared_ptr<kupata::Sample> readAudio (std::unique_ptr<juce::AudioFormatReader>, juce::String& error);
    std::shared_ptr<kupata::Sample> readMemory (const void* data, size_t size, juce::String& error);
    void install (int pad, Meta&& m);   // builds its sound and gives it to the engine
    void rebuild (int pad);
    void setPadSound (int pad, std::shared_ptr<const kupata::Sound>);
    juce::MemoryBlock encode (const kupata::Sample&) const;
    juce::ValueTree savePad (int pad, bool forKit) const;
    void restorePad (int pad, const juce::ValueTree&);
    void setPadParams (int pad, const std::map<juce::String, float>& values);

    kupata::Engine engine;
    juce::SpinLock engineLock;
    std::array<Meta, kupata::numPads> meta;

    std::array<std::atomic<float>, kupata::numPads> uiHit {};
    std::array<std::atomic<bool>, kupata::numPads> uiRelease {};
    std::atomic<int> learnPad { -1 }, learnedNote { -1 }, learnedFor { -1 };
    std::atomic<bool> stopAll { false };
    std::atomic<int> version { 0 };

    std::array<kupata::Event, 1024> events {};
    void timerCallback() override;
    void syncReaper();
    ReaperLink reaper;
    std::atomic<int> reaperChannels { 0 };
    std::atomic<bool> reaperHasRoutes { false };

    kupata::Telemetry telemetry;
    juce::SpinLock telemetryLock;
    int samplesSinceTelemetry = 0;

    struct PadRefs { std::atomic<float> *shape, *tune, *tone, *comp, *volume, *pan, *start, *end, *gate, *mute, *solo; };
    std::array<PadRefs, kupata::numPads> padRefs {};
    std::atomic<float> *gainRef = nullptr, *velocityRef = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KupataProcessor)
};
