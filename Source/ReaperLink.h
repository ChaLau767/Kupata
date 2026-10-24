// Kupata — working together with REAPER (VST3 only)
// Copyright (C) 2026 Lautaro Martín Vieyra
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// When Kupata runs as a VST3 in REAPER, REAPER hands it its extension API. With it Kupata
//  - knows how many channels its track really has (pads aimed at a channel the track does
//    not have fall back to 1-2, so nothing goes silent),
//  - notices when REAPER has built multi-output routing (tracks that receive from Kupata's
//    channels 3-4, 5-6…) and then sends each pad to its own pair,
//  - names those tracks after the pads, and keeps the names in step when a pad changes,
//  - can build that routing itself: one track per pad, in a folder under Kupata's track.
// Everything here runs on the message thread, which in REAPER is its main thread.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class ReaperLink : public juce::VST3ClientExtensions
{
public:
    ~ReaperLink() override;

    void setIHostApplication (Steinberg::FUnknown*) override;
    bool available() const noexcept { return getFunc != nullptr; }

    struct Route { int channel = 0; bool mono = false; void* track = nullptr; };

    int trackChannels();                        // 0 when unknown
    std::vector<Route> routes();                // the sends of Kupata's track (channel: the first, 0-based)
    juce::String trackName (void* track);
    void setTrackName (void* track, const juce::String&);
    juce::String trackMark (void* track);       // what Kupata wrote in the track, "" if nothing
    void setTrackMark (void* track, const juce::String&);

    // One track per pad (names in order), receiving its pair. Returns how many were made.
    int buildRouting (const juce::StringArray& names, const juce::String& undoName);

private:
    void* api (const char* name);
    void* parentTrack();
    void* project();

    struct Host;                                // the IReaperHostApplication interface
    Host* host = nullptr;
    void* (*getFunc) (const char*) = nullptr;   // not used directly: marks that REAPER answered
};
