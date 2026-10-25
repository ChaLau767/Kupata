// Kupata — parameters and interface texts
// Copyright (C) 2026 Lautaro Martín Vieyra
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <cstring>
#include "KupataEngine.h"

namespace kupata
{

// Source texts are UTF-8: always convert them with this.
inline juce::String u8 (const char* text) { return juce::String::fromUTF8 (text); }

enum class Lang { en = 0, es = 1 };

// toUpperCase() does not change accented letters: finish the job for Spanish.
inline juce::String caps (const juce::String& t)
{
    auto r = t.toUpperCase();
    const char* pairs[][2] { { "á", "Á" }, { "é", "É" }, { "í", "Í" }, { "ó", "Ó" }, { "ú", "Ú" }, { "ñ", "Ñ" }, { "ü", "Ü" } };
    for (auto& pr : pairs) r = r.replace (juce::String::fromUTF8 (pr[0]), juce::String::fromUTF8 (pr[1]));
    return r;
}

inline juce::String localNumber (juce::String t, Lang lang) { return lang == Lang::es ? t.replaceCharacter ('.', ',') : t; }

//==============================================================================
struct FloatSpec
{
    const char* id;
    const char* en;
    const char* es;
    float min, max, def;
    const char* unit;
    int decimals;
    enum Scale { linear, volume } scale;
    bool bipolar;
    const char* noteEn;
    const char* noteEs;
    float interval = 0.0f;
};

// The knobs of every pad (host parameters "p1_shape" … "p16_end").
inline const std::vector<FloatSpec>& padSpecs()
{
    using S = FloatSpec;
    static const std::vector<FloatSpec> specs {
        { "shape",  "Shape",  "Forma",     -100.0f, 100.0f, 0.0f,   "",   0, S::linear, true,
          "Left: shorter, down to a click. Right: more sustain, and past the middle a softer attack. The loudness stays",
          "Izquierda: más corto, hasta un clic. Derecha: más sostenido y, pasada la mitad, con ataque más suave. El volumen se mantiene" },
        { "tune",   "Tune",   "Afinación", -24.0f,  24.0f,  0.0f,   "st", 1, S::linear, true,
          "Pitch in semitones (the sound gets shorter or longer, as on a tape)", "Altura en semitonos (el sonido se acorta o se alarga, como en una cinta)", 0.01f },
        { "tone",   "Tone",   "Tono",      -100.0f, 100.0f, 0.0f,   "",   0, S::linear, true,
          "Left: darker. Right: brighter and thinner. It adapts to how bright the sound is", "Izquierda: más oscuro. Derecha: más brillante y delgado. Se adapta al brillo del sonido" },
        { "comp",   "Comp",   "Compresión", 0.0f,   100.0f, 0.0f,   "%",  0, S::linear, false,
          "Punchier, with more sustain. The loudness stays", "Más pegada y más sostén. El volumen se mantiene" },
        { "volume", "Volume", "Volumen",   -60.0f,  6.0f,   0.0f,   "dB", 1, S::volume, false, "", "" },
        { "pan",    "Pan",    "Paneo",     -100.0f, 100.0f, 0.0f,   "",   0, S::linear, true,  "", "", 1.0f },
        { "start",  "Start",  "Inicio",    0.0f,    100.0f, 0.0f,   "%",  1, S::linear, false,
          "Where the sound starts (drag the marker on the waveform)", "Dónde empieza el sonido (arrastrá el marcador en la forma de onda)" },
        { "end",    "End",    "Final",     0.0f,    100.0f, 100.0f, "%",  1, S::linear, false,
          "Where the sound ends (drag the marker on the waveform)", "Dónde termina el sonido (arrastrá el marcador en la forma de onda)" },
    };
    return specs;
}

inline const std::vector<FloatSpec>& globalSpecs()
{
    using S = FloatSpec;
    static const std::vector<FloatSpec> specs {
        { "gain",     "Output",   "Salida",    -36.0f, 12.0f,  0.0f,  "dB", 1, S::linear, true, "Output level of the whole kit", "Nivel de salida de todo el kit" },
        { "velocity", "Velocity", "Velocidad", 0.0f,   100.0f, 70.0f, "%",  0, S::linear, false,
          "How much the strength of each note changes its level. 0 %: every note at full level", "Cuánto cambia el nivel según la fuerza de cada nota. 0 %: todas las notas al máximo" },
    };
    return specs;
}

inline juce::String padId (int pad, const juce::String& base) { return "p" + juce::String (pad + 1) + "_" + base; }
inline juce::String baseId (const juce::String& id) { return id.startsWith ("p") && id.containsChar ('_') ? id.fromFirstOccurrenceOf ("_", false, false) : id; }
inline int padOfId (const juce::String& id) { return id.startsWith ("p") && id.containsChar ('_') ? id.substring (1).getIntValue() - 1 : -1; }

inline const FloatSpec& spec (const juce::String& id)
{
    const auto b = baseId (id);
    for (const auto& s : padSpecs()) if (b == s.id) return s;
    for (const auto& s : globalSpecs()) if (b == s.id) return s;
    jassertfalse;
    return padSpecs().front();
}

// Value text. "C", "L", "R" and "-inf" are translated by the interface.
inline juce::String formatValue (const FloatSpec& s, float v)
{
    const juce::String id (s.id), unit (s.unit);
    if (id == "volume" && v <= -59.9f) return "-inf";
    if (id == "pan")
    {
        const int p = juce::roundToInt (v);
        return p == 0 ? juce::String ("C") : (p < 0 ? "L" + juce::String (-p) : "R" + juce::String (p));
    }
    if ((id == "shape" || id == "tone") && juce::roundToInt (v) == 0) return "0";
    juce::String t = (s.decimals == 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, s.decimals)) + (unit.isEmpty() ? "" : " " + unit);
    if (s.bipolar && v > 0.0f && (s.decimals > 0 ? std::abs (v) >= 0.05f : juce::roundToInt (v) != 0)) t = "+" + t;
    return t;
}

