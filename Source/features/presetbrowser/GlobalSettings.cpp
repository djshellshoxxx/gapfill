#include "GlobalSettings.h"

namespace gapfill::settings
{

namespace
{
    std::shared_ptr<GlobalSettings>& sharedSlot()
    {
        static std::shared_ptr<GlobalSettings> instance;
        return instance;
    }

    const char* favouritesKey = "favourites";
}

GlobalSettings::GlobalSettings (const juce::File& f) : file (f)
{
    juce::PropertiesFile::Options options;
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    options.millisecondsBeforeSaving = 500;       // debounce: written 500 ms after the last change
    options.processLock = nullptr;

    // A file that exists but is not valid XML is treated as unreadable: defaults apply.
    if (file.existsAsFile())
    {
        const auto parsed = juce::XmlDocument::parse (file);
        usable = parsed != nullptr && file.getSize() < 2 * 1024 * 1024;
    }
    else
    {
        usable = file.getParentDirectory().exists() || file.getParentDirectory().createDirectory().wasOk();
    }

    props = std::make_unique<juce::PropertiesFile> (file, options);

    if (! usable)
        props->clear();                           // ignore whatever half-parsed content there was
}

GlobalSettings::~GlobalSettings()
{
    flush();
}

std::shared_ptr<GlobalSettings> GlobalSettings::shared()
{
    auto& slot = sharedSlot();

    if (slot == nullptr)
        slot = std::make_shared<GlobalSettings> (defaultFile());

    return slot;
}

juce::File GlobalSettings::defaultFile()
{
    if (const auto env = juce::SystemStats::getEnvironmentVariable ("GAPFILL_SETTINGS_FILE", {}); env.isNotEmpty())
        return juce::File (env);

   #ifdef GAPFILL_UNIT_TESTS
    return juce::File::getSpecialLocation (juce::File::tempDirectory)
             .getChildFile ("gapfill-unit-test-settings-" + juce::String::toHexString (juce::Time::currentTimeMillis()) + ".xml");
   #else
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
             .getChildFile ("GapFill").getChildFile ("settings.xml");
   #endif
}

void GlobalSettings::useFileForTests (const juce::File& f)
{
    sharedSlot() = std::make_shared<GlobalSettings> (f);
}

int GlobalSettings::getInt (const juce::String& key, int fallback) const
{
    return props->getIntValue (key, fallback);
}

void GlobalSettings::setInt (const juce::String& key, int value)
{
    props->setValue (key, value);
}

bool GlobalSettings::getBool (const juce::String& key, bool fallback) const
{
    return props->getBoolValue (key, fallback);
}

void GlobalSettings::setBool (const juce::String& key, bool value)
{
    props->setValue (key, value);
}

juce::StringArray GlobalSettings::getFavourites() const
{
    auto list = juce::StringArray::fromLines (props->getValue (favouritesKey, {}));
    list.removeEmptyStrings();
    list.removeDuplicates (false);

    while (list.size() > kMaxFavourites)
        list.remove (list.size() - 1);

    return list;
}

bool GlobalSettings::isFavourite (const juce::String& identity) const
{
    return getFavourites().contains (identity);
}

bool GlobalSettings::setFavourite (const juce::String& identity, bool on)
{
    if (identity.isEmpty() || identity.length() > kMaxFavouriteLength || identity.containsAnyOf ("\r\n"))
        return false;

    auto list = getFavourites();

    if (on)
    {
        if (! list.contains (identity))
        {
            if (list.size() >= kMaxFavourites)
                return false;

            list.add (identity);
        }
    }
    else
    {
        list.removeString (identity);
    }

    props->setValue (favouritesKey, list.joinIntoString ("\n"));
    return on;
}

void GlobalSettings::flush()
{
    if (props != nullptr && usable)
        props->saveIfNeeded();
}

} // namespace gapfill::settings
