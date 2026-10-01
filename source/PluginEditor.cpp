#include "PluginEditor.h"
#include "ui/Textures.h"

namespace afterglow
{
namespace
{
    using ui::Layout::cheekWidth;
    using ui::Layout::headerHeight;
    using ui::Layout::bayHeight;
    using ui::Layout::bigKnobHeight;
} // namespace

//======================================================================================================================
MainPanel::MainPanel (AfterglowProcessor& p)
    : processor (p),
      header (p.getState(), p.getPresetManager(), p.getUndoManager()),
      bigKnobs (p.getState()),
      master (p.getState(), p.getTelemetry()),
      browser (p.getPresetManager()),
      about (p.getState()),
      saveDialog (p.getPresetManager())
{
    auto& state = p.getState();
    auto& telemetry = p.getTelemetry();
    modules[0] = std::make_unique<ui::NoisePanel> (state, telemetry);
    modules[1] = std::make_unique<ui::WobblePanel> (state, telemetry);
    modules[2] = std::make_unique<ui::DistortPanel> (state, telemetry);
    modules[3] = std::make_unique<ui::DigitalPanel> (state, telemetry);
    modules[4] = std::make_unique<ui::SpacePanel> (state, telemetry);
    modules[5] = std::make_unique<ui::MagneticPanel> (state, telemetry);

    addAndMakeVisible (header);
    for (auto& m : modules)
        addAndMakeVisible (*m);
    addAndMakeVisible (bigKnobs);
    addAndMakeVisible (master);
    addChildComponent (browser);
    addChildComponent (about);
    addChildComponent (saveDialog);

    header.onBrowse = [this] { openBrowser(); };
    header.onSave = [this] { openSaveDialog(); };
    header.onAbout = [this] { openAbout(); };
    browser.onClose = [this] { closeOverlays(); };
    about.onClose = [this] { closeOverlays(); };
    about.onScaleChosen = [this] (float s) { if (onScaleChosen) onScaleChosen (s); };
    saveDialog.onClose = [this] { saveDialog.setVisible (false); header.refresh(); };

    getProperties().set ("popupHost", true);
    setBufferedToImage (true);
    setSize (ui::designWidth, ui::designHeight);
}

void MainPanel::resized()
{
    const auto inner = getLocalBounds().reduced (cheekWidth, 0);
    header.setBounds (inner.getX(), 0, inner.getWidth(), headerHeight);
    bayArea = { inner.getX(), headerHeight, inner.getWidth(), bayHeight };

    using namespace ui::Layout;
    for (size_t i = 0; i < modules.size(); ++i)
        modules[i]->setBounds (bayArea.getX() + bayMargin + static_cast<int> (i) * modulePitch, bayArea.getY() + 8, moduleWidth, bayArea.getHeight() - 16);

    bigKnobs.setBounds (inner.getX(), bayArea.getBottom(), inner.getWidth(), bigKnobHeight);
    master.setBounds (inner.getX(), bigKnobs.getBottom(), inner.getWidth(), getHeight() - bigKnobs.getBottom());

    browser.setBounds (bayArea.reduced (6, 4));
    about.setBounds (bayArea.reduced (6, 4));
    saveDialog.setBounds (getLocalBounds());
}

void MainPanel::refresh (double seconds)
{
    for (auto& m : modules)
        m->refresh (seconds);
    header.refresh();
    bigKnobs.refresh();
    master.refresh (seconds);
}

void MainPanel::showOverlay (const juce::String& name)
{
    if (name == "browser")
        openBrowser();
    else if (name == "about")
        openAbout();
    else if (name == "save")
        openSaveDialog();
}

void MainPanel::openBrowser()
{
    about.setVisible (false);
    browser.open();
}

void MainPanel::openSaveDialog()
{
    saveDialog.open();
}

void MainPanel::openAbout()
{
    if (browser.isVisible())
        return;
    about.setVisible (! about.isVisible());
    if (about.isVisible())
    {
        about.toFront (true);
        about.grabKeyboardFocus();
    }
}

void MainPanel::closeOverlays()
{
    browser.setVisible (false);
    about.setVisible (false);
    header.refresh();
}

void MainPanel::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const auto scale = std::max (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());

    g.fillAll (ui::Colours::chassis);