inline float parseValue (const FloatSpec& s, juce::String t)
{
    t = t.trim().replaceCharacter (',', '.');
    const juce::String id (s.id);
    if (id == "volume" && (t.startsWithIgnoreCase ("-inf") || t.startsWithIgnoreCase ("inf"))) return s.min;
    if (id == "pan")
    {
        if (t.startsWithIgnoreCase ("c")) return 0.0f;
        if (t.startsWithIgnoreCase ("l") || t.startsWithIgnoreCase ("i")) return -t.substring (1).getFloatValue();
        if (t.startsWithIgnoreCase ("r") || t.startsWithIgnoreCase ("d")) return t.substring (1).getFloatValue();
    }
    return t.getFloatValue();
}

inline juce::NormalisableRange<float> rangeFor (const FloatSpec& s)
{
    juce::NormalisableRange<float> r (s.min, s.max, s.interval);
    if (s.scale == FloatSpec::volume) r.setSkewForCentre (-12.0f);
    return r;
}

inline const juce::StringArray& chokeChoices()
{
    static const juce::StringArray c = [] { juce::StringArray a { "Off" }; for (int i = 1; i <= numChokes; ++i) a.add (juce::String (i)); return a; } ();
    return c;
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto floatParam = [] (const juce::String& id, const juce::String& name, const FloatSpec& s) {
        const auto sp = s;
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name, rangeFor (s), s.def,
            juce::AudioParameterFloatAttributes()
                .withLabel (s.unit)
                .withStringFromValueFunction ([sp] (float v, int) { return formatValue (sp, v); })
                .withValueFromStringFunction ([sp] (const juce::String& t) { return parseValue (sp, t); }));
    };
    for (const auto& s : globalSpecs()) layout.add (floatParam (s.id, u8 (s.en), s));
    for (int p = 0; p < numPads; ++p)
    {
        const juce::String prefix = "P" + juce::String (p + 1) + " ";
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("pad" + juce::String (p + 1), "Pad " + juce::String (p + 1), "|");
        for (const auto& s : padSpecs()) group->addChild (floatParam (padId (p, s.id), prefix + u8 (s.en), s));
        group->addChild (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { padId (p, "gate"), 1 }, prefix + "Gate", false));
        group->addChild (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { padId (p, "mute"), 1 }, prefix + "Mute", false));
        group->addChild (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { padId (p, "solo"), 1 }, prefix + "Solo", false));
        group->addChild (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { padId (p, "mono"), 1 }, prefix + "Mono", true));
        group->addChild (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { padId (p, "reverse"), 2 }, prefix + "Reverse", false));
        layout.add (std::move (group));
    }
    return layout;
}

