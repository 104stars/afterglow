#include "PresetManager.h"
#include "../Parameters.h"

namespace afterglow
{
namespace
{
    bool isPresetParameter (const juce::String& id) { return id != ParamIDs::quality; }
} // namespace

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& s, juce::UndoManager* um)
    : state (s), undo (um)
{
    for (const auto& id : allParameterIds())
        state.addParameterListener (id, this);

    refreshUserPresets();
    currentIndex = 0;
    currentName = presets.empty() ? juce::String ("Init") : presets.front().name;
}

PresetManager::~PresetManager()
{
    for (const auto& id : allParameterIds())
        state.removeParameterListener (id, this);
}

juce::StringArray PresetManager::getCategories()
{
    return { "Drums", "Bass", "Keys", "Guitar", "Vocals", "Mix Bus", "Lo-Fi", "Sound Design", "Post" };
}

juce::File PresetManager::getUserPresetDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("Afterglow")
        .getChildFile ("Presets");
}

std::unique_ptr<juce::PropertiesFile> PresetManager::openSettings() const
{
    juce::PropertiesFile::Options options;
    options.applicationName = "Afterglow";
    options.filenameSuffix = "settings";
    options.folderName = "Afterglow";
    options.osxLibrarySubFolder = "Application Support";
    options.commonToAllUsers = false;
    return std::make_unique<juce::PropertiesFile> (options);
}

void PresetManager::parameterChanged (const juce::String& parameterID, float)
{
    if (loadingDepth.load() == 0 && isPresetParameter (parameterID))
        dirty.store (true);
}

void PresetManager::runWhileLoading (const std::function<void()>& fn)
{
    ++loadingDepth;
    fn();
    --loadingDepth;
}

bool PresetManager::readPresetFile (const juce::File& file, Preset& preset)
{
    const auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName ("AfterglowPreset"))
        return false;

    preset.name = xml->getStringAttribute ("name", file.getFileNameWithoutExtension());
    preset.category = xml->getStringAttribute ("category", "User");
    preset.author = xml->getStringAttribute ("author");
    preset.description = xml->getStringAttribute ("description");
    preset.isFactory = false;
    preset.file = file;
    preset.values.clear();

    for (auto* param : xml->getChildWithTagNameIterator ("Parameter"))
        preset.values.emplace_back (param->getStringAttribute ("id"), static_cast<float> (param->getDoubleAttribute ("value")));

    return true;
}

void PresetManager::refreshUserPresets()
{
    const auto previousName = currentName;
    presets = getFactoryPresets();

    std::vector<Preset> user;
    const auto dir = getUserPresetDirectory();

    if (dir.isDirectory())
    {
        for (const auto& entry : juce::RangedDirectoryIterator (dir, false, juce::String ("*") + fileExtension))
        {
            Preset p;
            if (readPresetFile (entry.getFile(), p))
                user.push_back (std::move (p));
        }
    }

    std::sort (user.begin(), user.end(), [] (const Preset& a, const Preset& b) { return a.name.compareNatural (b.name) < 0; });

    for (auto& p : user)
        presets.push_back (std::move (p));

    currentIndex = -1;
    for (size_t i = 0; i < presets.size(); ++i)
        if (presets[i].name == previousName)
            currentIndex = static_cast<int> (i);

    if (onPresetListChanged)
        onPresetListChanged();
}

std::vector<std::pair<juce::String, float>> PresetManager::captureValues() const
{
    std::vector<std::pair<juce::String, float>> values;

    for (const auto& id : allParameterIds())
    {
        if (! isPresetParameter (id))
            continue;

        if (auto* param = state.getParameter (id))
            values.emplace_back (id, param->convertFrom0to1 (param->getValue()));
    }

    return values;
}

void PresetManager::applyValues (const std::vector<std::pair<juce::String, float>>& values)
{
    runWhileLoading ([&]
    {
        // Every preset starts from the defaults, so presets only need to list what they change.
        for (const auto& id : allParameterIds())
        {
            if (! isPresetParameter (id))
                continue;

            if (auto* param = state.getParameter (id))
            {
                param->beginChangeGesture();
                param->setValueNotifyingHost (param->getDefaultValue());
                param->endChangeGesture();
            }
        }

        for (const auto& [id, value] : values)
        {
            if (! isPresetParameter (id))
                continue;

            if (auto* param = state.getParameter (id))
            {
                param->beginChangeGesture();
                param->setValueNotifyingHost (param->convertTo0to1 (value));
                param->endChangeGesture();
            }
        }
    });
}

bool PresetManager::loadPreset (int index)
{
    if (! juce::isPositiveAndBelow (index, static_cast<int> (presets.size())))
        return false;

    if (undo != nullptr)
        undo->beginNewTransaction ("Load preset");

    const auto& preset = presets[static_cast<size_t> (index)];
    applyValues (preset.values);
    currentIndex = index;
    currentName = preset.name;
    dirty.store (false);
    return true;
}

bool PresetManager::loadPresetByName (const juce::String& name)
{
    for (size_t i = 0; i < presets.size(); ++i)
        if (presets[i].name == name)
            return loadPreset (static_cast<int> (i));

    return false;
}

