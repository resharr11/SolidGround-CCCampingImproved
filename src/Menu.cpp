// Solid Ground's settings page in SKSE Menu Framework's Mod Control Panel (his design 2026-11-16: sections Camp and
// fire, Mound, Tents, Advanced layout; sliders with Reset buttons, no presets). Optional: without the framework there
// is no page and SolidGroundCamping.ini works as before.
//
// Changes are saved to the player's own file, Documents\My Games\Skyrim Special Edition\SKSE\SolidGroundCamping.ini
// (never Data, never the shipped file; it wins over the shipped file), and apply to the next camp the player makes.
// Only values the player changed are written; Reset takes a value out of the file again, so the mod's own default (or
// the shipped file's line, or a kit's) applies.
//
// SKSE Menu Framework 3 (Nexus 120352, by Thiago). Its API header SKSEMenuFramework.h comes from
// github.com/QTR-Modding/SKSE-Menu-Framework-3-API (LGPL-2.1, licence beside it; the framework's README: "The header
// file has a license that does not require mods using it as a library to be open source"). It finds the framework's
// functions at run time (GetModuleHandle + GetProcAddress): nothing is linked. (Same setup as Free Cooking's and
// NeedsHUD's Menu.cpp.)

#include <algorithm>
#include <cfloat>   // FLT_MIN / FLT_MAX in the header
#include <cmath>
#include <cstdarg>  // va_list in the header
#include <format>
#include <map>
#include <string>
#include <vector>
#include "SKSEMenuFramework.h"

#include "Settings.h"

namespace
{
	namespace UI = ImGuiMCP;
	using SGC::Tr;

	constexpr float CM_PER_UNIT = 1.4286f;     // 1 game unit = 1.4286 cm (128 units = 6 feet = 1.83 m)
	constexpr float INCH_PER_UNIT = 0.5625f;   // 1 game unit = 0.5625 inch

	std::map<std::string, float> g_user;  // the player's own file, as the page edits it
	bool                         g_loaded = false;

	void Load()
	{
		if (!g_loaded) {
			g_user = SGC::UserSettings();
			g_loaded = true;
		}
	}

	bool g_saveFailed = false;  // the last save didn't reach the file: said under the page (Codex review 2026-11-16c)
	void Save()
	{
		g_saveFailed = !SGC::SaveUserSettings(g_user);
	}

	bool Inches() { return g_user.contains("pageinches") && g_user.at("pageinches") != 0.0f; }

	// the value the camp code uses: the player's own line, else the shipped file's, else the default
	float Value(const std::string& a_key, float a_default)
	{
		if (const auto it = g_user.find(a_key); it != g_user.end()) {
			return it->second;
		}
		return SGC::ShippedSetting(a_key.c_str(), a_default);  // (not the merged settings: they lag a second after a Reset)
	}

	void Reset(std::initializer_list<const char*> a_keys)
	{
		for (const auto* key : a_keys) {
			g_user.erase(key);
		}
		Save();
	}

	// a tooltip for the item just drawn, wrapped to a fixed width (his rule 2026-11-12: long text in a menu breaks into
	// lines by width; ImGui's own tooltip never wraps)
	void Tip(const char* a_text)
	{
		if (UI::BeginItemTooltip()) {
			UI::PushTextWrapPos(UI::GetFontSize() * 32.0f);
			UI::TextUnformatted(a_text);
			UI::PopTextWrapPos();
			UI::EndTooltip();
		}
	}

	// a slider's number format from the translation file, used only when it holds exactly one %f slot (and %% for a
	// percent sign): anything else could make the page read memory it shouldn't (Codex review 2026-11-15, Free
	// Cooking), so it falls back to the English
	const char* Format(const char* a_en)
	{
		const char* tr = Tr(a_en);
		const std::string_view s(tr ? tr : "");
		int slots = 0;
		for (std::size_t i = 0; i < s.size(); ++i) {
			if (s[i] != '%') {
				continue;
			}
			if (i + 1 < s.size() && s[i + 1] == '%') {
				++i;
				continue;
			}
			std::size_t j = i + 1;
			while (j < s.size() && (s[j] == '-' || s[j] == '+' || s[j] == ' ' || s[j] == '#' || s[j] == '0')) ++j;
			while (j < s.size() && s[j] >= '0' && s[j] <= '9') ++j;
			if (j < s.size() && s[j] == '.') {
				++j;
				while (j < s.size() && s[j] >= '0' && s[j] <= '9') ++j;
			}
			if (j >= s.size() || s[j] != 'f' || ++slots > 1) {
				return a_en;
			}
			i = j;
		}
		return slots == 1 ? tr : a_en;
	}

