#pragma once
// =============================================================================
// UiParts.h  -  the pop-out THEME editor and the red 7-segment TEMPO display
// =============================================================================
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include "ThemeStore.h"
#include "CongoLookAndFeel.h"

namespace congo
{
// ---- a clickable colour swatch --------------------------------------------------------
struct Swatch : public juce::Component
{
    juce::Colour colour;
    juce::String title;
    bool selected = false;
    std::function<void()> onClick;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (juce::Colours::black);
        g.fillRect (r);
        g.setColour (colour);
        g.fillRect (r.reduced (3.0f).withTrimmedBottom (18.0f));
        g.setColour (selected ? juce::Colours::white : juce::Colour (0xff707070));
        g.drawRect (r, selected ? 2.4f : 1.0f);
        g.setFont (monoFont (11.0f, true));
        g.drawText (title, r.removeFromBottom (18.0f).toNearestInt(), juce::Justification::centred, false);
    }
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }
};

// ---- the THEME editor (opens in a pop-out next to the THEME button) ------------------------
class ThemeEditor : public juce::Component, private juce::ChangeListener
{
public:
    // onApply previews / applies a theme on the plugin; current = the theme showing now
    ThemeEditor (ThemeStore& s, const NamedTheme& current, std::function<void (const NamedTheme&)> apply,
                 bool discoOn = false, std::function<void (bool)> discoChanged = {})
        : store (s), working (current), onApply (std::move (apply)), onDisco (std::move (discoChanged))
    {
        title.setText ("THEME EDITOR", juce::dontSendNotification);
        title.setFont (monoFont (15.0f, true));
        addAndMakeVisible (title);

        listLabel.setText ("THEMES", juce::dontSendNotification);
        addAndMakeVisible (listLabel);
        list.onChange = [this] { pick (list.getText()); };
        addAndMakeVisible (list);

        const char* names[3] = { "BACKGROUND", "MAIN", "SECONDARY" };
        for (int k = 0; k < 3; ++k)
        {
            swatch[k].title = names[k];
            swatch[k].onClick = [this, k] { target = k;  syncControls(); };
            addAndMakeVisible (swatch[k]);
        }

        selector.addChangeListener (this);
        addAndMakeVisible (selector);

        hexLabel.setText ("HEX", juce::dontSendNotification);
        addAndMakeVisible (hexLabel);
        hex.setInputRestrictions (7, "#0123456789abcdefABCDEF");
        hex.onReturnKey = [this] { applyHex(); };
        hex.onFocusLost = [this] { applyHex(); };
        addAndMakeVisible (hex);

        nameLabel.setText ("NAME", juce::dontSendNotification);
        addAndMakeVisible (nameLabel);
        name.setInputRestrictions (24);
        addAndMakeVisible (name);

        save.setButtonText ("SAVE");
        save.setTooltip ("Save these three colours to your theme list (available in every project).");
        save.onClick = [this] { doSave(); };
        addAndMakeVisible (save);

        setDefault.setButtonText ("SET DEFAULT");
        setDefault.setTooltip ("Make these colours the theme every new copy of the plugin opens in.");
        setDefault.onClick = [this] { store.setDefault (working);  hint.setText ("Default set: new copies of the plugin\nopen in " + (working.name.isNotEmpty() ? working.name : juce::String ("these colours")) + ".", juce::dontSendNotification); };
        addAndMakeVisible (setDefault);

        disco.setButtonText ("Disco");
        disco.setClickingTogglesState (true);
        disco.setToggleState (discoOn, juce::dontSendNotification);
        disco.setTooltip ("Disco: all three colours change at random on every beat, in time with the tempo.\n"
                          "Switch it off and the last colours stay.");
        disco.onClick = [this] { if (onDisco) onDisco (disco.getToggleState()); };
        addAndMakeVisible (disco);

        del.setButtonText ("DELETE");
        del.setTooltip ("Delete the selected theme from your list (the built-in themes stay).");
        del.onClick = [this] { doDelete(); };
        addAndMakeVisible (del);

        hint.setText ("Pick a colour box, then a colour (or type a hex value).\n"
                      "Changes show live; SAVE adds it to your list.", juce::dontSendNotification);
        hint.setFont (monoFont (11.0f));
        addAndMakeVisible (hint);

        refreshList();
        syncControls();
        setSize (560, 420);
    }

