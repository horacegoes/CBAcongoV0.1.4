#pragma once
// =============================================================================
// PresetManager.h  -  factory presets + your own, saved as files in
//   <user application data>/CBA/Congo Filter/Presets/<name>.cfpreset
// A preset stores every parameter except the momentary buttons (TEST, KILL AUDIO,
// FREEZE). Anything missing from a preset (e.g. saved by an older version) goes to its
// default, so presets keep working across updates.
// =============================================================================
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>
#include <utility>
#include <vector>

namespace congo
{
class PresetManager
{
public:
    using Values = std::vector<std::pair<juce::String, float>>;

    explicit PresetManager (juce::AudioProcessorValueTreeState& s) : apvts (s)
    {
        buildFactory();
        loadUserFolder();
    }

    // factory first (in their order), then yours (alphabetical)
    juce::StringArray factoryNames() const { juce::StringArray n; for (auto& f : factory) n.add (f.first); return n; }
    juce::StringArray userNames() const    { juce::StringArray n; for (auto& u : user) n.add (u.first); return n; }
    juce::StringArray allNames() const     { auto n = factoryNames(); n.addArray (userNames()); return n; }

    bool isFactory (const juce::String& name) const { for (auto& f : factory) if (f.first == name) return true; return false; }
    bool exists (const juce::String& name) const    { return isFactory (name) || user.count (name) > 0; }

    juce::String current() const { return apvts.state.getProperty (juce::Identifier ("presetName"), "INIT").toString(); }

    const Values* find (const juce::String& name) const
    {
        for (auto& f : factory) if (f.first == name) return &f.second;
        auto it = user.find (name);
        return it != user.end() ? &it->second : nullptr;
    }

    bool load (const juce::String& name)
    {
        const Values* v = find (name);
        if (v == nullptr) return false;
        apply (*v, false);
        apvts.state.setProperty (juce::Identifier ("presetName"), name, nullptr);
        return true;
    }

    // step through the whole list (wraps round)
    juce::String step (int direction)
    {
        const auto names = allNames();
        if (names.isEmpty()) return {};
        int i = names.indexOf (current());
        i = (i < 0 ? 0 : (i + direction + names.size()) % names.size());
        load (names[i]);
        return names[i];
    }

    // save the current settings as one of your presets; returns the name used
    juce::String save (juce::String name)
    {
        name = clean (name);
        if (name.isEmpty()) name = "MY PRESET";
        const auto base = name;
        for (int n = 2; isFactory (name); ++n) name = base + " " + juce::String (n);   // factory stays untouched
        user[name] = capture();
        writeFile (name, user[name]);
        apvts.state.setProperty (juce::Identifier ("presetName"), name, nullptr);
        return name;
    }

    bool remove (const juce::String& name)
    {
        if (isFactory (name) || user.erase (name) == 0) return false;
        fileFor (name).deleteFile();
        return true;
    }

    static juce::File folder()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("CBA").getChildFile ("Congo Filter").getChildFile ("Presets");
    }

    // not stored in presets: the momentary buttons and the LOCK buttons themselves
    static bool isMomentary (const juce::String& id)
    {
        return id == "test" || id == "kill" || id == "freeze" || id == "frzHold"
            || id == "mixLock" || id == "masterLock" || id == "frzLock";
    }

    // RESET: back to the last save point - the preset last loaded or saved, exactly as it
    // was saved (INIT on a fresh launch). Locked settings are reset too.
    juce::String resetToLastSave()
    {
        auto name = current();
        if (! exists (name)) name = "INIT";
        const Values* v = find (name);
        apply (v != nullptr ? *v : Values {}, true);
        apvts.state.setProperty (juce::Identifier ("presetName"), name, nullptr);
        return name;
    }

private:
    static juce::String clean (const juce::String& n)
    {
        return n.trim().toUpperCase().retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_+./()#").substring (0, 32).trim();
    }
    static juce::File fileFor (const juce::String& name)
    {
        return folder().getChildFile (name.replaceCharacters ("/.", "__") + ".cfpreset");
    }

