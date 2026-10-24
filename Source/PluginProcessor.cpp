// Kupata — processor
// Copyright (C) 2026 Lautaro Martín Vieyra
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"

using namespace kupata;

namespace
{
    constexpr double maxSeconds = 60.0;        // longer files are cut
    constexpr double embedSeconds = 12.0;      // sounds up to this long travel inside the session
    const char* const paramBases[] { "shape", "tune", "tone", "comp", "volume", "pan", "start", "end", "gate", "mute", "solo" };

    const void* factoryData (int index, int& size)
    {
        size = 0;
        if (index < 0 || index >= numFactorySounds) return nullptr;
        const juce::String wanted = juce::String (factorySounds()[(size_t) index].file) + ".flac";
        for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
            if (wanted == BinaryData::getNamedResourceOriginalFilename (BinaryData::namedResourceList[i]))
                return BinaryData::getNamedResource (BinaryData::namedResourceList[i], size);
        return nullptr;
    }
}

// One output bus with as many channels as the track has (up to 64). Stereo by default, except as a
// VST3 in REAPER: there it starts with 32 channels, a pair per pad, so that REAPER offers to build
// the multi-output routing when it is inserted on a new track.
juce::AudioProcessor::BusesProperties KupataProcessor::buses()
{
    const bool reaperVst3 = juce::PluginHostType::getPluginLoadedAs() == juce::AudioProcessor::wrapperType_VST3
                            && juce::PluginHostType().isReaper();
    return BusesProperties().withOutput ("Output", reaperVst3 ? juce::AudioChannelSet::discreteChannels (numPads * 2)
                                                              : juce::AudioChannelSet::stereo(), true);
}

KupataProcessor::KupataProcessor()
    : AudioProcessor (buses()),
      apvts (*this, nullptr, "KUPATA", createLayout())
{
    formats.registerBasicFormats();
    auto r = [this] (const juce::String& id) { return apvts.getRawParameterValue (id); };
    gainRef = r ("gain");
    velocityRef = r ("velocity");
    for (int p = 0; p < numPads; ++p)
    {
        auto q = [&] (const char* b) { return r (padId (p, b)); };
        padRefs[(size_t) p] = { q ("shape"), q ("tune"), q ("tone"), q ("comp"), q ("volume"), q ("pan"), q ("start"), q ("end"),
                                q ("gate"), q ("mute"), q ("solo") };
    }
    if (! apvts.state.hasProperty ("lang")) apvts.state.setProperty ("lang", 0, nullptr);
    loadFactoryKit();
    startTimer (500);
}

KupataProcessor::~KupataProcessor()
{
    stopTimer();
    const juce::SpinLock::ScopedLockType lock (engineLock);
    engine.setPreview (nullptr);
}

bool KupataProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (! layouts.inputBuses.isEmpty() && layouts.getMainInputChannels() != 0) return false;
    const int outs = layouts.getMainOutputChannels();
    return outs >= 1 && outs <= maxOutputs;
}

void KupataProcessor::prepareToPlay (double sampleRate, int)
{
    const juce::SpinLock::ScopedLockType lock (engineLock);
    engine.prepare (sampleRate);
    samplesSinceTelemetry = 0;
}

Params KupataProcessor::readParams() const noexcept
{
    Params p;
    p.gain = gainRef->load();
    p.velocity = velocityRef->load();
    for (int i = 0; i < numPads; ++i)
    {
        const auto& R = padRefs[(size_t) i];
        auto& o = p.pads[(size_t) i];
        o.shape = R.shape->load(); o.tune = R.tune->load(); o.tone = R.tone->load(); o.comp = R.comp->load();
        o.volume = R.volume->load(); o.pan = R.pan->load(); o.start = R.start->load(); o.end = R.end->load();
        o.gate = R.gate->load() > 0.5f; o.mute = R.mute->load() > 0.5f; o.solo = R.solo->load() > 0.5f;
    }
    return p;
}

PadParams KupataProcessor::padParams (int pad) const
{
    return readParams().pads[(size_t) juce::jlimit (0, numPads - 1, pad)];
}

void KupataProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    // in REAPER, channels beyond the track's are dropped: count only those it has
    const int numOut = juce::jmin (numOutputs(), buffer.getNumChannels(), maxOutputs);

    int numEvents = 0;
    auto add = [&] (const Event& e) { if (numEvents < (int) events.size()) events[(size_t) numEvents++] = e; };
    if (stopAll.exchange (false)) { Event e; e.type = Event::allNotesOff; add (e); }
    for (int p = 0; p < numPads; ++p)
    {
        if (uiRelease[(size_t) p].exchange (false)) { Event e; e.type = Event::padOff; e.pad = p; add (e); }
        const float v = uiHit[(size_t) p].exchange (0.0f);
        if (v > 0.0f) { Event e; e.type = Event::padOn; e.pad = p; e.velocity = v; add (e); }
    }
    for (const auto ev : midi)
    {
        const auto m = ev.getMessage();
        Event e;
        e.offset = juce::jlimit (0, juce::jmax (0, n - 1), ev.samplePosition);
        if (m.isNoteOn())
        {
            const int lp = learnPad.load();
            if (lp >= 0) { learnedNote = m.getNoteNumber(); learnedFor = lp; learnPad = -1; }
            e.type = Event::noteOn; e.note = m.getNoteNumber(); e.velocity = m.getFloatVelocity();
        }
        else if (m.isNoteOff())                   { e.type = Event::noteOff; e.note = m.getNoteNumber(); }
        else if (m.isAllNotesOff() || m.isAllSoundOff()) e.type = Event::allNotesOff;
        else continue;
        add (e);
    }
    midi.clear();

    const juce::SpinLock::ScopedTryLockType lock (engineLock);
    if (! lock.isLocked() || numOut < 1)
    {
        buffer.clear();
        return;
    }
    engine.process (buffer.getArrayOfWritePointers(), numOut, n, readParams(), events.data(), numEvents);
    for (int ch = numOut; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, n);

    samplesSinceTelemetry += n;
    if (samplesSinceTelemetry >= (int) (getSampleRate() / 40.0))
    {
        const juce::SpinLock::ScopedTryLockType tl (telemetryLock);
        if (tl.isLocked())
        {
            engine.snapshot (telemetry);
            samplesSinceTelemetry = 0;
        }
    }
}

bool KupataProcessor::readTelemetry (Telemetry& dest)
{
    const juce::SpinLock::ScopedLockType lock (telemetryLock);
    dest = telemetry;
    telemetry.outPeak = 0.0f;
    return true;
}

//==============================================================================
std::shared_ptr<Sample> KupataProcessor::readAudio (std::unique_ptr<juce::AudioFormatReader> reader, juce::String& error)
{
    if (reader == nullptr) { error = "unsupported"; return nullptr; }
    const auto maxLength = (juce::int64) (reader->sampleRate * maxSeconds);
    const int length = (int) juce::jmin (reader->lengthInSamples, maxLength);
    if (length < 32) { error = "tooShort"; return nullptr; }
    juce::AudioBuffer<float> buf ((int) juce::jmax (1u, juce::jmin (2u, reader->numChannels)), length);
    reader->read (&buf, 0, length, 0, true, buf.getNumChannels() > 1);
    auto smp = std::make_shared<Sample>();
    smp->sampleRate = reader->sampleRate > 0 ? reader->sampleRate : 44100.0;
    smp->left.assign (buf.getReadPointer (0), buf.getReadPointer (0) + length);
    const float* r = buf.getReadPointer (buf.getNumChannels() > 1 ? 1 : 0);
    smp->right.assign (r, r + length);
    return smp;
}

std::shared_ptr<Sample> KupataProcessor::readMemory (const void* data, size_t size, juce::String& error)
{
    if (data == nullptr || size == 0) { error = "unsupported"; return nullptr; }
    auto stream = std::make_unique<juce::MemoryInputStream> (data, size, false);
    return readAudio (std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (std::move (stream))), error);
}

juce::MemoryBlock KupataProcessor::encode (const Sample& s) const
{
    juce::MemoryBlock block;
    juce::FlacAudioFormat flac;
    const bool stereo = s.left != s.right;
    auto* os = new juce::MemoryOutputStream (block, false);
    std::unique_ptr<juce::AudioFormatWriter> w (flac.createWriterFor (os, juce::jlimit (8000.0, 192000.0, s.sampleRate), stereo ? 2u : 1u, 24, {}, 0));
    if (w == nullptr) { delete os; return {}; }
    const float* ch[2] { s.left.data(), s.right.data() };
    w->writeFromFloatArrays (ch, stereo ? 2 : 1, s.length());
    w.reset();   // flushes and deletes the stream
    return block;
}