    ~ThemeEditor() override { selector.removeChangeListener (this); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff101010));
        g.setColour (juce::Colour (0xff3a3a3a));
        g.drawRect (getLocalBounds(), 1);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        title.setBounds (r.removeFromTop (22));
        r.removeFromTop (6);
        auto left = r.removeFromLeft (190);
        r.removeFromLeft (12);

        listLabel.setBounds (left.removeFromTop (18));
        list.setBounds (left.removeFromTop (28));
        left.removeFromTop (6);
        del.setBounds (left.removeFromTop (26).withWidth (90));
        left.removeFromTop (12);
        auto sw = left.removeFromTop (88);
        const int w = sw.getWidth() / 3;
        for (int k = 0; k < 3; ++k) swatch[k].setBounds (sw.removeFromLeft (w).reduced (2, 0));
        left.removeFromTop (8);
        auto hx = left.removeFromTop (26);
        hexLabel.setBounds (hx.removeFromLeft (40));
        hex.setBounds (hx);
        left.removeFromTop (8);
        auto nm = left.removeFromTop (26);
        nameLabel.setBounds (nm.removeFromLeft (40));
        name.setBounds (nm);
        left.removeFromTop (6);
        save.setBounds (left.removeFromTop (28));
        left.removeFromTop (6);
        auto row = left.removeFromTop (28);
        setDefault.setBounds (row.removeFromLeft (112));
        row.removeFromLeft (6);
        disco.setBounds (row);
        left.removeFromTop (6);
        hint.setBounds (left);

        selector.setBounds (r);
    }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        if (updating) return;
        colourRef (target) = selector.getCurrentColour().withAlpha (1.0f);
        changed();
    }

    juce::Colour& colourRef (int k) { return k == 0 ? working.bg : (k == 1 ? working.fg : working.dim); }

    void changed()
    {
        working.builtIn = false;
        syncControls (false);
        if (onApply) onApply (working);
    }

    void applyHex()
    {
        juce::Colour c;
        if (parseHex (hex.getText(), c)) { colourRef (target) = c;  changed(); }
        else hex.setText (toHex (colourRef (target)), juce::dontSendNotification);
    }

    void syncControls (bool moveSelector = true)
    {
        for (int k = 0; k < 3; ++k)
        {
            swatch[k].colour = colourRef (k);
            swatch[k].selected = k == target;
            swatch[k].repaint();
        }
        hex.setText (toHex (colourRef (target)), juce::dontSendNotification);
        if (moveSelector)
        {
            updating = true;
            selector.setCurrentColour (colourRef (target), juce::dontSendNotification);
            updating = false;
        }
        if (name.getText().isEmpty() || ! working.builtIn) name.setText (working.builtIn ? juce::String() : working.name,
                                                                          juce::dontSendNotification);
    }

    void refreshList()
    {
        list.clear (juce::dontSendNotification);
        int id = 1, sel = 0;
        for (auto& t : store.all())
        {
            list.addItem (t.name, id);
            if (t.name == working.name) sel = id;
            ++id;
        }
        if (sel > 0) list.setSelectedId (sel, juce::dontSendNotification);
    }

    void pick (const juce::String& n)
    {
        if (auto* t = store.find (n))
        {
            working = *t;
            name.setText (t->builtIn ? juce::String() : t->name, juce::dontSendNotification);
            syncControls();
            if (onApply) onApply (working);
        }
    }

    void doSave()
    {
        working.name = name.getText().isNotEmpty() ? name.getText()
                                                   : juce::String ("MY THEME ") + juce::String ((int) store.all().size() - 2);
        working.name = store.save (working);
        working.builtIn = false;
        refreshList();
        syncControls (false);
        if (onApply) onApply (working);
    }

    void doDelete()
    {
        if (store.remove (list.getText()))
        {
            refreshList();
            pick ("GREEN");
            refreshList();
        }
    }

    ThemeStore& store;
    NamedTheme working;
    std::function<void (const NamedTheme&)> onApply;
    std::function<void (bool)> onDisco;
    int target = 1;
    bool updating = false;

    juce::Label title, listLabel, hexLabel, nameLabel, hint;
    juce::ComboBox list;
    Swatch swatch[3];
    juce::ColourSelector selector { juce::ColourSelector::showColourAtTop | juce::ColourSelector::editableColour
                                    | juce::ColourSelector::showSliders | juce::ColourSelector::showColourspace };
    juce::TextEditor hex, name;
    juce::TextButton save, del, setDefault, disco;
};

