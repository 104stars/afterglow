#include "PresetBrowser.h"

namespace afterglow::ui
{
namespace
{
    void paintDarkPanel (juce::Graphics& g, juce::Rectangle<float> area, float corner)
    {
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRoundedRectangle (area.translated (0.0f, 3.0f), corner);
        juce::ColourGradient bg (juce::Colour (0xff262422), area.getX(), area.getY(), juce::Colour (0xff141312), area.getX(), area.getBottom(), false);
        g.setGradientFill (bg);
        g.fillRoundedRectangle (area, corner);
        g.setColour (Colours::amber.withAlpha (0.25f));
        g.drawRoundedRectangle (area.reduced (0.5f), corner, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawLine (area.getX() + corner, area.getY() + 1.5f, area.getRight() - corner, area.getY() + 1.5f, 1.0f);
    }

    void paintWell (juce::Graphics& g, juce::Rectangle<float> area)
    {
        g.setColour (juce::Colour (0xff0b0a09));
        g.fillRoundedRectangle (area, 4.0f);
        juce::ColourGradient inner (juce::Colours::black, area.getX(), area.getY(), juce::Colours::transparentBlack, area.getX(), area.getY() + 8.0f, false);
        g.setGradientFill (inner);
        g.fillRoundedRectangle (area, 4.0f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawRoundedRectangle (area.reduced (0.5f), 4.0f, 1.0f);
    }
} // namespace

PresetBrowser::PresetBrowser (PresetManager& p) : presets (p)
{
    filters.add ("ALL");
    filters.add ("FACTORY");
    filters.add ("USER");
    for (const auto& c : PresetManager::getCategories())
        filters.add (c.toUpperCase());

    for (int i = 0; i < filters.size(); ++i)
    {
        auto* b = filterButtons.add (new juce::TextButton (filters[i]));
        b->setClickingTogglesState (false);
        b->onClick = [this, i] { selectFilter (i); };
        addAndMakeVisible (b);
    }

    list.setModel (this);
    list.setRowHeight (24);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setOutlineThickness (0);
    addAndMakeVisible (list);

    for (auto* b : { &okButton, &cancelButton, &renameButton, &deleteButton, &startupButton, &folderButton })
        addAndMakeVisible (b);

    okButton.onClick = [this] { close (true); };
    cancelButton.onClick = [this] { close (false); };
    okButton.setTooltip ("Keep the selected preset and any tweaks.");
    cancelButton.setTooltip ("Go back to the settings you had before opening the browser.");

    startupButton.onClick = [this]
    {
        presets.setStartupPreset (selectedPresetIndex());
        list.repaint();
        updateInfo();
    };
    startupButton.setTooltip ("Loads this preset whenever a new instance of Afterglow is created.");

    folderButton.onClick = []
    {
        const auto dir = PresetManager::getUserPresetDirectory();
        dir.createDirectory();
        dir.startAsProcess();
    };
    folderButton.setTooltip ("Opens the user preset folder (Documents/Afterglow/Presets).");

    renameButton.onClick = [this]
    {
        const auto idx = selectedPresetIndex();
        if (idx < 0 || presets.getPresets()[static_cast<size_t> (idx)].isFactory)
            return;

        // A child of the browser (not a desktop window), owned by it, with the editor's look-and-feel inherited.
        renameWindow = std::make_unique<juce::AlertWindow> ("Rename preset", "Enter a new name:", juce::MessageBoxIconType::NoIcon);
        renameWindow->addTextEditor ("name", presets.getPresets()[static_cast<size_t> (idx)].name);
        renameWindow->addButton ("RENAME", 1, juce::KeyPress (juce::KeyPress::returnKey));
        renameWindow->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        addAndMakeVisible (*renameWindow);
        renameWindow->setCentrePosition (getLocalBounds().getCentre());

        juce::Component::SafePointer<PresetBrowser> safe (this);
        renameWindow->enterModalState (true, juce::ModalCallbackFunction::create ([safe, idx] (int result)
        {
            if (safe == nullptr || safe->renameWindow == nullptr)
                return;

            const auto newName = safe->renameWindow->getTextEditorContents ("name");
            safe->renameWindow.reset();
            if (result != 1)
                return;

            juce::String error;
            if (! safe->presets.renameUserPreset (idx, newName, error) && error.isNotEmpty())
                safe->messageBox = juce::AlertWindow::showScopedAsync (juce::MessageBoxOptions().withTitle ("Rename failed").withMessage (error)
                                                                           .withButton ("OK").withAssociatedComponent (safe.getComponent()),
                                                                       nullptr);
            safe->rebuildList();
        }), false);
    };

    deleteButton.onClick = [this]
    {
        const auto idx = selectedPresetIndex();
        if (idx < 0 || presets.getPresets()[static_cast<size_t> (idx)].isFactory)
            return;

        juce::Component::SafePointer<PresetBrowser> safe (this);
        const auto name = presets.getPresets()[static_cast<size_t> (idx)].name;
        messageBox = juce::AlertWindow::showScopedAsync (juce::MessageBoxOptions()
                                                             .withTitle ("Delete preset")
                                                             .withMessage ("Delete \"" + name + "\"? This cannot be undone.")
                                                             .withButton ("DELETE")
                                                             .withButton ("CANCEL")
                                                             .withAssociatedComponent (this),
                                                         [safe, idx] (int result)
                                                         {
                                                             if (safe != nullptr && result == 1)
                                                             {
                                                                 safe->presets.deleteUserPreset (idx);
                                                                 safe->rebuildList();
                                                             }
                                                         });
    };

    setWantsKeyboardFocus (true);
}

void PresetBrowser::open()
{
    snapshot = presets.takeSnapshot();
    presets.refreshUserPresets();
    filterIndex = 0;
    rebuildList();
    setVisible (true);
    toFront (true);
    list.grabKeyboardFocus();
}

void PresetBrowser::close (bool keep)
{
    if (! keep)
        presets.restoreSnapshot (snapshot);

    setVisible (false);
    if (onClose)
        onClose();
}

bool PresetBrowser::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        close (false);
        return true;
    }