void KupataProcessor::setPadSound (int pad, std::shared_ptr<const Sound> sound)
{
    std::shared_ptr<const Sound> old;
    {
        const juce::SpinLock::ScopedLockType lock (engineLock);
        engine.stopVoicesOf (pad);
        old = std::move (engine.sounds[(size_t) pad]);
        engine.sounds[(size_t) pad] = std::move (sound);
    }
    old.reset();   // freed here, on the message thread
    ++version;
}

void KupataProcessor::rebuild (int pad)
{
    auto& m = meta[(size_t) pad];
    if (m.sample == nullptr || m.mono.empty()) { setPadSound (pad, nullptr); return; }
    const int len = m.sample->length();
    int from = 0, to = len;
    const int numHits = (int) m.hits.size();
    if (m.hit > 0 && m.hit <= numHits)
    {
        from = m.hits[(size_t) m.hit - 1];
        to = m.hit < numHits ? m.hits[(size_t) m.hit] : len;
    }
    else m.hit = 0;
    const auto [a, b] = analysis::trim (m.mono, from, to, m.sample->sampleRate);
    setPadSound (pad, analysis::makeSound (m.sample, m.mono, a, b));
}

void KupataProcessor::install (int pad, Meta&& m)
{
    if (! juce::isPositiveAndBelow (pad, numPads)) return;
    if (m.sample != nullptr)
    {
        m.mono = analysis::mono (*m.sample);
        m.hits = analysis::hits (m.mono, m.sample->sampleRate);
        if (m.hits.empty()) m.hits = { 0 };
        if (m.hit >= (int) m.hits.size() + 1) m.hit = 0;
    }
    meta[(size_t) pad] = std::move (m);
    rebuild (pad);
}

bool KupataProcessor::loadFile (int pad, const juce::File& file, juce::String& error)
{
    if (! juce::isPositiveAndBelow (pad, numPads)) return false;
    auto smp = readAudio (std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (file)), error);
    if (smp == nullptr) return false;
    Meta m;
    m.sample = smp;
    m.name = file.getFileNameWithoutExtension();
    m.path = file.getFullPathName();
    if (smp->length() <= (int) (smp->sampleRate * embedSeconds)) m.embedded = encode (*smp);
    install (pad, std::move (m));
    return true;
}

void KupataProcessor::loadFactory (int pad, int index)
{
    if (! juce::isPositiveAndBelow (pad, numPads) || ! juce::isPositiveAndBelow (index, numFactorySounds)) return;
    int size = 0;
    const void* data = factoryData (index, size);
    juce::String error;
    auto smp = readMemory (data, (size_t) size, error);
    Meta m;
    m.sample = smp;
    m.factory = index;
    install (pad, std::move (m));
}

void KupataProcessor::loadFactoryKit()
{
    for (int p = 0; p < numPads; ++p)
    {
        loadFactory (p, factoryLayout (p));
        resetKnobs (p);
        PadRouting r;
        r.note = presetNote (2, p);   // the kit's own notes (General MIDI, as in Sitala)
        r.choke = factorySounds()[(size_t) factoryLayout (p)].choke;
        setRouting (p, r);
    }
    apvts.state.setProperty ("kitName", "", nullptr);
    ++version;
}

void KupataProcessor::clearPad (int pad)
{
    if (! juce::isPositiveAndBelow (pad, numPads)) return;
    meta[(size_t) pad] = Meta {};
    setPadSound (pad, nullptr);
}

juce::String KupataProcessor::padName (int pad, Lang lang) const
{
    if (! juce::isPositiveAndBelow (pad, numPads)) return {};
    const auto& m = meta[(size_t) pad];
    juce::String n = m.factory >= 0 ? u8 (lang == Lang::es ? factorySounds()[(size_t) m.factory].es : factorySounds()[(size_t) m.factory].en) : m.name;
    if (m.hit > 0 && m.hits.size() > 1) n << " " << m.hit;
    return n;
}