// ---- red 7-segment TEMPO display ----------------------------------------------------------
class TempoDisplay : public juce::Component, public juce::SettableTooltipClient
{
public:
    // shown(): the BPM to display; hostLive(): HOST on and the host is sending a tempo;
    // hostMode(): HOST on. When HOST is off the display edits the tempo parameter.
    TempoDisplay (juce::RangedAudioParameter& tempoParam, std::function<float()> shown,
                  std::function<bool()> hostLive, std::function<bool()> hostMode)
        : param (tempoParam), shownBpm (std::move (shown)), isHostLive (std::move (hostLive)),
          isHostMode (std::move (hostMode))
    {
        editor.setInputRestrictions (6, "0123456789.");
        editor.setJustification (juce::Justification::centred);
        editor.onReturnKey = [this] { commitText(); };
        editor.onFocusLost = [this] { commitText(); };
        editor.onEscapeKey = [this] { editor.setVisible (false); };
        addChildComponent (editor);
    }

    void refresh()
    {
        const auto text = currentText();
        if (text != lastText) { lastText = text;  repaint(); }
        setTooltip (isHostMode() ? "TEMPO from the host. Turn HOST off to set your own."
                                 : "TEMPO: drag up / down (shift = fine), scroll, or double-click to type. "
                                   "Double-click with alt to reset to 133.33.");
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setFont (monoFont (11.0f, true));
        g.setColour (juce::Colour (0xffd8d8d8));
        g.drawText ("TEMPO", r.removeFromTop (13.0f).toNearestInt(), juce::Justification::centred, false);
        r.removeFromTop (1.0f);
        g.setColour (juce::Colour (0xff0b0405));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (juce::Colour (0xff3a3a3a));
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);

        const auto text = currentText();                          // e.g. "133.33"
        const int digits = 5;
        auto area = r.reduced (7.0f, 5.0f);
        const float cw = area.getWidth() / (float) digits;
        int d = 0;
        for (int i = 0; i < text.length() && d < digits; ++i)
        {
            const auto ch = text[i];
            if (ch == '.') continue;
            const bool dotAfter = i + 1 < text.length() && text[i + 1] == '.';
            drawDigit (g, juce::Rectangle<float> (area.getX() + cw * (float) d, area.getY(), cw, area.getHeight()), ch, dotAfter);
            ++d;
        }
    }

    void resized() override { editor.setBounds (getLocalBounds().withTrimmedTop (14).reduced (4)); }

    void mouseDown (const juce::MouseEvent&) override
    {
        if (isHostMode()) return;
        dragStart = param.convertFrom0to1 (param.getValue());
        param.beginChangeGesture();
        dragging = true;
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging) return;
        const float perPixel = e.mods.isShiftDown() ? 0.01f : 0.25f;
        set (dragStart - (float) e.getDistanceFromDragStartY() * perPixel, false);
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging) param.endChangeGesture();
        dragging = false;
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (isHostMode()) return;
        const float step = e.mods.isShiftDown() ? 0.01f : 1.0f;
        set (param.convertFrom0to1 (param.getValue()) + (w.deltaY > 0 ? step : -step), true);
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (isHostMode()) return;
        if (e.mods.isAltDown()) { set (133.33f, true);  return; }
        editor.setText (juce::String (param.convertFrom0to1 (param.getValue()), 2), juce::dontSendNotification);
        editor.setVisible (true);
        editor.grabKeyboardFocus();
        editor.selectAll();
    }