    if (key == juce::KeyPress::returnKey)
    {
        close (true);
        return true;
    }

    return false;
}

void PresetBrowser::selectFilter (int index)
{
    filterIndex = index;
    rebuildList();
}

void PresetBrowser::rebuildList()
{
    visible.clear();
    const auto& all = presets.getPresets();

    for (size_t i = 0; i < all.size(); ++i)
    {
        const auto& p = all[i];
        const auto f = filters[filterIndex];
        const auto include = filterIndex == 0 || (filterIndex == 1 && p.isFactory) || (filterIndex == 2 && ! p.isFactory)
                          || (filterIndex > 2 && p.category.toUpperCase() == f);
        if (include)
            visible.push_back (static_cast<int> (i));
    }

    for (int i = 0; i < filterButtons.size(); ++i)
        filterButtons[i]->setToggleState (i == filterIndex, juce::dontSendNotification);

    list.updateContent();

    suppressLoad = true;
    list.deselectAllRows();
    for (size_t r = 0; r < visible.size(); ++r)
        if (visible[r] == presets.getCurrentIndex())
        {
            list.selectRow (static_cast<int> (r));
            break;
        }
    suppressLoad = false;

    list.repaint();
    updateInfo();
}

int PresetBrowser::selectedPresetIndex() const
{
    const auto row = list.getSelectedRow();
    return juce::isPositiveAndBelow (row, static_cast<int> (visible.size())) ? visible[static_cast<size_t> (row)] : -1;
}

int PresetBrowser::getNumRows() { return static_cast<int> (visible.size()); }

void PresetBrowser::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, static_cast<int> (visible.size())))
        return;

    const auto& p = presets.getPresets()[static_cast<size_t> (visible[static_cast<size_t> (row)])];
    const auto area = juce::Rectangle<float> (0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height)).reduced (2.0f, 1.0f);

    if (selected)
    {
        g.setColour (Colours::amber.withAlpha (0.16f));
        g.fillRoundedRectangle (area, 3.0f);
        g.setColour (Colours::amber);
        g.fillRoundedRectangle (area.withWidth (3.0f), 1.5f);
    }
    else if (row % 2 == 1)
    {
        g.setColour (juce::Colours::white.withAlpha (0.025f));
        g.fillRect (area);
    }

    auto text = area.reduced (12.0f, 0.0f);
    g.setFont (Fonts::get().labelMedium (17.0f));
    g.setColour (selected ? Colours::amber : Colours::silkscreen.withAlpha (0.9f));
    g.drawText (p.name, text, juce::Justification::centredLeft, true);

    auto right = juce::String (p.isFactory ? "" : "USER   ") + p.category.toUpperCase();
    if (presets.getStartupPresetName() == p.name)
        right = "STARTUP   " + right;
    g.setFont (Fonts::get().label (12.0f));
    g.setColour (Colours::silkscreen.withAlpha (0.4f));
    g.drawText (right, text, juce::Justification::centredRight, true);
}