KupataProcessor::PadInfo KupataProcessor::padInfo (int pad)
{
    PadInfo info;
    if (! juce::isPositiveAndBelow (pad, numPads)) return info;
    const auto& m = meta[(size_t) pad];
    info.name = padName (pad, getLanguage());
    info.path = m.path;
    info.factory = m.factory;
    info.missing = m.missing;
    info.numHits = (int) m.hits.size();
    info.hit = m.hit;
    info.fileSeconds = m.sample != nullptr ? m.sample->length() / m.sample->sampleRate : 0.0;
    const juce::SpinLock::ScopedLockType lock (engineLock);
    info.sound = engine.sounds[(size_t) pad];
    info.routing = engine.routing[(size_t) pad];
    return info;
}

void KupataProcessor::setHit (int pad, int hit)
{
    if (! juce::isPositiveAndBelow (pad, numPads) || meta[(size_t) pad].sample == nullptr) return;
    meta[(size_t) pad].hit = juce::jlimit (0, (int) meta[(size_t) pad].hits.size(), hit);
    rebuild (pad);
}

int KupataProcessor::hitsToPads (int pad)
{
    if (! juce::isPositiveAndBelow (pad, numPads)) return 0;
    const Meta source = meta[(size_t) pad];   // a copy: the pads are about to change
    const int numHits = (int) source.hits.size();
    if (source.sample == nullptr || numHits < 2) return 0;
    const auto routing = padInfo (pad).routing;
    int filled = 0;
    for (int k = 1; k <= numHits && pad + k - 1 < numPads; ++k)
    {
        const int target = pad + k - 1;
        Meta m = source;
        m.hit = k;
        meta[(size_t) target] = std::move (m);
        rebuild (target);
        if (target != pad)
        {
            resetKnobs (target);
            auto r = padInfo (target).routing;
            r.out = routing.out; r.stereo = routing.stereo;
            setRouting (target, r);
        }
        ++filled;
    }
    return filled;
}

void KupataProcessor::setRouting (int pad, const PadRouting& r)
{
    if (! juce::isPositiveAndBelow (pad, numPads)) return;
    {
        const juce::SpinLock::ScopedLockType lock (engineLock);
        auto& d = engine.routing[(size_t) pad];
        d.note = juce::jlimit (0, 127, r.note);
        d.out = juce::jlimit (0, maxOutputs - 1, r.out);
        d.stereo = r.stereo;
        d.choke = juce::jlimit (0, numChokes, r.choke);
    }
    ++version;
}

void KupataProcessor::setNotePreset (int preset)
{
    for (int p = 0; p < numPads; ++p)
    {
        auto r = padInfo (p).routing;
        r.note = presetNote (preset, p);
        setRouting (p, r);
    }
}

void KupataProcessor::setMultiOut (bool on, int channels)
{
    const int outs = channels > 0 ? channels : numOutputs();
    for (int p = 0; p < numPads; ++p)
    {
        auto r = padInfo (p).routing;
        r.stereo = true;
        r.out = on && (p + 1) * 2 <= outs ? p * 2 : 0;
        setRouting (p, r);
    }
}

void KupataProcessor::setParam (const juce::String& id, float value)
{
    if (auto* param = apvts.getParameter (id))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 (value));
        param->endChangeGesture();
    }
}

void KupataProcessor::resetKnobs (int pad)
{
    for (const char* b : paramBases)
        if (auto* param = apvts.getParameter (padId (pad, b)))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->getDefaultValue());
            param->endChangeGesture();
        }
}

void KupataProcessor::setPadParams (int pad, const std::map<juce::String, float>& values)
{
    for (const auto& [base, v] : values) setParam (padId (pad, base), v);
}

