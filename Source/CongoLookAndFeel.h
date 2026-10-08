#pragma once
// =============================================================================
// Congo Filter look: 80s terminal on a curved CRT. Three themes:
//   0 green on black, 1 orange on black, 2 black on white
// =============================================================================
#include <juce_gui_basics/juce_gui_basics.h>

namespace congo
{
// A theme is three colours: background, main (text, knobs, lit buttons) and secondary
// (outlines, unlit ticks, labels). The phosphor glow is worked out from them.
struct Theme
{
    juce::Colour bg, fg, dim, glow;

    bool light() const noexcept { return bg.getBrightness() > 0.5f; }

    static Theme fromColours (juce::Colour bg, juce::Colour fg, juce::Colour dim)
    {
        const bool lightBg = bg.getBrightness() > 0.5f;
        return { bg, fg, dim, lightBg ? juce::Colours::black.withAlpha (0.13f) : fg.withAlpha (0.33f) };
    }
    static Theme get (int index)                                  // built-ins: green, orange, white
    {
        switch (index)
        {
            case 1:  return fromColours (juce::Colour (0xff0c0805), juce::Colour (0xffffa526), juce::Colour (0xff6b4510));
            case 2:  return fromColours (juce::Colour (0xfff2f0ea), juce::Colour (0xff151515), juce::Colour (0xff9a9890));
            default: return fromColours (juce::Colour (0xff040a05), juce::Colour (0xff3dff6e), juce::Colour (0xff17602a));
        }
    }
};

inline juce::Font monoFont (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height,
                                          bold ? juce::Font::bold : juce::Font::plain));
}

// A knob's value box you can drag: hold the left button on the number and push up / pull down
// (shift = fine). A click without dragging still opens it for typing.
class DragValueLabel : public juce::Label
{
public:
    void mouseDown (const juce::MouseEvent& e) override
    {
        juce::Label::mouseDown (e);
        if (auto* s = dynamic_cast<juce::Slider*> (getParentComponent()))
            startNorm = s->valueToProportionOfLength (s->getValue());
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto* s = dynamic_cast<juce::Slider*> (getParentComponent());
        if (s == nullptr || ! s->isEnabled() || isBeingEdited()) return;
        const double perPixel = e.mods.isShiftDown() ? 0.0005 : 0.004;           // 250 px = whole range
        const double norm = juce::jlimit (0.0, 1.0, startNorm - (double) e.getDistanceFromDragStartY() * perPixel);
        s->setValue (s->proportionOfLengthToValue (norm), juce::sendNotificationSync);
    }
private:
    double startNorm = 0.0;
};

class CongoLookAndFeel : public juce::LookAndFeel_V4
{
public:
    CongoLookAndFeel() { setTheme (0); }

    void setTheme (int index) { themeIndex = index;  setTheme (Theme::get (index)); }

