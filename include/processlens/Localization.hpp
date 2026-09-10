#pragma once

namespace processlens {

enum class Language { Russian, English };

constexpr const wchar_t* Tr(Language language, const wchar_t* russian, const wchar_t* english) noexcept {
    return language == Language::Russian ? russian : english;
}

} // namespace processlens