void KupataProcessor::movePad (int from, int to, bool copy)
{
    if (! juce::isPositiveAndBelow (from, numPads) || ! juce::isPositiveAndBelow (to, numPads) || from == to) return;
    auto values = [this] (int pad) {
        std::map<juce::String, float> v;
        for (const char* b : paramBases)
            if (auto* param = apvts.getParameter (padId (pad, b))) v[b] = param->convertFrom0to1 (param->getValue());
        return v;
    };
    const auto vFrom = values (from), vTo = values (to);
    const Meta mFrom = meta[(size_t) from], mTo = meta[(size_t) to];
    auto rFrom = padInfo (from).routing, rTo = padInfo (to).routing;
    const auto sFrom = padInfo (from).sound, sTo = padInfo (to).sound;

    meta[(size_t) to] = mFrom;
    setPadSound (to, sFrom);
    setPadParams (to, vFrom);
    rFrom.note = rTo.note;   // the notes stay where they are: the sound moves to the other key
    setRouting (to, rFrom);
    if (! copy)
    {
        meta[(size_t) from] = mTo;
        setPadSound (from, sTo);
        setPadParams (from, vTo);
        rTo.note = padInfo (from).routing.note;
        setRouting (from, rTo);
    }
}

bool KupataProcessor::pollLearn()
{
    const int pad = learnedFor.exchange (-1);
    const int note = learnedNote.exchange (-1);
    if (pad < 0 || note < 0) return false;
    auto r = padInfo (pad).routing;
    r.note = note;
    setRouting (pad, r);
    return true;
}

void KupataProcessor::playPad (int pad, float velocity)
{
    if (juce::isPositiveAndBelow (pad, numPads)) uiHit[(size_t) pad] = juce::jlimit (0.05f, 1.0f, velocity);
}

void KupataProcessor::releasePad (int pad)
{
    if (juce::isPositiveAndBelow (pad, numPads)) uiRelease[(size_t) pad] = true;
}

bool KupataProcessor::previewFile (const juce::File& file)
{
    juce::String error;
    auto smp = readAudio (std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (file)), error);
    if (smp == nullptr) return false;
    const auto mono = analysis::mono (*smp);
    auto snd = analysis::makeSound (smp, mono, 0, smp->length());
    std::shared_ptr<const Sound> old;
    {
        const juce::SpinLock::ScopedLockType lock (engineLock);
        old = engine.takePreview();
        engine.setPreview (snd);
    }
    return true;
}

void KupataProcessor::stopPreview()
{
    std::shared_ptr<const Sound> old;
    const juce::SpinLock::ScopedLockType lock (engineLock);
    old = engine.takePreview();
}

void KupataProcessor::renderPicture (int pad, std::vector<float>& dest, int maxFrames)
{
    dest.clear();
    const auto info = padInfo (pad);
    if (info.sound == nullptr) return;
    Engine::renderOffline (*info.sound, padParams (pad), info.sound->sample->sampleRate, maxFrames, dest);
}

//==============================================================================
Lang KupataProcessor::getLanguage() const
{
    return (int) apvts.state.getProperty ("lang", 0) == 1 ? Lang::es : Lang::en;
}

void KupataProcessor::setLanguage (Lang lang)
{
    apvts.state.setProperty ("lang", lang == Lang::es ? 1 : 0, nullptr);
    ++version;
}

//==============================================================================
juce::ValueTree KupataProcessor::savePad (int pad, bool forKit) const
{
    const auto& m = meta[(size_t) pad];
    juce::ValueTree t ("PAD");
    t.setProperty ("index", pad, nullptr);
    PadRouting r;
    {
        const juce::SpinLock::ScopedLockType lock (engineLock);
        r = engine.routing[(size_t) pad];
    }
    t.setProperty ("note", r.note, nullptr);
    t.setProperty ("out", r.out, nullptr);
    t.setProperty ("stereo", r.stereo, nullptr);
    t.setProperty ("choke", r.choke, nullptr);
    if (m.missing && m.saved.isValid())
    {
        for (const auto* p : { "name", "path", "hit", "audio" })
            if (m.saved.hasProperty (p)) t.setProperty (p, m.saved.getProperty (p), nullptr);
        return t;
    }
    if (m.sample == nullptr) return t;
    t.setProperty ("hit", m.hit, nullptr);
    if (m.factory >= 0) { t.setProperty ("factory", m.factory, nullptr); return t; }
    t.setProperty ("name", m.name, nullptr);
    t.setProperty ("path", m.path, nullptr);
    if (m.embedded.getSize() > 0) t.setProperty ("audio", m.embedded.toBase64Encoding(), nullptr);
    else if (forKit) t.setProperty ("audio", encode (*m.sample).toBase64Encoding(), nullptr);
    return t;
}