    void setTheme (const Theme& t)
    {
        theme = t;
        setColour (juce::ResizableWindow::backgroundColourId, theme.bg);
        setColour (juce::Label::textColourId, theme.fg);
        setColour (juce::Slider::textBoxTextColourId, theme.fg);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, theme.dim);
        setColour (juce::TextEditor::textColourId, theme.fg);
        setColour (juce::TextEditor::backgroundColourId, theme.bg);
        setColour (juce::TextEditor::highlightColourId, theme.dim);
        setColour (juce::TextEditor::outlineColourId, theme.dim);
        setColour (juce::TextEditor::focusedOutlineColourId, theme.fg);
        setColour (juce::CaretComponent::caretColourId, theme.fg);
        setColour (juce::ComboBox::backgroundColourId, theme.bg);
        setColour (juce::ComboBox::textColourId, theme.fg);
        setColour (juce::ComboBox::outlineColourId, theme.dim);
        setColour (juce::ComboBox::arrowColourId, theme.fg);
        setColour (juce::PopupMenu::backgroundColourId, theme.bg);
        setColour (juce::PopupMenu::textColourId, theme.fg);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, theme.fg);
        setColour (juce::PopupMenu::highlightedTextColourId, theme.bg);
        setColour (juce::TextButton::buttonColourId, theme.bg);
        setColour (juce::TextButton::buttonOnColourId, theme.fg);
        setColour (juce::TextButton::textColourOffId, theme.fg);
        setColour (juce::TextButton::textColourOnId, theme.bg);
        setColour (juce::TooltipWindow::backgroundColourId, theme.bg);
        setColour (juce::TooltipWindow::textColourId, theme.fg);
        setColour (juce::TooltipWindow::outlineColourId, theme.fg);
    }

    const Theme& getTheme() const noexcept { return theme; }
    int getThemeIndex() const noexcept { return themeIndex; }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (6.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto  centre = bounds.getCentre();
        const float angle  = startAngle + pos * (endAngle - startAngle);
        const float alpha  = s.isEnabled() ? 1.0f : 0.3f;

        // tick ring
        for (int i = 0; i <= 20; ++i)
        {
            const float a = startAngle + (float) i / 20.0f * (endAngle - startAngle);
            const float r1 = radius * 0.92f, r2 = radius * (i % 5 == 0 ? 1.0f : 0.97f);
            g.setColour ((i <= (int) std::round (pos * 20.0f) ? theme.fg : theme.dim).withMultipliedAlpha (alpha));
            g.drawLine (centre.x + r1 * std::sin (a), centre.y - r1 * std::cos (a),
                        centre.x + r2 * std::sin (a), centre.y - r2 * std::cos (a), 1.4f);
        }

        // value arc with phosphor glow
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, radius * 0.78f, radius * 0.78f, 0.0f, startAngle, angle, true);
        g.setColour (theme.glow.withMultipliedAlpha (alpha));
        g.strokePath (arc, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (theme.fg.withMultipliedAlpha (alpha));
        g.strokePath (arc, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // body + pointer
        const float body = radius * 0.6f;
        g.setColour (theme.bg);
        g.fillEllipse (centre.x - body, centre.y - body, body * 2.0f, body * 2.0f);
        g.setColour (theme.dim.withMultipliedAlpha (alpha));
        g.drawEllipse (centre.x - body, centre.y - body, body * 2.0f, body * 2.0f, 1.2f);
        g.setColour (theme.fg.withMultipliedAlpha (alpha));
        g.drawLine (centre.x + body * 0.25f * std::sin (angle), centre.y - body * 0.25f * std::cos (angle),
                    centre.x + body * 0.95f * std::sin (angle), centre.y - body * 0.95f * std::cos (angle), 2.4f);
    }

    // every knob's value box: same look as before, plus drag up / down to change the value
    juce::Label* createSliderTextBox (juce::Slider& slider) override
    {
        std::unique_ptr<juce::Label> base (LookAndFeel_V4::createSliderTextBox (slider));
        auto* l = new DragValueLabel();
        l->setFont (base->getFont());
        l->setJustificationType (base->getJustificationType());
        l->setBorderSize (base->getBorderSize());
        l->setMinimumHorizontalScale (base->getMinimumHorizontalScale());
        for (int id : { (int) juce::Label::backgroundColourId, (int) juce::Label::textColourId, (int) juce::Label::outlineColourId,
                        (int) juce::Label::backgroundWhenEditingColourId, (int) juce::Label::textWhenEditingColourId,
                        (int) juce::Label::outlineWhenEditingColourId })
            if (base->isColourSpecified (id)) l->setColour (id, base->findColour (id));
        l->setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        return l;
    }

    // MASTER fader: a groove and a DAW-style cap (the meters behind it are drawn by MasterStrip)
    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                           float minPos, float maxPos, const juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        if (style != juce::Slider::LinearVertical)
        {
            LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minPos, maxPos, style, s);
            return;
        }
        const float cx = (float) x + (float) width * 0.5f;
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRect (cx - 2.0f, (float) y, 4.0f, (float) height);
        g.setColour (theme.dim);
        g.drawRect (cx - 2.0f, (float) y, 4.0f, (float) height, 1.0f);

        const float capW = juce::jmin ((float) width - 4.0f, 44.0f), capH = 18.0f;
        const juce::Rectangle<float> cap (cx - capW * 0.5f, sliderPos - capH * 0.5f, capW, capH);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRect (cap.translated (1.5f, 2.0f));
        g.setColour (theme.bg.brighter (0.25f));
        g.fillRect (cap);
        g.setColour (theme.fg.withMultipliedAlpha (s.isEnabled() ? 1.0f : 0.4f));
        g.drawRect (cap, 1.4f);
        g.fillRect (cap.getX() + 4.0f, sliderPos - 1.0f, capW - 8.0f, 2.0f);       // the line you read
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                               bool highlighted, bool down) override
    {
        const auto r = b.getLocalBounds().toFloat().reduced (1.5f);
        const bool on = b.getToggleState();
        const float alpha = b.isEnabled() ? 1.0f : 0.35f;
        if (b.getComponentID() == "kill")                     // always red, brighter when pressed
        {
            const juce::Colour red (0xffd42a1e);
            g.setColour (red.withAlpha (0.35f));
            g.fillRect (r.expanded (1.5f));
            g.setColour (down ? red.brighter (0.35f) : (highlighted ? red.brighter (0.15f) : red));
            g.fillRect (r);
            g.setColour (red.brighter (0.5f));
            g.drawRect (r, 1.2f);
            return;
        }
        if (on)
        {
            g.setColour (theme.glow.withMultipliedAlpha (alpha));
            g.fillRect (r.expanded (1.5f));
            g.setColour (theme.fg.withMultipliedAlpha (alpha));
            g.fillRect (r);
        }
        else if (highlighted || down)
        {
            g.setColour (theme.dim.withAlpha (0.35f * alpha));
            g.fillRect (r);
        }
        g.setColour ((on ? theme.fg : theme.dim).withMultipliedAlpha (alpha));
        g.drawRect (r, 1.2f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        const float alpha = b.isEnabled() ? 1.0f : 0.35f;
        g.setFont (monoFont (juce::jmin (15.0f, (float) b.getHeight() * 0.55f), true));
        const bool kill = b.getComponentID() == "kill";
        g.setColour (kill ? juce::Colours::white
                          : (b.getToggleState() ? theme.bg : theme.fg).withMultipliedAlpha (alpha));
        g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, false);
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int,
                       juce::ComboBox& box) override
    {
        const auto r = juce::Rectangle<int> (0, 0, width, height).toFloat().reduced (1.0f);
        const float alpha = box.isEnabled() ? 1.0f : 0.35f;
        g.setColour (theme.bg);
        g.fillRect (r);
        g.setColour (theme.dim.withMultipliedAlpha (alpha));
        g.drawRect (r, 1.2f);
        juce::Path arrow;
        const float ax = (float) width - 14.0f, ay = (float) height * 0.5f;
        arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
        g.setColour (theme.fg.withMultipliedAlpha (alpha));
        g.fillPath (arrow);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override        { return monoFont (14.0f, true); }
    juce::Font getPopupMenuFont() override                        { return monoFont (14.0f); }
    juce::Font getLabelFont (juce::Label&) override               { return monoFont (13.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return monoFont (juce::jmin (15.0f, (float) h * 0.55f), true); }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (6, 1, box.getWidth() - 24, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }

private:
    Theme theme;
    int themeIndex = 0;
};
} // namespace congo