    // Walnut end cheeks.
    for (auto side : { 0, 1 })
    {
        const auto cheek = side == 0 ? area.withWidth (static_cast<float> (cheekWidth)) : area.withLeft (area.getRight() - static_cast<float> (cheekWidth));
        const auto wood = ui::Textures::get (ui::Textures::Kind::walnut, juce::roundToInt (cheek.getWidth() * scale), juce::roundToInt (cheek.getHeight() * scale),
                                             static_cast<uint32_t> (3 + side));
        ui::Textures::drawFitted (g, wood, cheek);

        // Lacquer: rounded edge highlights and shading towards the chassis.
        juce::ColourGradient lacquer (juce::Colours::white.withAlpha (0.16f), side == 0 ? cheek.getX() + 3.0f : cheek.getRight() - 3.0f, 0.0f,
                                      juce::Colours::black.withAlpha (0.45f), side == 0 ? cheek.getRight() : cheek.getX(), 0.0f, false);
        lacquer.addColour (0.35, juce::Colours::transparentBlack);
        g.setGradientFill (lacquer);
        g.fillRect (cheek);
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.drawVerticalLine (side == 0 ? juce::roundToInt (cheek.getRight()) - 1 : juce::roundToInt (cheek.getX()), 0.0f, area.getBottom());
    }

    // Recessed module bay.
    const auto bay = bayArea.toFloat();
    juce::ColourGradient bayGrad (juce::Colour (0xff0d0e0f), bay.getX(), bay.getY(), juce::Colour (0xff1c1d20), bay.getX(), bay.getBottom(), false);
    g.setGradientFill (bayGrad);
    g.fillRect (bay);
    const auto tile = juce::roundToInt (128.0f * scale);
    ui::Textures::fillTiled (g, ui::Textures::get (ui::Textures::Kind::darkGrainTile, tile, tile, 9u), bay, scale, 0.6f);
    juce::ColourGradient topShadow (juce::Colours::black.withAlpha (0.75f), bay.getX(), bay.getY(), juce::Colours::transparentBlack, bay.getX(), bay.getY() + 14.0f, false);
    g.setGradientFill (topShadow);
    g.fillRect (bay);
}

//======================================================================================================================
AfterglowEditor::AfterglowEditor (AfterglowProcessor& p)
    : AudioProcessorEditor (p), processor (p), content (p)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (content);
    tooltips = std::make_unique<juce::TooltipWindow> (this, 700);
    tooltips->setLookAndFeel (&lookAndFeel);

    content.onScaleChosen = [this] (float s) { applyScale (s); };

    // Read the stored size before the resize limits are applied (they may resize the editor).
    const auto scale = juce::jlimit (0.6f, 2.2f, processor.getUiScale());

    setResizable (true, true);
    setResizeLimits (juce::roundToInt (ui::designWidth * 0.6f), juce::roundToInt (ui::designHeight * 0.6f),
                     juce::roundToInt (ui::designWidth * 2.2f), juce::roundToInt (ui::designHeight * 2.2f));
    if (auto* sizeConstrainer = getConstrainer())
        sizeConstrainer->setFixedAspectRatio (static_cast<double> (ui::designWidth) / ui::designHeight);

    addMouseListener (this, true);
    setWantsKeyboardFocus (true);

    setSize (juce::roundToInt (ui::designWidth * scale), juce::roundToInt (ui::designHeight * scale));
    constructed = true;

    lastTick = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (30);
}

AfterglowEditor::~AfterglowEditor()
{
    stopTimer();
    removeMouseListener (this);
    if (tooltips != nullptr)
        tooltips->setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void AfterglowEditor::applyScale (float scale)
{
    scale = juce::jlimit (0.6f, 2.2f, scale);
    setSize (juce::roundToInt (ui::designWidth * scale), juce::roundToInt (ui::designHeight * scale));
}

void AfterglowEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::Colours::chassis);
}

void AfterglowEditor::resized()
{
    const auto scale = static_cast<float> (getWidth()) / static_cast<float> (ui::designWidth);
    content.setTransform (juce::AffineTransform::scale (scale));
    content.setBounds (0, 0, ui::designWidth, ui::designHeight);
    if (constructed)
        processor.setUiScale (scale);
}

bool AfterglowEditor::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    if (mods.isCommandDown() && key.getKeyCode() == 'Z')
    {
        if (mods.isShiftDown())
            processor.getUndoManager().redo();
        else
            processor.getUndoManager().undo();
        return true;
    }

    if (mods.isCommandDown() && key.getKeyCode() == 'Y')
    {
        processor.getUndoManager().redo();
        return true;
    }

    return false;
}

void AfterglowEditor::mouseDown (const juce::MouseEvent&)
{
    // Every new mouse gesture starts a new undo step.
    processor.getUndoManager().beginNewTransaction();
}

void AfterglowEditor::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto seconds = juce::jlimit (0.0, 0.25, (now - lastTick) * 0.001);
    lastTick = now;
    content.refresh (seconds);
}

juce::Image AfterglowEditor::renderSnapshot (float scale, const juce::String& overlay)
{
    if (overlay.isNotEmpty())
        content.showOverlay (overlay);

    // Advance the animations a little so the displays show something representative.
    for (int i = 0; i < 20; ++i)
        content.refresh (1.0 / 30.0);

    return createComponentSnapshot (getLocalBounds(), true, scale);
}

} // namespace afterglow