//==============================================================================
// The factory sounds (Resources/kit, in the same order: sessions store this number) and the kit
// they make: which of them goes on each pad. Sounds can be appended, never moved.
struct FactorySound { const char* file; const char* en; const char* es; int choke; };
constexpr int numFactorySounds = 19;
inline const std::array<FactorySound, numFactorySounds>& factorySounds()
{
    static const std::array<FactorySound, numFactorySounds> sounds { {
        { "01-bombo-largo",      "Kick long",    "Bombo largo",      0 },
        { "02-bombo-corto",      "Kick short",   "Bombo corto",      0 },
        { "03-tambor",           "Snare",        "Tambor",           0 },
        { "04-tambor-brillante", "Snare bright", "Tambor brillante", 0 },
        { "05-palmas",           "Clap",         "Palmas",           0 },
        { "06-aro",              "Rimshot",      "Aro",              0 },
        { "07-hihat-cerrado",    "Closed hat",   "Hihat cerrado",    1 },
        { "08-hihat-abierto",    "Open hat",     "Hihat abierto",    1 },
        { "09-platillo",         "Cymbal",       "Platillo",         0 },
        { "10-tom-grave",        "Low tom",      "Tom grave",        0 },
        { "11-tom-medio",        "Mid tom",      "Tom medio",        0 },
        { "12-tom-agudo",        "High tom",     "Tom agudo",        0 },
        { "13-cencerro",         "Cowbell",      "Cencerro",         0 },
        { "14-clave",            "Clave",        "Clave",            0 },
        { "15-maracas",          "Maracas",      "Maracas",          0 },
        { "16-bombo-sub",        "Sub kick",     "Bombo sub",        0 },
        { "17-conga-grave",      "Low conga",    "Conga grave",      0 },
        { "18-conga-media",      "Mid conga",    "Conga media",      0 },
        { "19-conga-aguda",      "High conga",   "Conga aguda",      0 },
    } };
    return sounds;
}

// The analog kit, pad 1 (bottom left) to 16, in the order of Sitala's 808 kit, so that the same pad
// (and, with multi-out, the same outputs) holds the same instrument: kick, snare, closed and open hat,
// cymbal, three toms, three congas, clap, clave, maracas, cowbell, rimshot. Bank B starts empty (-1).
inline int factoryLayout (int pad)
{
    static const int layout[bankSize] { 0, 2, 6, 7,  8, 9, 10, 11,  16, 17, 18, 4,  13, 14, 12, 5 };
    return juce::isPositiveAndBelow (pad, bankSize) ? layout[pad] : -1;
}

// Note maps for the whole kit (both banks).
enum NoteMap { mapChromatic = 0, mapWhite, mapGeneralMidi, numNoteMaps };
inline int presetNote (int preset, int pad, int base = firstNote)
{
    pad = juce::jlimit (0, numPads - 1, pad);
    if (preset == mapGeneralMidi)
    {
        // the same notes as Sitala's 808 kit (the kit starts with them, so MIDI parts written for
        // Sitala play the same instruments); bank B goes on from C5
        static const int gm[bankSize] { 36, 38, 42, 46, 49, 45, 47, 50, 64, 62, 63, 39, 75, 70, 56, 37 };
        return pad < bankSize ? gm[pad] : 72 + pad - bankSize;
    }
    if (preset == mapWhite)
    {
        static const int steps[7] { 0, 2, 4, 5, 7, 9, 11 };
        return juce::jlimit (0, 127, 36 + 12 * (pad / 7) + steps[pad % 7]);
    }
    return juce::jlimit (0, 127, base + pad);
}

