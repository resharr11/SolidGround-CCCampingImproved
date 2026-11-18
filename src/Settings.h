#pragma once
// Solid Ground's settings page (Menu.cpp) and the camp code (main.cpp) talk through these. The page is a file of its
// own because SKSEMenuFramework.h brings in windows.h.
#include <map>
#include <string>
#include <vector>

namespace SGC
{
	struct TentInfo
	{
		std::string name;     // as the shelter menu shows it (translated)
		bool        kit = false;
		float       size = 1.0f;  // its size without a setting (ours 1, a kit tent its Size)
		float       door = 0.0f;  // its opening without a setting (ours 0, a kit tent its Door), degrees
	};
	std::vector<TentInfo> Tents();                          // ours, then the kit tents
	std::string           TentKey(int a_tent, const char* a_what);  // "size" or "door"
	int                   KitFireSpotCount();               // -1: no kit sets it

	float                        ShippedSetting(const char* a_key, float a_default);  // the shipped SolidGroundCamping.ini only
	std::map<std::string, float> UserSettings();            // the player's own file
	bool                         SaveUserSettings(const std::map<std::string, float>& a_values);
	std::string                  UserIniPathText();

	const char* Tr(const char* a_en);
	void        RegisterMenu();  // Menu.cpp, at kDataLoaded (after the kits and the translation are read)
}