    Values capture() const
    {
        Values v;
        for (auto* prm : apvts.processor.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
                if (! isMomentary (r->getParameterID()))
                    v.push_back ({ r->getParameterID(), r->convertFrom0to1 (r->getValue()) });
        return v;
    }

    // Sets only what actually changes (a preset change used to send ~220 edits to the host,
    // which made switching feel sticky). LOCKed settings are kept unless resetting everything.
    void apply (const Values& v, bool ignoreLocks)
    {
        auto on = [this] (const char* id) { auto* p = apvts.getRawParameterValue (id); return p != nullptr && p->load() > 0.5f; };
        const bool mixLocked = ! ignoreLocks && on ("mixLock");
        const bool masterLocked = ! ignoreLocks && on ("masterLock");
        const bool holding = on ("freeze") && on ("frzHold");          // a held loop keeps its OUTPUT level
        for (auto* prm : apvts.processor.getParameters())
        {
            auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm);
            if (r == nullptr) continue;
            const auto id = r->getParameterID();
            if (isMomentary (id)) continue;
            if (mixLocked && id == "mix") continue;
            if (masterLocked && (id == "master" || id == "masterPan")) continue;
            if (holding && id == "output") continue;
            float value = r->convertFrom0to1 (r->getDefaultValue());
            for (auto& kv : v) if (kv.first == id) { value = kv.second;  break; }
            const float norm = r->convertTo0to1 (value);
            if (std::abs (norm - r->getValue()) > 1.0e-6f) r->setValueNotifyingHost (norm);
        }
    }

    void writeFile (const juce::String& name, const Values& v) const
    {
        juce::XmlElement root ("CONGOPRESET");
        root.setAttribute ("name", name);
        root.setAttribute ("version", "0.1.4");
        for (auto& kv : v)
        {
            auto* e = root.createNewChildElement ("PARAM");
            e->setAttribute ("id", kv.first);
            e->setAttribute ("value", (double) kv.second);
        }
        folder().createDirectory();
        root.writeTo (fileFor (name));
    }

    void loadUserFolder()
    {
        for (auto& f : folder().findChildFiles (juce::File::findFiles, false, "*.cfpreset"))
            if (auto xml = juce::parseXML (f))
            {
                const auto name = clean (xml->getStringAttribute ("name"));
                if (name.isEmpty() || isFactory (name)) continue;
                Values v;
                for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
                    v.push_back ({ e->getStringAttribute ("id"), (float) e->getDoubleAttribute ("value") });
                user[name] = v;
            }
    }

    // factory presets: only the settings that differ from the defaults
    void buildFactory()
    {
        factory = {
            { "INIT", {} },
            { "S950 JUNGLE 3X", { { "syncOn", 1 }, { "syncRatio", 13 }, { "retrig", 4 }, { "akai", 0 }, { "cutoff", 2400 },
                                  { "reso", 55 }, { "drive", 6 } } },
            { "HIGHER STATES", { { "mode", 1 }, { "time", 600 }, { "rv1_on", 1 }, { "rv1_mix", 22 } } },
            { "S2000 INTELL PAD", { { "model", 1 }, { "type", 1 }, { "bits", 16 }, { "rate", 44100 }, { "mode", 1 },
                                    { "time", 2000 }, { "quality", 20 }, { "xfd", 10 }, { "rv1_on", 1 }, { "rv1_mix", 45 },
                                    { "rv1_decay", 85 } } },
            { "MOOG SINGS", { { "samplerOn", 0 }, { "cutoff", 420 }, { "reso", 98 }, { "drive", 12 } } },
            { "C-SYNC STUTTER", { { "cycleSync", 1 }, { "cycleDiv", 6 }, { "time", 200 }, { "retrig", 4 } } },
            { "LOFI 8K 8BIT", { { "rate", 8000 }, { "bits", 8 }, { "cutoff", 3500 }, { "reso", 30 } } },
            { "DUB ECHO", { { "samplerOn", 0 }, { "fx1_on", 1 }, { "fx1_echoOn", 1 }, { "fx1_echoMode", 2 },
                            { "fx1_lDelay", 375 }, { "fx1_rDelay", 250 }, { "fx1_lFbk", 70 }, { "fx1_rFbk", 70 },
                            { "fx1_lDamp", 22 }, { "fx1_rDamp", 22 }, { "rv1_on", 1 }, { "rv1_mix", 20 } } },
            { "ROTARY ORGAN", { { "samplerOn", 0 }, { "fx1_on", 1 }, { "fx1_modOn", 1 }, { "fx1_modType", 1 },
                                { "fx1_distLevel", 0 } } },
            { "REVERSE VERB", { { "samplerOn", 0 }, { "rv1_on", 1 }, { "rv1_type", 6 }, { "rv1_time", 60 }, { "rv1_mix", 55 } } },
        };
    }

    juce::AudioProcessorValueTreeState& apvts;
    std::vector<std::pair<juce::String, Values>> factory;
    std::map<juce::String, Values> user;
};
} // namespace congo