private:
    juce::String currentText() const
    {
        if (isHostMode() && ! isHostLive()) return "---.--";
        const float v = juce::jlimit (0.0f, 999.99f, shownBpm());
        auto t = juce::String (v, 2);
        while (t.length() < 6) t = " " + t;                       // right-align: " 98.50"
        return t;
    }

    void set (float bpm, bool gesture)
    {
        bpm = juce::jlimit (20.0f, 300.0f, bpm);
        if (gesture) param.beginChangeGesture();
        param.setValueNotifyingHost (param.convertTo0to1 (bpm));
        if (gesture) param.endChangeGesture();
        refresh();
    }

    void commitText()
    {
        if (! editor.isVisible()) return;
        const float v = editor.getText().getFloatValue();
        editor.setVisible (false);
        if (v > 0.0f) set (v, true);
    }

    // 7 segments: a top, b upper right, c lower right, d bottom, e lower left, f upper left, g middle
    void drawDigit (juce::Graphics& g, juce::Rectangle<float> cell, juce::juce_wchar ch, bool dot) const
    {
        static const unsigned char map[10] = { 0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f };
        unsigned char on = 0;
        if (ch >= '0' && ch <= '9') on = map[ch - '0'];
        else if (ch == '-') on = 0x40;

        const auto c = cell.reduced (cell.getWidth() * 0.12f, 0.0f);
        const float t = c.getWidth() * 0.18f, x0 = c.getX(), x1 = c.getRight() - t * 0.4f,
                    y0 = c.getY(), ym = c.getCentreY(), y1 = c.getBottom(), sk = c.getWidth() * 0.12f;
        auto seg = [&] (float ax, float ay, float bx, float by, bool horizontal)
        {
            juce::Path p;
            const float h = t * 0.5f;
            if (horizontal)
            {
                p.startNewSubPath (ax + h, ay);  p.lineTo (ax + h * 2.0f, ay - h);  p.lineTo (bx - h * 2.0f, by - h);
                p.lineTo (bx - h, by);           p.lineTo (bx - h * 2.0f, by + h);  p.lineTo (ax + h * 2.0f, ay + h);
            }
            else
            {
                p.startNewSubPath (ax, ay + h);  p.lineTo (ax + h, ay + h * 2.0f);  p.lineTo (bx + h, by - h * 2.0f);
                p.lineTo (bx, by - h);           p.lineTo (bx - h, by - h * 2.0f);  p.lineTo (ax - h, ay + h * 2.0f);
            }
            p.closeSubPath();
            return p;
        };
        // slight italic slant, like the hardware LEDs
        const juce::Path segs[7] = {
            seg (x0 + sk, y0 + t * 0.5f, x1 + sk, y0 + t * 0.5f, true),
            seg (x1 + sk, y0 + t * 0.5f, x1 + sk * 0.5f, ym, false),
            seg (x1 + sk * 0.5f, ym, x1, y1 - t * 0.5f, false),
            seg (x0, y1 - t * 0.5f, x1, y1 - t * 0.5f, true),
            seg (x0 + sk * 0.5f, ym, x0, y1 - t * 0.5f, false),
            seg (x0 + sk, y0 + t * 0.5f, x0 + sk * 0.5f, ym, false),
            seg (x0 + sk * 0.5f, ym, x1 + sk * 0.5f, ym, true) };
        for (int k = 0; k < 7; ++k)
        {
            const bool lit = (on >> k) & 1;
            g.setColour (lit ? juce::Colour (0xffff3030) : juce::Colour (0xff2a0a0b));
            g.fillPath (segs[k]);
        }
        if (dot)
        {
            const float r = t * 0.65f;
            g.setColour (juce::Colour (0xffff3030));
            g.fillEllipse (c.getRight() + cell.getWidth() * 0.02f, y1 - r * 1.2f, r, r);
        }
    }

    juce::RangedAudioParameter& param;
    std::function<float()> shownBpm;
    std::function<bool()>  isHostLive, isHostMode;
    juce::TextEditor editor;
    juce::String lastText;
    float dragStart = 133.33f;
    bool  dragging = false;
};

// ---- MASTER channel: live L/R output meters behind the fader, on the fader's own scale -----
class MasterStrip : public juce::Component
{
public:
    // takePeak(c): the highest |sample| on side c since the last call (then reset)
    MasterStrip (juce::Slider& faderToTrack, CongoLookAndFeel& laf, std::function<float (int)> takePeakFn)
        : fader (faderToTrack), lnf (laf), takePeak (std::move (takePeakFn)) {}

    void tick (float dtSeconds)
    {
        for (int c = 0; c < 2; ++c)
        {
            const float db = juce::Decibels::gainToDecibels (takePeak (c), -100.0f);
            level[c] = db > level[c] ? db : juce::jmax (db, level[c] - 24.0f * dtSeconds);   // 24 dB/s fall
            if (level[c] >= hold[c]) { hold[c] = level[c];  holdAge[c] = 0.0f; }
            else if ((holdAge[c] += dtSeconds) > 1.5f) hold[c] = juce::jmax (level[c], hold[c] - 30.0f * dtSeconds);
            if (db > 0.0f) clip[c] = true;
        }
        repaint();
    }

    void mouseDown (const juce::MouseEvent&) override { clip[0] = clip[1] = false;  repaint(); }

