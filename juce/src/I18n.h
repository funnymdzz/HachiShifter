#pragma once

#include <juce_core/juce_core.h>

namespace hachi
{
class I18n
{
public:
    enum class Language { zhCN, zhTW, jaJP, koKR, enUS };

    I18n();
    void setLanguage(Language languageToUse) { language = languageToUse; }
    [[nodiscard]] Language getLanguage() const { return language; }
    [[nodiscard]] juce::String text(const juce::String& key) const;

private:
    Language language = Language::zhCN;
};
}

