#include "Strings.h"

#include "Settings.h"
#include "Ue.h"

#include <fstream>
#include <unordered_map>

namespace strings
{
	namespace
	{
		std::unordered_map<std::string, std::string> g_texts;
		std::string                                  g_language;   // empty = never loaded

		std::string ToUtf8(std::wstring_view a_w)
		{
			if (a_w.empty()) {
				return {};
			}
			const int n = ::WideCharToMultiByte(CP_UTF8, 0, a_w.data(), static_cast<int>(a_w.size()), nullptr, 0, nullptr, nullptr);
			std::string out(static_cast<std::size_t>(n > 0 ? n : 0), '\0');
			if (n > 0) {
				::WideCharToMultiByte(CP_UTF8, 0, a_w.data(), static_cast<int>(a_w.size()), out.data(), n, nullptr, nullptr);
			}
			return out;
		}

		// -1 no file, -2 not UTF-16LE with a BOM, else how many keys were read
		int ReadInto(const std::filesystem::path& a_path, bool a_overwrite)
		{
			std::ifstream in(a_path, std::ios::binary);
			if (!in) {
				return -1;
			}
			const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			if (bytes.size() < 2 || static_cast<unsigned char>(bytes[0]) != 0xFF || static_cast<unsigned char>(bytes[1]) != 0xFE) {
				return -2;
			}
			const std::wstring text(reinterpret_cast<const wchar_t*>(bytes.data() + 2), (bytes.size() - 2) / 2);
			int added = 0;
			for (std::size_t pos = 0; pos < text.size();) {
				auto eol = text.find(L'\n', pos);
				if (eol == std::wstring::npos) {
					eol = text.size();
				}
				std::wstring line = text.substr(pos, eol - pos);
				pos = eol + 1;
				if (!line.empty() && line.back() == L'\r') {
					line.pop_back();
				}
				const auto tab = line.find(L'\t');
				if (line.empty() || line[0] != L'$' || tab == std::wstring::npos) {
					continue;
				}
				std::string key = ToUtf8(std::wstring_view(line).substr(1, tab - 1));
				if (!a_overwrite && g_texts.contains(key)) {
					continue;
				}
				g_texts[std::move(key)] = ToUtf8(std::wstring_view(line).substr(tab + 1));
				++added;
			}
			return added;
		}

		// the game's culture code ("en", "fr-FR", "zh-Hans" ...) as our file name's language
		std::string GameLanguage()
		{
			static auto* lib = ue::Class(L"/Script/Engine.KismetInternationalizationLibrary");
			ue::Call c(lib ? lib->GetDefaultObject(false) : nullptr, L"GetCurrentLanguage");
			if (!c || !c.Run()) {
				return "english";
			}
			auto* ret = static_cast<UE::FString*>(c.At("ReturnValue"));
			std::string code = ret ? pe::Utf8(*ret) : std::string();
			if (ret) {
				if (void* data = *reinterpret_cast<void**>(ret)) {
					UE::FMemory::Free(data);   // the engine's string
				}
			}
			std::transform(code.begin(), code.end(), code.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
			static const std::pair<const char*, const char*> kCodes[] = {
				{ "ja", "japanese" }, { "ko", "korean" }, { "zh", "chinese" }, { "ru", "russian" }, { "de", "german" }, { "fr", "french" },
				{ "es", "spanish" }, { "it", "italian" }, { "pl", "polish" }, { "cs", "czech" },
			};
			for (const auto& [prefix, name] : kCodes) {
				if (code.starts_with(prefix)) {
					return name;
				}
			}
			return "english";
		}
	}

	void Refresh()
	{
		const std::string lang = GameLanguage();
		if (lang == g_language) {
			return;
		}
		g_texts.clear();
		const auto dir = settings::PluginFolder() / L"SimpleLoadoutSystem" / L"Translations";
		const auto file = [&](const std::string& l) { return dir / (L"SimpleLoadoutSystem_" + std::wstring(l.begin(), l.end()) + L".txt"); };
		const int own = ReadInto(file(lang), true);
		if (lang != "english") {
			ReadInto(file("english"), false);
		}
		if (own < 0) {
			logger::warn("strings: no {} file ({}) - English is used", lang, own == -1 ? "missing" : "not UTF-16LE with a BOM");
		}
		g_language = lang;
		logger::info("strings: {} loaded ({} texts)", lang, g_texts.size());
	}

	const char* Get(const char* a_key, const char* a_english)
	{
		const auto it = g_texts.find(a_key);
		return it != g_texts.end() && !it->second.empty() ? it->second.c_str() : a_english;
	}

	const std::string& Language()
	{
		return g_language;
	}
}