    void paint (juce::Graphics& g) override
    {
        const auto& t = lnf.getTheme();
        const auto fb = fader.getBounds().translated (-getX(), -getY());
        auto yOf = [&] (float db) { return (float) fb.getY() + fader.getPositionOfValue (juce::jlimit (-60.0, 6.0, (double) db)); };
        const float top = yOf (6.0f), bottom = yOf (-60.0f);
        const float cx = (float) fb.getCentreX();

        // meter wells
        const float mw = 9.0f, gap = 22.0f;
        const float xs[2] = { cx - gap * 0.5f - mw, cx + gap * 0.5f };
        for (int c = 0; c < 2; ++c)
        {
            const juce::Rectangle<float> well (xs[c], top, mw, bottom - top);
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.fillRect (well);
            const float yl = yOf (level[c]);
            if (level[c] > -60.0f)
            {
                juce::ColourGradient grad (t.fg, 0.0f, bottom, juce::Colour (0xffff3020), 0.0f, top, false);
                grad.addColour ((double) ((bottom - yOf (-6.0f)) / (bottom - top)), t.fg);
                grad.addColour ((double) ((bottom - yOf (0.0f)) / (bottom - top)), juce::Colour (0xffffb000));
                g.setGradientFill (grad);
                g.fillRect (juce::Rectangle<float> (xs[c], yl, mw, bottom - yl));
            }
            if (hold[c] > -60.0f)
            {
                g.setColour (hold[c] > 0.0f ? juce::Colour (0xffff3020) : t.fg.brighter (0.3f));
                g.fillRect (xs[c], yOf (hold[c]) - 1.0f, mw, 2.0f);
            }
            g.setColour (t.dim);
            g.drawRect (well, 1.0f);
        }

        // dB scale on the left, at the fader's own positions
        g.setFont (monoFont (10.0f));
        for (float db : { 6.0f, 0.0f, -6.0f, -12.0f, -24.0f, -36.0f, -48.0f, -60.0f })
        {
            const float y = yOf (db);
            g.setColour (db == 0.0f ? t.fg : t.dim.brighter (0.4f));
            g.fillRect (xs[0] - 7.0f, y - 0.5f, 5.0f, 1.0f);
            g.fillRect (xs[1] + mw + 2.0f, y - 0.5f, 5.0f, 1.0f);
            const juce::String txt = db <= -60.0f ? juce::String ("-inf") : (db > 0.0f ? "+" : "") + juce::String ((int) db);
            g.drawText (txt, juce::Rectangle<float> (0.0f, y - 6.0f, xs[0] - 9.0f, 12.0f).toNearestInt(),
                        juce::Justification::centredRight, false);
        }
    }

    float levelDb (int c) const noexcept { return level[c]; }
    bool  clipped (int c) const noexcept { return clip[c]; }
    void  clearClips() noexcept { clip[0] = clip[1] = false;  repaint(); }
    // where the meter wells sit (x, in this component), for the clip lights above them
    float wellX (int c) const noexcept
    {
        const float cx = (float) fader.getBounds().translated (-getX(), -getY()).getCentreX();
        return c == 0 ? cx - 11.0f - 9.0f : cx + 11.0f;
    }

private:
    juce::Slider& fader;
    CongoLookAndFeel& lnf;
    std::function<float (int)> takePeak;
    float level[2] { -100.0f, -100.0f }, hold[2] { -100.0f, -100.0f }, holdAge[2] {};
    bool  clip[2] {};
};

// ---- a number you can drag (used for the NUDGE readout): up = more, down = less ------------
class DragNumberLabel : public juce::Label
{
public:
    std::function<float()> getValue;
    std::function<void (float)> setValue;
    float perPixel = 0.05f, finePerPixel = 0.01f;

    void mouseDown (const juce::MouseEvent& e) override
    {
        juce::Label::mouseDown (e);
        if (getValue) start = getValue();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! setValue || isBeingEdited()) return;
        const float step = e.mods.isShiftDown() ? finePerPixel : perPixel;
        setValue (start - (float) e.getDistanceFromDragStartY() * step);
    }
private:
    float start = 0.0f;
};

// ---- the red clip lights: their own button above the fader, so clicking them only clears
//      the red and never touches the fader ------------------------------------------------
class ClipLights : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit ClipLights (MasterStrip& s) : strip (s) { setTooltip ("Clip lights: click to clear."); }
    void paint (juce::Graphics& g) override
    {
        for (int c = 0; c < 2; ++c)
        {
            const float x = strip.wellX (c) + (float) (strip.getX() - getX());
            const juce::Rectangle<float> led (x - 2.0f, 3.0f, 13.0f, (float) getHeight() - 6.0f);
            g.setColour (strip.clipped (c) ? juce::Colour (0xffff3020) : juce::Colour (0xff3a0c0c));
            g.fillRoundedRectangle (led, 2.0f);
        }
    }
    void mouseDown (const juce::MouseEvent&) override { strip.clearClips();  repaint(); }
private:
    MasterStrip& strip;
};
} // namespace congo
