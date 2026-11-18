cmake_minimum_required(VERSION 3.21)

# Solid Ground Camping (CC Camping Improved): stands on its own since 2026-11-10. The camp code (src\Core, src\Camp),
# the CommonLibSSE copy (CommonLibSSE.zip, unpacked into extern\ by build.bat) and the vcpkg port all live in this
# project; Free Cooking keeps only its own small Camping support.
set(NAME "SolidGroundCamping" CACHE STRING "")
set(VERSION 1.1.0)

project(${NAME} VERSION ${VERSION} LANGUAGES CXX)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Version.h.in ${CMAKE_CURRENT_BINARY_DIR}/include/Version.h @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.rc.in ${CMAKE_CURRENT_BINARY_DIR}/version.rc @ONLY)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
	message(FATAL_ERROR "In-source builds not allowed.")
endif()

if (MSVC)
	add_compile_options(/MP)
endif ()

# ---- keep the build folder's path (it holds the Windows user name) out of the DLL ----
# Source file names compiled into the code (std::source_location, __FILE__) are cut down to start below
# TRIM_ROOT: the folder above "Skyrim Mods" (no spaces in it, so the option needs no quotes). /d1trimfile is an
# undocumented MSVC option; Microsoft's own WindowsAppSDK build uses it the same way, noting it "Requires /Brepro"
# (WindowsAppSDK.Build.Cpp.props). make_release.ps1 scans the DLL and refuses to build the zip if a path is left.
# Set here, before CommonLibSSE is added, so CommonLibSSE's own sources get it too; the vcpkg-built library gets
# it from cmake/triplets/x64-windows-static-clean.cmake.
get_filename_component(TRIM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
file(TO_NATIVE_PATH "${TRIM_ROOT}" TRIM_ROOT)
if (TRIM_ROOT MATCHES " ")
	message(FATAL_ERROR "The folder above 'Skyrim Mods' has a space in its path: ${TRIM_ROOT}")
endif ()
if (MSVC)
	add_compile_options(/Brepro "/d1trimfile:${TRIM_ROOT}")
endif ()

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)

# ---- CommonLibSSE (powerof3, dev) : Anniversary Edition runtime ----
set(SKYRIM_SUPPORT_AE ON CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/extern/CommonLibSSE CommonLibSSE)

# the camp code: pieces of main.cpp (it includes them); listed only so they show in the project
set(shared
	src/Core/Timers.inl
	src/Core/Common.inl
	src/Camp/Basics.inl
	src/Camp/GroundAndTents.inl
	src/Camp/PlantsMoundsFollowers.inl
	src/Camp/Foundation.inl
	src/Camp/FireAndWatch.inl
	src/Camp/Placement.inl
	src/Camp/Dungeon.inl
	src/Camp/MenusAndHooks.inl
	src/Camp/Setup.inl
	src/Camp/Exports.inl
)
set_source_files_properties(${shared} PROPERTIES HEADER_FILE_ONLY TRUE)

# the settings page (2026-11-16): its own file, because SKSEMenuFramework.h brings in windows.h
add_library(${PROJECT_NAME} SHARED src/PCH.h src/PCH.cpp src/main.cpp src/Menu.cpp src/Settings.h src/SKSEMenuFramework.h ${shared}
	${CMAKE_CURRENT_BINARY_DIR}/include/Version.h
	${CMAKE_CURRENT_BINARY_DIR}/version.rc)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
# _SILENCE_...: SKSEMenuFramework.h uses std::wstring_convert (deprecated, an error under /sdl), as in Free Cooking
target_compile_definitions(${PROJECT_NAME} PRIVATE _UNICODE _CRT_SECURE_NO_WARNINGS
	_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS)
target_include_directories(${PROJECT_NAME} PRIVATE
	${CMAKE_CURRENT_BINARY_DIR}/include
	${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(${PROJECT_NAME} PRIVATE CommonLibSSE::CommonLibSSE)
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)

if (MSVC)
	target_compile_options(${PROJECT_NAME} PRIVATE
		/sdl /utf-8 /Zi /permissive- /Zc:preprocessor /wd4200
		"$<$<CONFIG:RELEASE>:/Zc:inline;/JMC-;/Ob3>")
	target_link_options(${PROJECT_NAME} PRIVATE
		"$<$<CONFIG:DEBUG>:/INCREMENTAL;/OPT:NOREF;/OPT:NOICF>"
		"$<$<CONFIG:RELEASE>:/INCREMENTAL:NO;/OPT:REF;/OPT:ICF;/DEBUG:FULL>"
		# the DLL names its debug symbols file by name only, not by its full path on this PC
		/Brepro "/PDBALTPATH:${PROJECT_NAME}.pdb")
endif ()