//==============================================================================
inline juce::String tr (Lang lang, const char* key)
{
    struct Entry { const char* key; const char* en; const char* es; };
    static const Entry table[] {
        { "subtitle",     "drum sampler",                     "sampler de percusión" },
        { "factoryKit",   "Analog kit",                       "Kit analógico" },
        { "kit",          "Kit",                              "Kit" },
        { "kitTip",       "Load or save a whole kit: its sounds and every knob", "Cargar o guardar un kit entero: sus sonidos y todas las perillas" },
        { "loadKit",      "Load kit from a file…",            "Cargar un kit desde un archivo…" },
        { "saveKit",      "Save kit…",                        "Guardar el kit…" },
        { "yourKits",     "Your kits",                        "Tus kits" },
        { "openKitsFolder", "Show the kits folder",           "Mostrar la carpeta de kits" },
        { "noKits",       "(no saved kits yet)",              "(todavía no guardaste kits)" },
        { "kitName",      "Name of the kit",                  "Nombre del kit" },
        { "save",         "Save",                             "Guardar" },
        { "cancel",       "Cancel",                           "Cancelar" },
        { "untitled",     "Untitled kit",                     "Kit sin nombre" },
        { "kitError",     "This kit could not be read",       "No pude leer este kit" },
        { "empty",        "Empty",                            "Vacío" },
        { "dropHere",     "Drop audio files here, or on a pad", "Soltá archivos de audio acá o sobre un pad" },
        { "noSounds",     "No sounds in this folder. Choose another one below, or drop files on the pads", "No hay sonidos en esta carpeta. Elegí otra abajo, o soltá archivos sobre los pads" },
        { "emptyPadTip",  "Drop a sound here, or right-click to load one", "Soltá un sonido acá, o clic derecho para cargarlo" },
        { "padTip",       "Click: play and select · Drag: move to another pad (Alt: copy) · Right-click: more", "Clic: tocar y elegir · Arrastrar: mover a otro pad (Alt: copiar) · Clic derecho: más" },
        { "missing",      "File not found",                   "No encuentro el archivo" },
        { "locate",       "Locate the file…",                 "Buscar el archivo…" },
        { "load",         "Load a sound…",                    "Cargar un sonido…" },
        { "factorySound", "Factory sound",                    "Sonido de fábrica" },
        { "clear",        "Clear pad",                        "Vaciar el pad" },
        { "learn",        "Learn its note (play a key)",      "Aprender su nota (tocá una tecla)" },
        { "learning",     "Play a key…",                      "Tocá una tecla…" },
        { "resetKnobs",   "Reset its knobs",                  "Volver las perillas al inicio" },
        { "copyPad",      "Copy pad",                         "Copiar el pad" },
        { "pastePad",     "Paste pad",                        "Pegar el pad" },
        { "hits",         "hits",                             "golpes" },
        { "hit",          "Hit",                              "Golpe" },
        { "whole",        "Whole",                            "Entero" },
        { "hitTip",       "This sound has several hits: play one of them, or the whole sound", "Este sonido tiene varios golpes: tocá uno de ellos, o el sonido entero" },
        { "sliceToPads",  "Hits to pads",                     "Golpes a pads" },
        { "sliceTip",     "Puts each hit on its own pad, from this one on", "Pone cada golpe en su propio pad, desde este en adelante" },
        { "gate",         "Gate",                             "Gate" },
        { "gateTip",      "On: the sound stops when the note is released. Off: it always plays to its end", "Encendido: el sonido se corta al soltar la nota. Apagado: siempre suena hasta el final" },
        { "mute",         "Mute",                             "Silencio" },
        { "solo",         "Solo",                             "Solo" },
        { "mono",         "Mono",                             "Mono" },
        { "monoTip",      "On: a new hit cuts the one still sounding, as in Sitala. Off: the hits overlap", "Encendido: un golpe nuevo corta al que todavía suena, como en Sitala. Apagado: los golpes se superponen" },
        { "choke",        "Choke",                            "Corte" },
        { "chokeTip",     "Pads in the same group cut each other off, as a closed hi-hat cuts the open one", "Los pads del mismo grupo se cortan entre sí, como el hi-hat cerrado corta al abierto" },
        { "off",          "Off",                              "No" },
        { "out",          "Out",                              "Salida" },
        { "outTip",       "Output channel. Give the track more channels in your DAW (up to 64) to use them", "Canal de salida. Para usar más, dale más canales a la pista en tu DAW (hasta 64)" },
        { "monoShort",    "M",                                "M" },
        { "stereoShort",  "ST",                               "EST" },
        { "stereoTip",    "M: the pad goes to one channel. ST: to a pair of channels, with its pan", "M: el pad sale por un canal. EST: por un par de canales, con su paneo" },
        { "note",         "Note",                             "Nota" },
        { "noteTip",      "The MIDI note that plays this pad. Drag up or down, or right-click a pad: learn", "La nota MIDI que toca este pad. Arrastrá hacia arriba o abajo, o clic derecho en un pad: aprender" },
        { "outputs",      "outputs",                          "salidas" },
        { "outputOne",    "output",                           "salida" },
        { "multiOut",     "Multi-out",                        "Multisalida" },
        { "multiOutTip",  "Each pad to its own pair of outputs (1-2, 3-4…), as far as the track has channels", "Cada pad a su propio par de salidas (1-2, 3-4…), hasta donde alcancen los canales de la pista" },
        { "allTo12",      "Every pad to 1-2",                 "Todos los pads a 1-2" },
        { "reaperBuild",  "Create a track per pad in REAPER", "Crear una pista por pad en REAPER" },
        { "reaperDone",   "REAPER already has a track per pad", "REAPER ya tiene una pista por pad" },
        { "reaperFailed", "REAPER did not let Kupata create the tracks", "REAPER no dejó que Kupata creara las pistas" },
        { "multiOutReaperTip", "Each pad to its own output. In REAPER, Kupata can also create a track per pad, named after it", "Cada pad a su propia salida. En REAPER, Kupata también puede crear una pista por pad, con su nombre" },
        { "notes",        "Notes",                            "Notas" },
        { "notesTip",     "Which keys play the pads. Each pad shows its note in its top right corner", "Qué teclas tocan los pads. Cada pad muestra su nota arriba a la derecha" },
        { "mapChromatic", "In a row",                         "Seguidas" },
        { "mapWhite",     "White keys",                       "Teclas blancas" },
        { "mapGm",        "Sitala (GM)",                      "Sitala (GM)" },
        { "mapCustom",    "Your own",                         "Propias" },
        { "chromatic",    "In a row, from C2: pad 1 C2, pad 2 C#2…", "Seguidas, desde Do2: pad 1 Do2, pad 2 Do#2…" },
        { "fromKey",      "In a row, from the next key you play", "Seguidas, desde la próxima tecla que toques" },
        { "fromKeyWait",  "Play the key for pad 1…",          "Tocá la tecla del pad 1…" },
        { "whiteKeys",    "White keys, from C2",              "Teclas blancas, desde Do2" },
        { "generalMidi",  "Sitala / General MIDI drums: kick C2, snare D2, hat F#2…", "Sitala / batería General MIDI: bombo Do2, tambor Re2, hihat Fa#2…" },
        { "selectByMidi", "A pad played by MIDI becomes the selected one", "El pad que tocás por MIDI queda elegido" },
        { "kitByProgram", "Program Change switches kits (0: analog kit, 1 on: your kits)", "Program Change cambia de kit (0: kit analógico, 1 en adelante: tus kits)" },
        { "midiIn",       "MIDI",                             "MIDI" },
        { "midiTip",      "The last note that reached Kupata, and how strong. If a key does not play a pad, its note is not in the map: see Notes", "La última nota que le llegó a Kupata, y con qué fuerza. Si una tecla no toca ningún pad, su nota no está en el mapa: mirá Notas" },
        { "noPad",        "no pad",                           "ningún pad" },
        { "bank",         "Bank",                             "Banco" },
        { "bankTip",      "Two banks of 16 pads: A and B. Bank B plays on notes of its own and, with multi-out, on outputs 33-64", "Dos bancos de 16 pads: A y B. El B se toca con sus propias notas y, con multisalida, sale por 33-64" },
        { "reverse",      "Reverse",                          "Reversa" },
        { "playsBackwards", "plays backwards, from End to Start", "suena al revés, de Final a Inicio" },
        { "reverseTip",   "The sound plays from its end to its start", "El sonido suena desde el final hacia el principio" },
        { "tuneSnapTip",  "On sounds with a clear pitch, Tune stops on exact notes. Hold Shift to tune freely", "En los sonidos con altura definida, Afinación se detiene en notas exactas. Con Mayúsculas, afinación libre" },
        { "browser",      "Sounds",                           "Sonidos" },
        { "browserTip",   "Click: listen · Double-click or drag onto a pad: load", "Clic: escuchar · Doble clic o arrastrar a un pad: cargar" },
        { "chooseFolder", "Choose a folder…",                 "Elegir una carpeta…" },
        { "up",           "Up",                               "Subir" },
        { "home",         "Home",                             "Inicio" },
        { "openFile",     "Choose an audio file",             "Elegí un archivo de audio" },
        { "dblclick",     "Double-click: default value",      "Doble clic: valor inicial" },
        { "velocityShort", "vel.",                            "vel." },
        { "waveTip",      "Drag the two markers to trim the sound. The light shape is the sound as it plays now", "Arrastrá los dos marcadores para recortar el sonido. La forma clara es el sonido tal como suena ahora" },
        { "pitchOf",      "pitch",                            "altura" },
        { "noPitch",      "no clear pitch",                   "sin altura definida" },
        { "seconds",      "s",                                "s" },
        { "ms",           "ms",                               "ms" },
        { "unsupported",  "Unsupported or unreadable file",   "Archivo no compatible o ilegible" },
        { "tooShort",     "The file is too short",            "El archivo es demasiado corto" },
        { "tooLong",      "Only the first 60 seconds are used", "Se usan solo los primeros 60 segundos" },
    };
    for (const auto& e : table)
        if (std::strcmp (e.key, key) == 0) return u8 (lang == Lang::es ? e.es : e.en);
    return key;
}

