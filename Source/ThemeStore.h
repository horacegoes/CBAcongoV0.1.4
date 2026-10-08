#pragma once
// =============================================================================
// ThemeStore.h  -  built-in and user themes. User themes are saved to
//   <user application data>/CBA/Congo Filter/themes.xml
// so they are available in every project and every copy of the plugin.
// =============================================================================
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace congo
{
struct NamedTheme
{
    juce::String name;
    juce::Colour bg, fg, dim;
    bool builtIn = false;
};

inline juce::String toHex (juce::Colour c)        { return "#" + c.toDisplayString (false).toUpperCase(); }
inline bool parseHex (const juce::String& text, juce::Colour& out)
{
    const auto t = text.trim().removeCharacters ("#");
    if (t.length() != 6 || ! t.containsOnly ("0123456789abcdefABCDEF")) return false;
    out = juce::Colour ((juce::uint32) (0xff000000u | (juce::uint32) t.getHexValue32()));
    return true;
}

class ThemeStore
{
public:
    ThemeStore() { load(); }

    const std::vector<NamedTheme>& all() const noexcept { return themes; }

    const NamedTheme* find (const juce::String& name) const noexcept
    {
        for (auto& t : themes) if (t.name == name) return &t;
        return nullptr;
    }

    // add or replace a user theme; built-in names can't be overwritten. Returns the saved name.
    juce::String save (NamedTheme t)
    {
        t.name = t.name.trim().toUpperCase().substring (0, 24);
        if (t.name.isEmpty()) t.name = "MY THEME";
        auto base = t.name;
        for (int n = 2; find (t.name) != nullptr && find (t.name)->builtIn; ++n) t.name = base + " " + juce::String (n);
        t.builtIn = false;
        bool replaced = false;
        for (auto& x : themes) if (x.name == t.name) { x = t;  replaced = true; }
        if (! replaced) themes.push_back (t);
        write();
        return t.name;
    }

    // your default: the theme every NEW copy of the plugin opens in (saved sessions keep theirs)
    bool hasDefault() const noexcept { return defaultSet; }
    const NamedTheme& defaultTheme() const noexcept { return defTheme; }
    void setDefault (const NamedTheme& t) { defTheme = t;  defaultSet = true;  write(); }

    bool remove (const juce::String& name)
    {
        for (auto it = themes.begin(); it != themes.end(); ++it)
            if (it->name == name && ! it->builtIn) { themes.erase (it);  write();  return true; }
        return false;
    }

    static juce::File file()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("CBA").getChildFile ("Congo Filter").getChildFile ("themes.xml");
    }

private:
    void load()
    {
        themes = { { "GREEN",  juce::Colour (0xff040a05), juce::Colour (0xff3dff6e), juce::Colour (0xff17602a), true },
                   { "ORANGE", juce::Colour (0xff0c0805), juce::Colour (0xffffa526), juce::Colour (0xff6b4510), true },
                   { "WHITE",  juce::Colour (0xfff2f0ea), juce::Colour (0xff151515), juce::Colour (0xff9a9890), true } };
        if (auto xml = juce::parseXML (file()))
        {
            NamedTheme d;  d.name = xml->getStringAttribute ("defaultName");
            if (d.name.isNotEmpty() && parseHex (xml->getStringAttribute ("defaultBg"), d.bg)
                && parseHex (xml->getStringAttribute ("defaultMain"), d.fg) && parseHex (xml->getStringAttribute ("defaultSecondary"), d.dim))
            { defTheme = d;  defaultSet = true; }
            for (auto* e : xml->getChildWithTagNameIterator ("THEME"))
            {
                NamedTheme t;
                t.name = e->getStringAttribute ("name").toUpperCase();
                if (t.name.isEmpty() || find (t.name) != nullptr) continue;
                if (parseHex (e->getStringAttribute ("bg"), t.bg) && parseHex (e->getStringAttribute ("main"), t.fg)
                    && parseHex (e->getStringAttribute ("secondary"), t.dim))
                    themes.push_back (t);
            }
        }
    }

    void write() const
    {
        juce::XmlElement root ("THEMES");
        if (defaultSet)
        {
            root.setAttribute ("defaultName", defTheme.name);
            root.setAttribute ("defaultBg", toHex (defTheme.bg));
            root.setAttribute ("defaultMain", toHex (defTheme.fg));
            root.setAttribute ("defaultSecondary", toHex (defTheme.dim));
        }
        for (auto& t : themes)
        {
            if (t.builtIn) continue;
            auto* e = root.createNewChildElement ("THEME");
            e->setAttribute ("name", t.name);
            e->setAttribute ("bg", toHex (t.bg));
            e->setAttribute ("main", toHex (t.fg));
            e->setAttribute ("secondary", toHex (t.dim));
        }
        const auto f = file();
        f.getParentDirectory().createDirectory();
        root.writeTo (f);
    }

    std::vector<NamedTheme> themes;
    NamedTheme defTheme;
    bool defaultSet = false;
};
} // namespace congo
