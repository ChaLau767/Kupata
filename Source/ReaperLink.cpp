// Kupata — working together with REAPER (VST3 only)
// Copyright (C) 2026 Lautaro Martín Vieyra
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The interface below has the same layout as IReaperHostApplication in the REAPER SDK
// (reaper_vst3_interfaces.h, by Cockos, zlib licence), declared here so that nothing
// else from the VST3 SDK is needed. The function names and signatures are those of
// reaper_plugin_functions.h.

#include "ReaperLink.h"

JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wshadow", "-Wshadow-field-in-constructor", "-Wnon-virtual-dtor", "-Wzero-as-null-pointer-constant", "-Wunused-parameter")
JUCE_BEGIN_IGNORE_WARNINGS_MSVC (4100)
#include <pluginterfaces/base/funknown.h>
JUCE_END_IGNORE_WARNINGS_MSVC
JUCE_END_IGNORE_WARNINGS_GCC_LIKE

JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wnon-virtual-dtor")
struct ReaperLink::Host : public Steinberg::FUnknown
{
    virtual void* PLUGIN_API getReaperApi (const char* funcname) = 0;
    virtual void* PLUGIN_API getReaperParent (Steinberg::uint32 w) = 0;   // 1 track, 3 project
    virtual void* PLUGIN_API reaperExtended (Steinberg::uint32 call, void* a, void* b, void* c) = 0;
};
JUCE_END_IGNORE_WARNINGS_GCC_LIKE

namespace
{
    const Steinberg::TUID reaperHostIid = INLINE_UID (0x79655E36, 0x77EE4267, 0xA573FEF7, 0x4912C27C);
    const char* const markKey = "P_EXT:kupata";

    template <typename F> F fn (void* p) { return reinterpret_cast<F> (p); }
}

ReaperLink::~ReaperLink()
{
    if (host != nullptr) host->release();
}

void ReaperLink::setIHostApplication (Steinberg::FUnknown* ptr)
{
    if (ptr == nullptr || host != nullptr) return;
    void* obj = nullptr;
    if (ptr->queryInterface (reaperHostIid, &obj) == Steinberg::kResultOk && obj != nullptr)
    {
        host = static_cast<Host*> (obj);
        getFunc = reinterpret_cast<void* (*) (const char*)> (host->getReaperApi ("GetMediaTrackInfo_Value"));
        if (getFunc == nullptr) { host->release(); host = nullptr; }
    }
}

void* ReaperLink::api (const char* name) { return host != nullptr ? host->getReaperApi (name) : nullptr; }
void* ReaperLink::parentTrack()          { return host != nullptr ? host->getReaperParent (1) : nullptr; }
void* ReaperLink::project()              { return host != nullptr ? host->getReaperParent (3) : nullptr; }

int ReaperLink::trackChannels()
{
    auto* tr = parentTrack();
    auto get = fn<double (*) (void*, const char*)> (api ("GetMediaTrackInfo_Value"));
    if (tr == nullptr || get == nullptr) return 0;
    return juce::jlimit (0, 128, juce::roundToInt (get (tr, "I_NCHAN")));
}

std::vector<ReaperLink::Route> ReaperLink::routes()
{
    std::vector<Route> out;
    auto* tr = parentTrack();
    auto num = fn<int (*) (void*, int)> (api ("GetTrackNumSends"));
    auto value = fn<double (*) (void*, int, int, const char*)> (api ("GetTrackSendInfo_Value"));
    auto info = fn<void* (*) (void*, int, int, const char*, void*)> (api ("GetSetTrackSendInfo"));
    if (tr == nullptr || num == nullptr || value == nullptr || info == nullptr) return out;
    const int n = num (tr, 0);
    for (int i = 0; i < n; ++i)
    {
        const int src = (int) value (tr, 0, i, "I_SRCCHAN");
        if (src < 0) continue;
        Route r;
        r.channel = src & 1023;
        r.mono = (src & 1024) != 0;
        r.track = info (tr, 0, i, "P_DESTTRACK", nullptr);
        if (r.track != nullptr) out.push_back (r);
    }
    return out;
}

juce::String ReaperLink::trackName (void* track)
{
    auto s = fn<bool (*) (void*, const char*, char*, bool)> (api ("GetSetMediaTrackInfo_String"));
    if (track == nullptr || s == nullptr) return {};
    char buf[4096] {};
    return s (track, "P_NAME", buf, false) ? juce::String::fromUTF8 (buf) : juce::String();
}