	// ---- one control per setting; each saves when the player lets go of it ----

	void Switch(const char* a_key, bool a_default, const char* a_label, const char* a_tip)
	{
		bool on = Value(a_key, a_default ? 1.0f : 0.0f) != 0.0f;
		if (UI::Checkbox(Tr(a_label), &on)) {
			g_user[a_key] = on ? 1.0f : 0.0f;
			Save();
		}
		Tip(Tr(a_tip));
	}

	void Number(const std::string& a_key, float a_default, float a_min, float a_max, const char* a_label, const char* a_format, const char* a_tip)
	{
		float v = Value(a_key, a_default);
		UI::PushID(a_key.c_str());
		if (UI::SliderFloat(Tr(a_label), &v, a_min, a_max, Format(a_format), UI::ImGuiSliderFlags_AlwaysClamp)) {
			g_user[a_key] = v;
		}
		if (UI::IsItemDeactivatedAfterEdit()) {
			Save();
		}
		Tip(Tr(a_tip));
		UI::PopID();
	}

	void Whole(const char* a_key, int a_default, int a_min, int a_max, const char* a_label, const char* a_tip)
	{
		int v = static_cast<int>(std::lround(Value(a_key, static_cast<float>(a_default))));
		if (UI::SliderInt(Tr(a_label), &v, a_min, a_max, "%d", UI::ImGuiSliderFlags_AlwaysClamp)) {
			g_user[a_key] = static_cast<float>(v);
		}
		if (UI::IsItemDeactivatedAfterEdit()) {
			Save();
		}
		Tip(Tr(a_tip));
	}

	// a distance: kept in game units in the file (as every other Skyrim tool and the shipped file use), shown in
	// centimetres, or inches with the units switch (his rule: metric by default, imperial as an option)
	void Distance(const char* a_key, float a_default, float a_min, float a_max, const char* a_label, const char* a_tip)
	{
		const bool inches = Inches();
		const float k = inches ? INCH_PER_UNIT : CM_PER_UNIT;
		float shown = Value(a_key, a_default) * k;
		UI::PushID(a_key);
		if (UI::SliderFloat(Tr(a_label), &shown, a_min * k, a_max * k, inches ? Format("%.0f in") : Format("%.0f cm"), UI::ImGuiSliderFlags_AlwaysClamp)) {
			g_user[a_key] = shown / k;
		}
		if (UI::IsItemDeactivatedAfterEdit()) {
			Save();
		}
		Tip(Tr(a_tip));
		UI::PopID();
	}

	void Angle(const char* a_key, float a_default, const char* a_label, const char* a_tip)
	{
		Number(a_key, a_default, -180.0f, 360.0f, a_label, "%.0f degrees", a_tip);
	}

	bool ResetButton(const char* a_id)
	{
		UI::PushID(a_id);
		const bool clicked = UI::SmallButton(Tr("Reset this section"));
		UI::PopID();
		return clicked;
	}