void PresetBrowser::selectedRowsChanged (int lastRowSelected)
{
    if (suppressLoad || ! juce::isPositiveAndBelow (lastRowSelected, static_cast<int> (visible.size())))
        return;

    // Tweak while browsing: the preset is heard immediately.
    presets.loadPreset (visible[static_cast<size_t> (lastRowSelected)]);
    updateInfo();
}

void PresetBrowser::listBoxItemClicked (int, const juce::MouseEvent&) {}

void PresetBrowser::listBoxItemDoubleClicked (int, const juce::MouseEvent&) { close (true); }

void PresetBrowser::updateInfo()
{
    const auto idx = selectedPresetIndex();
    const auto isUser = idx >= 0 && ! presets.getPresets()[static_cast<size_t> (idx)].isFactory;
    renameButton.setEnabled (isUser);
    deleteButton.setEnabled (isUser);
    startupButton.setEnabled (idx >= 0);
    repaint (infoArea);
}

void PresetBrowser::resized()
{
    const auto area = getLocalBounds();
    auto y = 22;
    for (auto* b : filterButtons)
    {
        b->setBounds (20, y, 168, 21);
        y += 24;
    }

    list.setBounds (204, 18, 470, area.getHeight() - 36);
    infoArea = { 692, 18, area.getWidth() - 710, 170 };

    const auto bx = infoArea.getX();
    const auto bw = (infoArea.getWidth() - 8) / 2;
    renameButton.setBounds (bx, 196, bw, 26);
    deleteButton.setBounds (bx + bw + 8, 196, bw, 26);
    startupButton.setBounds (bx, 228, bw, 26);
    folderButton.setBounds (bx + bw + 8, 228, bw, 26);
    okButton.setBounds (bx, area.getHeight() - 52, bw, 34);
    cancelButton.setBounds (bx + bw + 8, area.getHeight() - 52, bw, 34);
}

void PresetBrowser::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    paintDarkPanel (g, area.reduced (2.0f), 8.0f);
    paintWell (g, list.getBounds().toFloat().expanded (4.0f));
    paintWell (g, infoArea.toFloat().expanded (4.0f));

    g.setFont (Fonts::get().labelBold (13.0f));
    g.setColour (Colours::amber.withAlpha (0.7f));
    g.drawText ("BROWSE", juce::Rectangle<float> (20.0f, 4.0f, 168.0f, 16.0f), juce::Justification::centredLeft, false);

    const auto idx = selectedPresetIndex();
    if (idx < 0)
        return;

    const auto& p = presets.getPresets()[static_cast<size_t> (idx)];
    auto info = infoArea.toFloat().reduced (12.0f, 10.0f);
    g.setColour (Colours::amber);
    g.setFont (Fonts::get().display (22.0f));
    g.drawFittedText (p.name, info.removeFromTop (28.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);

    g.setFont (Fonts::get().label (13.0f));
    g.setColour (Colours::silkscreen.withAlpha (0.6f));
    g.drawText (p.category.toUpperCase() + "   " + (p.author.isNotEmpty() ? "BY " + p.author.toUpperCase() : juce::String()), info.removeFromTop (18.0f), juce::Justification::centredLeft, true);
    info.removeFromTop (6.0f);

    juce::AttributedString description;
    description.append (p.description.isNotEmpty() ? p.description : juce::String ("No description."), Fonts::get().labelMedium (16.0f), Colours::silkscreen.withAlpha (0.85f));
    juce::TextLayout layout;
    layout.createLayout (description, info.getWidth());
    layout.draw (g, info);
}