void ReaperLink::setTrackName (void* track, const juce::String& name)
{
    auto s = fn<bool (*) (void*, const char*, char*, bool)> (api ("GetSetMediaTrackInfo_String"));
    if (track == nullptr || s == nullptr) return;
    juce::MemoryBlock b (name.toRawUTF8(), name.getNumBytesAsUTF8() + 1);
    s (track, "P_NAME", static_cast<char*> (b.getData()), true);
}

juce::String ReaperLink::trackMark (void* track)
{
    auto s = fn<bool (*) (void*, const char*, char*, bool)> (api ("GetSetMediaTrackInfo_String"));
    if (track == nullptr || s == nullptr) return {};
    char buf[1024] {};
    return s (track, markKey, buf, false) ? juce::String::fromUTF8 (buf) : juce::String();
}

void ReaperLink::setTrackMark (void* track, const juce::String& mark)
{
    auto s = fn<bool (*) (void*, const char*, char*, bool)> (api ("GetSetMediaTrackInfo_String"));
    if (track == nullptr || s == nullptr) return;
    juce::MemoryBlock b (mark.toRawUTF8(), mark.getNumBytesAsUTF8() + 1);
    s (track, markKey, static_cast<char*> (b.getData()), true);
}

int ReaperLink::buildRouting (const juce::StringArray& names, const juce::String& undoName)
{
    auto* parent = parentTrack();
    auto* proj = project();
    auto getVal = fn<double (*) (void*, const char*)> (api ("GetMediaTrackInfo_Value"));
    auto setVal = fn<bool (*) (void*, const char*, double)> (api ("SetMediaTrackInfo_Value"));
    auto insert = fn<void (*) (int, bool)> (api ("InsertTrackAtIndex"));
    auto getTrack = fn<void* (*) (void*, int)> (api ("GetTrack"));
    auto send = fn<int (*) (void*, void*)> (api ("CreateTrackSend"));
    auto setSend = fn<bool (*) (void*, int, int, const char*, double)> (api ("SetTrackSendInfo_Value"));
    auto undoBegin = fn<void (*) (void*)> (api ("Undo_BeginBlock2"));
    auto undoEnd = fn<void (*) (void*, const char*, int)> (api ("Undo_EndBlock2"));
    auto refresh = fn<void (*) (bool)> (api ("TrackList_AdjustWindows"));
    auto update = fn<void (*)()> (api ("UpdateArrange"));
    if (parent == nullptr || getVal == nullptr || setVal == nullptr || insert == nullptr || getTrack == nullptr
        || send == nullptr || setSend == nullptr)
        return 0;

    const int index = juce::roundToInt (getVal (parent, "IP_TRACKNUMBER"));   // 1-based; the new tracks go right after it
    if (index <= 0) return 0;
    const int count = names.size();
    if (undoBegin != nullptr) undoBegin (proj);

    setVal (parent, "I_NCHAN", (double) juce::jlimit (2, 128, count * 2));
    setVal (parent, "B_MAINSEND", 0.0);   // each pad now reaches the mix through its own track
    const int depth = juce::roundToInt (getVal (parent, "I_FOLDERDEPTH"));
    setVal (parent, "I_FOLDERDEPTH", 1.0);

    int made = 0;
    for (int k = 0; k < count; ++k)
    {
        insert (index + k, true);
        auto* tr = getTrack (proj, index + k);
        if (tr == nullptr) break;
        setTrackName (tr, names[k]);
        setTrackMark (tr, names[k]);
        const int s = send (parent, tr);
        if (s >= 0)
        {
            setSend (parent, 0, s, "I_SRCCHAN", (double) (k * 2));
            setSend (parent, 0, s, "I_DSTCHAN", 0.0);
            setSend (parent, 0, s, "I_SENDMODE", 0.0);   // post-fader
            setSend (parent, 0, s, "I_MIDIFLAGS", 31.0);   // audio only: no MIDI goes with it
        }
        setVal (tr, "I_FOLDERDEPTH", 0.0);
        ++made;
    }
    if (made > 0)
    {
        auto* last = getTrack (proj, index + made - 1);
        // the folder closes after the last pad, keeping whatever the parent track closed before
        setVal (last, "I_FOLDERDEPTH", depth >= 1 ? 0.0 : (double) (depth - 1));
        if (depth >= 1) setVal (parent, "I_FOLDERDEPTH", 1.0);
    }
    if (undoEnd != nullptr) undoEnd (proj, undoName.toRawUTF8(), -1);
    if (refresh != nullptr) refresh (false);
    if (update != nullptr) update();
    return made;
}
