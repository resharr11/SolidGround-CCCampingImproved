// Solid Ground Camping (CC Camping Improved): the camp improvements, standing on their own. The camp code lives in this
// project (src\Core and src\Camp, moved here from Free Cooking on 2026-11-10); this file sets this mod's side of it and
// connects it to SKSE. The handoff with Free Cooking's older camp code (Camp/Setup.inl) stays for its test builds.

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <numbers>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

#include "Settings.h"

namespace
{
	constexpr const char* CAMP_PLUGIN = "SolidGroundCamping.esp";
	constexpr const char* CAMP_OTHER_PLUGIN = "FreeCooking.esp";
	constexpr const char* CAMP_OTHER_DLL = "FreeCooking.dll";
	constexpr const char* CAMP_INI = "Data/SKSE/Plugins/SolidGroundCamping.ini";
	constexpr bool CAMP_WINS_TIES = false;  // on equal camp code, Free Cooking runs the camps

	#include "Core/Timers.inl"
	#include "Core/Common.inl"
	#include "Core/Translate.inl"
	#include "Camp/Basics.inl"
	#include "Camp/GroundAndTents.inl"
	#include "Camp/PlantsMoundsFollowers.inl"
	#include "Camp/Foundation.inl"
	#include "Camp/FireAndWatch.inl"
	#include "Camp/Placement.inl"
	#include "Camp/Dungeon.inl"
	#include "Camp/MenusAndHooks.inl"
	#include "Camp/Setup.inl"

	// this mod's data in the save: only the camp (the same 'CAMP' record Free Cooking writes)
	void RegisterSerialization()
	{
		auto serialization = SKSE::GetSerializationInterface();
		if (!serialization) {
			return;
		}
		serialization->SetUniqueID('SGCP');
		serialization->SetSaveCallback([](SKSE::SerializationInterface* a_intfc) { CampSave(a_intfc); });
		serialization->SetLoadCallback([](SKSE::SerializationInterface* a_intfc) {
			std::uint32_t type = 0, version = 0, length = 0;
			while (a_intfc->GetNextRecordInfo(type, version, length)) {
				CampLoadRecord(a_intfc, type, version, length);
			}
		});
		serialization->SetRevertCallback([](SKSE::SerializationInterface*) { CampRevert(); });
	}
}

#include "Camp/Exports.inl"

// ---- for the settings page (Menu.cpp) ----
std::vector<SGC::TentInfo> SGC::Tents()
{
	std::vector<TentInfo> out;
	for (int i = 0; i < TentCount(); ++i) {
		const auto& t = g_tents[i];
		out.push_back({ ::Tr(t.name.c_str()), t.kit, t.kit ? t.size : 1.0f, t.kit ? t.door : 0.0f });
	}
	return out;
}
std::string SGC::TentKey(int a_tent, const char* a_what) { return TentSettingKey(a_tent, a_what); }
int         SGC::KitFireSpotCount() { return g_kitFireSpotCount; }
float       SGC::ShippedSetting(const char* a_key, float a_default) { return CampSetting(CampShippedSettings(), a_key, a_default); }
const char* SGC::Tr(const char* a_en) { return ::Tr(a_en); }

std::string SGC::UserIniPathText()
{
	const auto path = CampUserIniPath();
	return path.empty() ? std::string{} : path.string();
}

std::map<std::string, float> SGC::UserSettings()
{
	std::map<std::string, float> out;
	if (const auto path = CampUserIniPath(); !path.empty()) {
		std::unordered_map<std::string, float> read;
		ReadCampSettingsFile(path, read);
		out.insert(read.begin(), read.end());
	}
	return out;
}

// the player's own file, written whole (to a temporary file first, then moved over the old one, so a crash mid-write
// never leaves half a file). The camp code reads it again within a second
bool SGC::SaveUserSettings(const std::map<std::string, float>& a_values)
{
	const auto path = CampUserIniPath();
	if (path.empty()) {
		logger::warn("Settings page: no Documents\\My Games\\...\\SKSE folder found; the settings can't be saved");
		return false;
	}
	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);
	auto temp = path;
	temp += L".tmp";
	{
		std::ofstream out(temp, std::ios::trunc);
		if (!out) {
			logger::error("Settings page: couldn't write {}", temp.string());
			return false;
		}
		out << "; Solid Ground - CC Camping Improved: your own settings, written by the in-game settings page\n"
			   "; (Mod Control Panel > Solid Ground). They win over the mod's SolidGroundCamping.ini and are\n"
			   "; kept when the mod is updated. Delete this file to go back to the mod's settings.\n\n";
		for (const auto& [key, value] : a_values) {
			out << key << " = " << std::format("{:g}", value) << "\n";
		}
		if (!out.flush()) {
			logger::error("Settings page: couldn't write {}", temp.string());
			return false;
		}
	}
	std::filesystem::rename(temp, path, ec);
	if (ec) {
		logger::error("Settings page: couldn't replace {} ({})", path.string(), ec.message());
		std::filesystem::remove(temp, ec);
		return false;
	}
	return true;
}

SKSE_EXPORT constinit auto SKSEPlugin_Version = []() {
	SKSE::PluginVersionData v;
	v.PluginVersion({ Version::MAJOR, Version::MINOR, Version::PATCH, 0 });
	v.PluginName("SolidGroundCamping");
	v.AuthorName("resharr");
	v.UsesAddressLibrary();
	v.UsesUpdatedStructs();
	return v;
}();

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* a_skse)
{
	// Also creates My Games/Skyrim Special Edition/SKSE/SolidGroundCamping.log
	SKSE::Init(a_skse);
	logger::info("Solid Ground Camping (CC Camping Improved) v{}", Version::NAME);
	RegisterSerialization();
	SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message* a_msg) {
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			LoadTranslation();  // before anything that shows or sets text (Core/Translate.inl)
			LoadKits();  // the kit files: a reshaped mound, spots round the fire, tents (Camp/PlantsMoundsFollowers.inl)
			ApplyCampModelReplacers();  // a modder's models at our paths for the pot stand, fur rug and tent bedroll (Camp/Basics.inl)
			SGC::RegisterMenu();  // the settings page, when SKSE Menu Framework is installed (Menu.cpp)
			PlayerUpdateHook::Install();  // the timers the camp code runs on
			CampSetup();
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
			g_loadingSave = true;
			CampLoadReset();  // the previous game's camp jobs dropped, what they changed put back (Camp/Setup.inl)
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
			g_loadingSave = false;
			// (SKSE says whether the load worked: a failed one leaves nothing to put the camp state on)
			if (reinterpret_cast<std::uintptr_t>(a_msg->data) != 0) {
				CampGameLoaded();
			} else {
				logger::warn("Camp: the save failed to load; the camp state is left alone");
			}
			break;
		case SKSE::MessagingInterface::kNewGame:
			g_loadingSave = false;
			CampLoadReset();
			CampNewGame();
			break;
		default:
			break;
		}
	});
	return true;
}