//======================================================================================================================
SavePresetDialog::SavePresetDialog (PresetManager& p) : presets (p)
{
    name.setTextToShowWhenEmpty ("Preset name", Colours::silkscreen.withAlpha (0.3f));
    name.setFont (Fonts::get().labelMedium (18.0f));
    name.setIndents (8, 6);
    name.onReturnKey = [this] { save(); };
    name.onEscapeKey = [this] { if (onClose) onClose(); };

    description.setMultiLine (true, true);
    description.setReturnKeyStartsNewLine (true);
    description.setTextToShowWhenEmpty ("Description (optional)", Colours::silkscreen.withAlpha (0.3f));
    description.setFont (Fonts::get().labelMedium (16.0f));
    description.setIndents (8, 6);

    category.addItemList (PresetManager::getCategories(), 1);
    category.addItem ("User", 100);
    category.setSelectedId (100);

    saveButton.onClick = [this] { save(); };
    cancelButton.onClick = [this] { if (onClose) onClose(); };

    for (auto* c : std::initializer_list<juce::Component*> { &name, &description, &category, &saveButton, &cancelButton })
        addAndMakeVisible (c);
}

void SavePresetDialog::open()
{
    error.clear();
    const auto current = presets.getCurrentPresetName();
    const auto idx = presets.getCurrentIndex();
    const auto isUser = juce::isPositiveAndBelow (idx, static_cast<int> (presets.getPresets().size())) && ! presets.getPresets()[static_cast<size_t> (idx)].isFactory;
    name.setText (isUser ? current : current + " (Mine)", false);
    description.clear();
    setVisible (true);
    toFront (true);
    name.grabKeyboardFocus();
    name.selectAll();
}

void SavePresetDialog::save()
{
    juce::String err;
    if (presets.saveUserPreset (name.getText(), category.getText(), description.getText(), err))
    {
        if (onClose)
            onClose();
        return;
    }

    error = err;
    repaint();
}

bool SavePresetDialog::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        if (onClose)
            onClose();
        return true;
    }
    return false;
}

void SavePresetDialog::resized()
{
    panel = juce::Rectangle<int> (420, 300).withCentre (getLocalBounds().getCentre());
    auto inner = panel.reduced (24, 20);
    inner.removeFromTop (34);
    name.setBounds (inner.removeFromTop (34));
    inner.removeFromTop (10);
    category.setBounds (inner.removeFromTop (30));
    inner.removeFromTop (10);
    description.setBounds (inner.removeFromTop (80));
    inner.removeFromTop (12);
    auto buttons = inner.removeFromBottom (34);
    cancelButton.setBounds (buttons.removeFromRight (110));
    buttons.removeFromRight (10);
    saveButton.setBounds (buttons.removeFromRight (110));
}

void SavePresetDialog::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.55f));
    paintDarkPanel (g, panel.toFloat(), 8.0f);

    g.setFont (Fonts::get().labelBold (17.0f));
    g.setColour (Colours::amber);
    g.drawText ("SAVE PRESET", panel.reduced (24, 18).removeFromTop (24).toFloat(), juce::Justification::centredLeft, false);

    if (error.isNotEmpty())
    {
        g.setFont (Fonts::get().labelMedium (15.0f));
        g.setColour (Colours::ledRed);
        g.drawText (error, juce::Rectangle<float> (static_cast<float> (panel.getX() + 24), static_cast<float> (saveButton.getY() - 4), 160.0f, 40.0f),
                    juce::Justification::centredLeft, true);
    }
}

} // namespace afterglow::ui
