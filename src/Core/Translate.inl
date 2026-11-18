// Part of the camp code: included inside main.cpp's anonymous namespace (not compiled on its own).
// ---- translation hook (the same format as Free Cooking's and NeedsHUD's; his rule 2026-11-09: every mod ships an
// English file for translators). Skyrim's own translation file format: Interface/Translations/SolidGroundCamping_<LANGUAGE>.txt,
// UTF-16 LE with BOM, one "$SGC_English text<TAB>Translated text" per line, picked by the game's language (sLanguage
// in Skyrim.ini, or "language = <LANGUAGE>" in SolidGroundCamping.ini for a translator's test). The $SGC_ prefix keeps
// our lines apart in the game's shared table. A missing file or line shows the English. Read once at kDataLoaded,
// only read afterwards (so any thread may call Tr)
	std::unordered_map<std::string, std::string> g_tr;
	constexpr std::string_view TR_PREFIX = "$SGC_";

	std::string TrUtf16ToUtf8(const std::u16string& a_in)
	{
		std::string out;
		for (size_t i = 0; i < a_in.size(); ++i) {
			std::uint32_t c = a_in[i];
			if (c >= 0xD800 && c <= 0xDBFF && i + 1 < a_in.size() && a_in[i + 1] >= 0xDC00 && a_in[i + 1] <= 0xDFFF) {
				c = 0x10000 + ((c - 0xD800) << 10) + (a_in[i + 1] - 0xDC00);
				++i;
			}
			if (c < 0x80) {
				out += static_cast<char>(c);
			} else if (c < 0x800) {
				out += static_cast<char>(0xC0 | (c >> 6));
				out += static_cast<char>(0x80 | (c & 0x3F));
			} else if (c < 0x10000) {
				out += static_cast<char>(0xE0 | (c >> 12));
				out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
				out += static_cast<char>(0x80 | (c & 0x3F));
			} else {
				out += static_cast<char>(0xF0 | (c >> 18));
				out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
				out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
				out += static_cast<char>(0x80 | (c & 0x3F));
			}
		}
		return out;
	}

	// "language = GERMAN" in SolidGroundCamping.ini (the settings file's only text value; CampSettings reads numbers)
	std::string TrLanguageOverride()
	{
		std::ifstream ini(CAMP_INI);
		std::string line;
		while (ini && std::getline(ini, line)) {
			const auto eq = line.find('=');
			if (eq == std::string::npos) {
				continue;
			}
			auto key = line.substr(0, eq);
			std::erase_if(key, [](char c) { return std::isspace(static_cast<unsigned char>(c)); });
			if (key != "language") {
				continue;
			}
			auto value = line.substr(eq + 1);
			if (const auto semi = value.find(';'); semi != std::string::npos) {
				value.erase(semi);
			}
			std::erase_if(value, [](char c) { return std::isspace(static_cast<unsigned char>(c)); });
			return value;
		}
		return {};
	}

	void LoadTranslation()
	{
		std::string lang = "ENGLISH";
		if (auto ini = RE::INISettingCollection::GetSingleton()) {
			if (auto s = ini->GetSetting("sLanguage:General")) {
				if (const char* v = s->GetString(); v && *v) {
					lang = v;
				}
			}
		}
		if (const auto over = TrLanguageOverride(); !over.empty()) {
			lang = over;
			logger::info("Translation: SolidGroundCamping.ini asks for {}", lang);
		}
		for (auto& ch : lang) {
			ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
		}
		g_tr.clear();
		if (lang == "ENGLISH") {
			return;
		}
		const std::string path = "Data/Interface/Translations/SolidGroundCamping_" + lang + ".txt";
		std::ifstream f(path, std::ios::binary);
		if (!f) {
			logger::info("Translation: no {} found, the camp's text stays in English", path.substr(5));
			return;
		}
		const std::string raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
		std::string text;
		if (raw.size() >= 2 && static_cast<unsigned char>(raw[0]) == 0xFF && static_cast<unsigned char>(raw[1]) == 0xFE) {
			std::u16string u;
			for (size_t i = 2; i + 1 < raw.size(); i += 2) {
				u += static_cast<char16_t>(static_cast<unsigned char>(raw[i]) | (static_cast<unsigned char>(raw[i + 1]) << 8));
			}
			text = TrUtf16ToUtf8(u);
		} else {  // saved as UTF-8 by mistake: read it anyway (skipping a UTF-8 BOM)
			text = raw.size() >= 3 && raw.compare(0, 3, "\xEF\xBB\xBF") == 0 ? raw.substr(3) : raw;
		}
		int lines = 0, used = 0;
		size_t pos = 0;
		while (pos < text.size()) {
			size_t end = text.find('\n', pos);
			if (end == std::string::npos) {
				end = text.size();
			}
			std::string line = text.substr(pos, end - pos);
			pos = end + 1;
			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}
			const size_t tab = line.find('\t');
			if (line.rfind(TR_PREFIX, 0) != 0 || tab == std::string::npos) {
				continue;
			}
			++lines;
			const std::string key = line.substr(TR_PREFIX.size(), tab - TR_PREFIX.size()), value = line.substr(tab + 1);
			if (!value.empty()) {
				g_tr[key] = value;
				++used;
			}
		}
		logger::info("Translation: {} read, {} of {} lines translated", path.substr(5), used, lines);
	}

	// the player's language for this English text (the English itself when there is no line for it)
	const char* Tr(const char* a_en)
	{
		if (!a_en || g_tr.empty()) {
			return a_en;
		}
		const auto it = g_tr.find(a_en);
		return it != g_tr.end() ? it->second.c_str() : a_en;
	}

	// a translated sentence with slots: the key is the whole English sentence with {0}, {1}... where names go, so a
	// translation can put them in any order. A translation with broken slots shows the English
	template <class... Args>
	std::string TrF(const char* a_en, const Args&... a_args)
	{
		try {
			return std::vformat(Tr(a_en), std::make_format_args(a_args...));
		} catch (const std::format_error&) {
			try {
				return std::vformat(a_en, std::make_format_args(a_args...));
			} catch (const std::format_error&) {
				return a_en;
			}
		}
	}