void KupataProcessor::restorePad (int pad, const juce::ValueTree& t)
{
    PadRouting r;
    r.note = t.getProperty ("note", firstNote + pad);
    r.out = t.getProperty ("out", 0);
    r.stereo = t.getProperty ("stereo", true);
    r.choke = t.getProperty ("choke", 0);
    setRouting (pad, r);

    const int hit = t.getProperty ("hit", 0);
    if (t.hasProperty ("factory"))
    {
        loadFactory (pad, t.getProperty ("factory"));
        setHit (pad, hit);
        return;
    }
    const juce::String path = t.getProperty ("path").toString();
    std::shared_ptr<Sample> smp;
    juce::String error;
    juce::MemoryBlock audio;
    if (t.hasProperty ("audio") && audio.fromBase64Encoding (t.getProperty ("audio").toString()))
        smp = readMemory (audio.getData(), audio.getSize(), error);   // the session's own copy comes first
    if (smp == nullptr && path.isNotEmpty() && juce::File (path).existsAsFile())
        smp = readAudio (std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (juce::File (path))), error);
    Meta m;
    m.name = t.getProperty ("name").toString();
    m.path = path;
    m.hit = hit;
    if (smp == nullptr)
    {
        if (path.isEmpty() && m.name.isEmpty()) { clearPad (pad); return; }
        m.missing = true;
        m.saved = t.createCopy();
        meta[(size_t) pad] = std::move (m);
        setPadSound (pad, nullptr);
        return;
    }
    m.sample = smp;
    if (audio.getSize() > 0 && smp->length() <= (int) (smp->sampleRate * embedSeconds)) m.embedded = audio;
    else if (smp->length() <= (int) (smp->sampleRate * embedSeconds)) m.embedded = encode (*smp);
    install (pad, std::move (m));
}

void KupataProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("version", JucePlugin_VersionString, nullptr);
    state.removeChild (state.getChildWithName ("PADS"), nullptr);
    juce::ValueTree pads ("PADS");
    for (int p = 0; p < numPads; ++p) pads.appendChild (savePad (p, false), nullptr);
    state.appendChild (pads, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void KupataProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto state = juce::ValueTree::fromXml (*xml);
    auto pads = state.getChildWithName ("PADS").createCopy();
    state.removeChild (state.getChildWithName ("PADS"), nullptr);
    apvts.replaceState (state);
    if (! apvts.state.hasProperty ("lang")) apvts.state.setProperty ("lang", 0, nullptr);
    if (pads.isValid())
    {
        std::array<bool, numPads> seen {};
        for (const auto& t : pads)
        {
            const int i = t.getProperty ("index", -1);
            if (! juce::isPositiveAndBelow (i, numPads)) continue;
            restorePad (i, t);
            seen[(size_t) i] = true;
        }
        for (int i = 0; i < numPads; ++i) if (! seen[(size_t) i]) clearPad (i);
    }
    ++version;
}

//==============================================================================
juce::File KupataProcessor::kitsFolder() const
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Kupata").getChildFile ("Kits");
}

bool KupataProcessor::saveKit (const juce::File& file, const juce::String& name)
{
    juce::ValueTree kit ("KUPATAKIT");
    kit.setProperty ("name", name, nullptr);
    kit.setProperty ("version", JucePlugin_VersionString, nullptr);
    for (int p = 0; p < numPads; ++p)
    {
        auto t = savePad (p, true);
        juce::ValueTree knobs ("KNOBS");
        for (const char* b : paramBases)
            if (auto* param = apvts.getParameter (padId (p, b))) knobs.setProperty (b, param->convertFrom0to1 (param->getValue()), nullptr);
        t.appendChild (knobs, nullptr);
        kit.appendChild (t, nullptr);
    }
    file.getParentDirectory().createDirectory();
    auto xml = kit.createXml();
    if (xml == nullptr || ! xml->writeTo (file)) return false;
    apvts.state.setProperty ("kitName", name, nullptr);
    ++version;
    return true;
}