inline juce::String localValue (juce::String t, Lang lang)
{
    if (t == "-inf") return juce::String::fromUTF8 ("−∞");
    if (lang == Lang::es && t.length() > 1 && juce::CharacterFunctions::isDigit (t[1]))
    {
        if (t.startsWith ("L")) return "I" + t.substring (1);
        if (t.startsWith ("R")) return "D" + t.substring (1);
    }
    return localNumber (t, lang);
}

// Note names: English letters, or Spanish solfège. Middle C (60) is C4 / Do4.
inline juce::String noteName (int midi, Lang lang)
{
    static const char* en[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static const char* es[] { "Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si" };
    midi = juce::jlimit (0, 127, midi);
    return u8 ((lang == Lang::es ? es : en)[midi % 12]) + juce::String (midi / 12 - 1);
}

// The note nearest to a frequency, with how many cents it is off.
inline juce::String pitchName (float hz, Lang lang)
{
    if (hz <= 0.0f) return tr (lang, "noPitch");
    const float n = 69.0f + 12.0f * std::log2 (hz / 440.0f);
    const int note = juce::roundToInt (n);
    const int cents = juce::roundToInt ((n - (float) note) * 100.0f);
    return noteName (note, lang) + (cents == 0 ? juce::String() : (cents > 0 ? " +" : " ") + juce::String (cents) + " ct");
}

} // namespace kupata