	void __stdcall RenderPage()
	{
		Load();

		UI::TextWrapped("%s", Tr("Changes apply to the next camp you make. Pack up and make camp again to see them."));
		UI::Spacing();

		// ---- Camp and fire ----
		UI::SeparatorText(Tr("Camp and fire"));
		Number("fireburnhours", 7.0f, 1.0f, 24.0f, "Fire burns", "%.0f hours", "Game hours a fire burns on 1 Firewood.");
		const int kitSpots = SGC::KitFireSpotCount();
		Whole("firespots", kitSpots >= 0 ? kitSpots : 6, 0, 12, "Spots round the fire",
			"Extra places round the fire where you and your followers warm your hands or sit, besides Camping's own two.");
		Distance("firespotreach", 90.0f, 40.0f, 200.0f, "Distance of those spots from the fire", "How far from the fire those spots are.");
		Switch("rug", true, "Rug under the bedroll", "A straw or fur rug under the bedroll in tents.");
		Switch("compact", true, "Keep the camp compact", "Pulls the camp's pieces closer together.");
		Distance("clearradius", 450.0f, 0.0f, 900.0f, "Plants cleared round the camp",
			"Bushes, plants and grass clumps this close to the camp are hidden while it stands, and come back when you pack up.");
		if (ResetButton("camp")) {
			Reset({ "fireburnhours", "firespots", "firespotreach", "rug", "compact", "clearradius" });
		}

		// ---- Mound ----
		UI::Spacing();
		UI::SeparatorText(Tr("Mound"));
		Switch("foundation", true, "Earth mound under the camp", "On a slope, the camp stands on a level heap of earth.");
		Distance("foundationmargin", 0.0f, 0.0f, 150.0f, "Extra rim", "More flat ground round the camp's pieces.");
		Whole("foundationextras", 0, 0, 4, "Extra small mounds",
			"Small heaps of earth round the edge of the camp, where the ground is close to the camp floor.");
		Switch("foundationplants", true, "Local plants at the mound's foot", "Plants that already grow nearby, where the mound meets the hill.");
		Switch("grass", true, "Grass on the mound", "Grass tufts on the mound.");
		Number("grassdensity", 5.0f, 0.0f, 15.0f, "Grass density", "%.0f", "More or fewer grass tufts on the mound.");
		Switch("moundpaint", true, "Paint the mound with the ground",
			"The mound takes the texture of the ground round the camp. Off: plain earth.");
		if (ResetButton("mound")) {
			Reset({ "foundation", "foundationmargin", "foundationextras", "foundationplants", "grass", "grassdensity", "moundpaint" });
		}

		// ---- Tents ----
		UI::Spacing();
		UI::SeparatorText(Tr("Tents"));
		UI::TextWrapped("%s", Tr("Every shelter is sized to fit the same spot. Size makes one larger or smaller; Opening turns it when its door doesn't face the fire."));
		const auto tents = SGC::Tents();
		static const char* const OPENINGS_EN[] = { "Front", "Left", "Back", "Right" };
		constexpr float OPENING_DEGREES[] = { 0.0f, 90.0f, 180.0f, -90.0f };  // (left = 90, as a kit's Door = left)
		std::vector<std::string> resetKeys;
		for (int i = 0; i < static_cast<int>(tents.size()); ++i) {
			const auto& tent = tents[i];
			UI::PushID(i);
			UI::AlignTextToFramePadding();
			std::string title = tent.name;
			if (tent.kit) {
				// "{0} (from a kit)": the name goes where {0} is (no std::format on a translator's text: a stray brace would throw)
				title = Tr("{0} (from a kit)");
				if (const auto at = title.find("{0}"); at != std::string::npos) {
					title.replace(at, 3, tent.name);
				} else {
					title = tent.name;
				}
			}
			UI::TextUnformatted(title.c_str());
			UI::Indent();
			const auto sizeKey = SGC::TentKey(i, "size"), doorKey = SGC::TentKey(i, "door");
			resetKeys.push_back(sizeKey);
			resetKeys.push_back(doorKey);
			Number(sizeKey, tent.size, 0.5f, tent.kit ? 2.0f : 1.5f, "Size", "%.2f", "1 fits the spot exactly.");
			const float door = Value(doorKey, tent.door);
			int pick = -1;
			for (int k = 0; k < 4; ++k) {
				if (std::abs(std::remainder(door - OPENING_DEGREES[k], 360.0f)) < 0.5f) {
					pick = k;
				}
			}
			const char* openings[5] = { Tr(OPENINGS_EN[0]), Tr(OPENINGS_EN[1]), Tr(OPENINGS_EN[2]), Tr(OPENINGS_EN[3]), Tr("Other (from a settings file)") };
			int shown = pick >= 0 ? pick : 4;
			UI::SetNextItemWidth(UI::GetFontSize() * 10.0f);
			if (UI::Combo(Tr("Opening"), &shown, openings, pick >= 0 ? 4 : 5) && shown < 4) {
				g_user[doorKey] = OPENING_DEGREES[shown];
				Save();
			}
			Tip(Tr("Which side of the model its door is on, so the camp can turn the door toward the fire."));
			UI::Unindent();
			UI::PopID();
		}
		if (ResetButton("tents")) {
			for (const auto& key : resetKeys) {
				g_user.erase(key);
			}
			Save();
		}

		// ---- Advanced layout ----
		UI::Spacing();
		if (UI::CollapsingHeader(Tr("Advanced layout"))) {
			UI::TextWrapped("%s", Tr("Where the camp's pieces stand, measured from the fire. Along: away from the tent (minus: toward it). Across: to the side."));
			UI::SeparatorText(Tr("Cooking pot"));
			Distance("shift", 160.0f, -300.0f, 300.0f, "Along", "Out from the fire, away from the tent (minus: toward it).");
			Distance("side", 0.0f, -300.0f, 300.0f, "Across", "To the side of that line.");
			Angle("turn", 180.0f, "Turn", "Turn of the pot and its stand.");
			Distance("sink", 0.0f, -30.0f, 30.0f, "Into the ground", "How far it sits into the ground.");
			Number("scale", 1.0f, 0.5f, 1.5f, "Size", "%.2f", "Size of the pot and its stand.");
			UI::SeparatorText(Tr("Chopping block"));
			Distance("blockshift", -80.0f, -300.0f, 300.0f, "Along", "Out from the fire, away from the tent (minus: toward it).");
			Distance("blockside", 170.0f, -300.0f, 300.0f, "Across", "To the side of that line.");
			Angle("blockturn", 90.0f, "Turn", "Turn of the chopping block.");
			Distance("blocksink", 0.0f, -30.0f, 30.0f, "Into the ground", "How far it sits into the ground.");
			Switch("blockfacecamp", true, "Chopping block faces the camp", "Turns the chopping block toward the middle of the camp.");
			UI::SeparatorText(Tr("Bedroll"));
			Angle("bedturn", 180.0f, "Turn", "Extra turn of the bedroll inside the shelter.");
			Number("bedscale", 0.8f, 0.5f, 1.2f, "Size", "%.2f", "Size of the bedroll.");
			Distance("bedback", 0.0f, -100.0f, 100.0f, "Back", "Moves the bedroll back from the middle of the shelter's spot.");
			Distance("bedside", 0.0f, -100.0f, 100.0f, "Across", "Moves the bedroll to the side.");
			Distance("bedlift", 1.0f, 0.0f, 10.0f, "Lift", "Lifts the bedroll off the ground a little.");
			Switch("bedtilt", true, "Bedroll follows the slope", "Tilts the bedroll with the ground under it.");
			Angle("rugturn", 90.0f, "Turn of the rug", "Turn of the rug under the bedroll.");
			UI::SeparatorText(Tr("Shelter spot"));
			Distance("tentwidth", 300.0f, 150.0f, 500.0f, "Width", "Width of the spot every shelter is sized to fit.");
			Distance("tentdepth", 210.0f, 100.0f, 400.0f, "Depth", "Depth of that spot, front to back.");
			Distance("tentfiregap", 15.0f, 0.0f, 100.0f, "Gap to the fire", "Gap between the fire's ring and the front of the shelter's spot.");
			if (ResetButton("layout")) {
				Reset({ "shift", "side", "turn", "sink", "scale", "blockshift", "blockside", "blockturn", "blocksink", "blockfacecamp", "bedturn",
					"bedscale", "bedback", "bedside", "bedlift", "bedtilt", "rugturn", "tentwidth", "tentdepth", "tentfiregap" });
			}
		}

		// ---- units and footer ----
		UI::Spacing();
		UI::Separator();
		bool inches = Inches();
		if (UI::Checkbox(Tr("Show distances in inches"), &inches)) {
			if (inches) {
				g_user["pageinches"] = 1.0f;
			} else {
				g_user.erase("pageinches");
			}
			Save();
		}
		Tip(Tr("Distances on this page are shown in inches instead of centimetres."));
		if (UI::Button(Tr("Reset this page to defaults"))) {
			const bool keepInches = Inches();
			g_user.clear();
			if (keepInches) {
				g_user["pageinches"] = 1.0f;
			}
			Save();
		}
		const auto path = SGC::UserIniPathText();
		if (path.empty()) {
			UI::TextWrapped("%s", Tr("No Documents folder was found, so these settings can't be saved."));
		} else if (g_saveFailed) {
			UI::TextWrapped("%s", Tr("Your last change couldn't be saved, so the next camp won't use it. Change the setting again to try once more."));
		} else {
			UI::TextDisabled("%s", Tr("Saved to Documents\\My Games\\...\\SKSE\\SolidGroundCamping.ini."));
		}
	}
}

void SGC::RegisterMenu()
{
	static bool registered = false;  // once only
	if (registered) {
		return;
	}
	registered = true;
	if (!SKSEMenuFramework::IsInstalled()) {
		logger::info("SKSE Menu Framework not installed: no in-game settings page (SolidGroundCamping.ini works as before)");
		return;
	}
	SKSEMenuFramework::SetSection("Camping - Solid Ground");  // (his call 2026-11-16: "Camping" first, so it is found in a long
	// list of mods; the name stays as it is in every language)
	SKSEMenuFramework::AddSectionItem(Tr("Camp"), RenderPage);
	logger::info("Settings page added to SKSE Menu Framework (Camping - Solid Ground > Camp)");
}