void PresetManager::loadNext()
{
    if (presets.empty())
        return;
    loadPreset ((currentIndex + 1) % static_cast<int> (presets.size()));
}

void PresetManager::loadPrevious()
{
    if (presets.empty())
        return;
    const auto count = static_cast<int> (presets.size());
    loadPreset ((currentIndex - 1 + count) % count);
}

bool PresetManager::saveUserPreset (const juce::String& name, const juce::String& category, const juce::String& description, juce::String& error)
{
    const auto cleanName = name.trim();
    if (cleanName.isEmpty())
    {
        error = "Please enter a preset name.";
        return false;
    }

    for (const auto& p : presets)
    {
        if (p.isFactory && p.name.equalsIgnoreCase (cleanName))
        {
            error = "A factory preset already uses this name.";
            return false;
        }
    }

    const auto dir = getUserPresetDirectory();
    if (! dir.isDirectory() && ! dir.createDirectory())
    {
        error = "Could not create " + dir.getFullPathName();
        return false;
    }

    juce::XmlElement xml ("AfterglowPreset");
    xml.setAttribute ("version", 1);
    xml.setAttribute ("name", cleanName);
    xml.setAttribute ("category", category.isNotEmpty() ? category : juce::String ("User"));
    xml.setAttribute ("author", juce::SystemStats::getFullUserName());
    xml.setAttribute ("description", description);

    for (const auto& [id, value] : captureValues())
    {
        auto* child = xml.createNewChildElement ("Parameter");
        child->setAttribute ("id", id);
        child->setAttribute ("value", value);
    }

    const auto file = dir.getChildFile (juce::File::createLegalFileName (cleanName) + fileExtension);
    if (! xml.writeTo (file))
    {
        error = "Could not write " + file.getFullPathName();
        return false;
    }

    currentName = cleanName;
    refreshUserPresets();
    dirty.store (false);
    return true;
}

bool PresetManager::deleteUserPreset (int index)
{
    if (! juce::isPositiveAndBelow (index, static_cast<int> (presets.size())))
        return false;

    const auto& preset = presets[static_cast<size_t> (index)];
    if (preset.isFactory || ! preset.file.deleteFile())
        return false;

    if (getStartupPresetName() == preset.name)
        setStartupPreset (-1);

    refreshUserPresets();
    return true;
}

bool PresetManager::renameUserPreset (int index, const juce::String& newName, juce::String& error)
{
    if (! juce::isPositiveAndBelow (index, static_cast<int> (presets.size())))
        return false;

    auto preset = presets[static_cast<size_t> (index)];
    const auto cleanName = newName.trim();

    if (preset.isFactory)
    {
        error = "Factory presets cannot be renamed.";
        return false;
    }

    if (cleanName.isEmpty())
    {
        error = "Please enter a preset name.";
        return false;
    }

    auto xml = juce::XmlDocument::parse (preset.file);
    if (xml == nullptr)
    {
        error = "Could not read the preset file.";
        return false;
    }

    xml->setAttribute ("name", cleanName);
    const auto target = preset.file.getParentDirectory().getChildFile (juce::File::createLegalFileName (cleanName) + fileExtension);

    if (target != preset.file && target.exists())
    {
        error = "A preset with this name already exists.";
        return false;
    }

    if (! xml->writeTo (target))
    {
        error = "Could not write the preset file.";
        return false;
    }

    if (target != preset.file)
        preset.file.deleteFile();

    if (currentName == preset.name)
        currentName = cleanName;

    if (getStartupPresetName() == preset.name)
    {
        auto settings = openSettings();
        settings->setValue ("startupPreset", cleanName);
    }

    refreshUserPresets();
    return true;
}

void PresetManager::setCurrentPresetName (const juce::String& name, bool dirtyFlag)
{
    if (name.isNotEmpty())
        currentName = name;

    currentIndex = -1;
    for (size_t i = 0; i < presets.size(); ++i)
        if (presets[i].name == currentName)
            currentIndex = static_cast<int> (i);

    dirty.store (dirtyFlag);
}

void PresetManager::setStartupPreset (int index)
{
    auto settings = openSettings();

    if (juce::isPositiveAndBelow (index, static_cast<int> (presets.size())))
        settings->setValue ("startupPreset", presets[static_cast<size_t> (index)].name);
    else
        settings->removeValue ("startupPreset");

    settings->saveIfNeeded();
}

juce::String PresetManager::getStartupPresetName() const
{
    return openSettings()->getValue ("startupPreset");
}

void PresetManager::loadStartupPresetIfAny()
{
    const auto name = getStartupPresetName();
    if (name.isNotEmpty() && loadPresetByName (name))
        return;

    // Otherwise start from the parameter defaults, which match the first factory preset.
    currentIndex = presets.empty() ? -1 : 0;
    currentName = presets.empty() ? juce::String ("Init") : presets.front().name;
    dirty.store (false);
}

PresetManager::Snapshot PresetManager::takeSnapshot() const
{
    return { captureValues(), currentName, currentIndex, dirty.load() };
}

void PresetManager::restoreSnapshot (const Snapshot& snapshot)
{
    applyValues (snapshot.values);
    currentName = snapshot.name;
    currentIndex = snapshot.index;
    dirty.store (snapshot.dirty);
}

} // namespace afterglow