bool KupataProcessor::loadKit (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName ("KUPATAKIT")) return false;
    const auto kit = juce::ValueTree::fromXml (*xml);
    std::array<bool, numPads> seen {};
    for (const auto& t : kit)
    {
        const int i = t.getProperty ("index", -1);
        if (! juce::isPositiveAndBelow (i, numPads)) continue;
        restorePad (i, t);
        resetKnobs (i);
        const auto knobs = t.getChildWithName ("KNOBS");
        std::map<juce::String, float> v;
        for (int k = 0; k < knobs.getNumProperties(); ++k)
        {
            const auto id = knobs.getPropertyName (k).toString();
            v[id] = (float) knobs.getProperty (knobs.getPropertyName (k));
        }
        setPadParams (i, v);
        seen[(size_t) i] = true;
    }
    for (int i = 0; i < numPads; ++i) if (! seen[(size_t) i]) { clearPad (i); resetKnobs (i); }
    const auto name = kit.getProperty ("name").toString();
    apvts.state.setProperty ("kitName", name.isNotEmpty() ? name : file.getFileNameWithoutExtension(), nullptr);
    ++version;
    return true;
}

//==============================================================================
void KupataProcessor::timerCallback()
{
    if (reaper.available()) syncReaper();
}

void KupataProcessor::syncReaper()
{
    const int channels = reaper.trackChannels();
    if (channels != reaperChannels.load()) { reaperChannels = channels; ++version; }
    const auto routes = reaper.routes();
    bool multi = false;   // something receives from channel 3 on: there is multi-output routing
    for (const auto& r : routes) multi = multi || r.channel >= 2;
    reaperHasRoutes = multi;
    if (! multi) return;

    // REAPER has built multi-output routing: the first time, each pad goes to its own pair
    if (! (bool) apvts.state.getProperty ("reaperMultiOut", false))
    {
        apvts.state.setProperty ("reaperMultiOut", true, nullptr);
        bool allOnFirstPair = true;
        for (int p = 0; p < numPads; ++p)
        {
            const auto r = padInfo (p).routing;
            allOnFirstPair = allOnFirstPair && r.out == 0 && r.stereo;
        }
        if (allOnFirstPair) setMultiOut (true, channels);
    }

    // the tracks that receive a pad take its name, and keep it in step
    const auto lang = getLanguage();
    for (const auto& route : routes)
    {
        int pad = -1;
        for (int p = 0; p < numPads && pad < 0; ++p)
            if (padInfo (p).routing.out == route.channel) pad = p;
        if (pad < 0) continue;
        auto wanted = padName (pad, lang);
        if (wanted.isEmpty()) wanted = "Pad " + juce::String (pad + 1);
        const auto name = reaper.trackName (route.track);
        const auto mark = reaper.trackMark (route.track);
        const bool ours = mark.isNotEmpty() && name == mark;   // named by Kupata and not renamed since
        // made by REAPER's routing: no name, or one taken from Kupata's outputs. On 1-2, where an
        // ordinary send (to a reverb, say) also starts, only a name that says Kupata
        const bool fresh = mark.isEmpty() && (name.containsIgnoreCase ("kupata")
                                              || (route.channel >= 2 && name.containsOnly ("0123456789/ -:()")));
        if ((ours || fresh) && name != wanted)
        {
            reaper.setTrackName (route.track, wanted);
            reaper.setTrackMark (route.track, wanted);
        }
        else if (fresh) reaper.setTrackMark (route.track, wanted);
    }
}

int KupataProcessor::buildReaperTracks()
{
    if (! reaper.available() || reaperHasRoutes.load()) return 0;
    const auto lang = getLanguage();
    juce::StringArray names;
    for (int p = 0; p < numPads; ++p)
    {
        auto n = padName (p, lang);
        names.add (n.isNotEmpty() ? n : "Pad " + juce::String (p + 1));
    }
    const int made = reaper.buildRouting (names, lang == Lang::es ? "Kupata: una pista por pad" : "Kupata: a track per pad");
    if (made > 0)
    {
        apvts.state.setProperty ("reaperMultiOut", true, nullptr);
        reaperChannels = made * 2;
        setMultiOut (true, made * 2);
        reaperHasRoutes = true;
        ++version;
    }
    return made;
}

juce::AudioProcessorEditor* KupataProcessor::createEditor() { return new KupataEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new KupataProcessor(); }
