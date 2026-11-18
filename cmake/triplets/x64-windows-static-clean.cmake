# x64-windows-static, plus the options that keep this PC's folder paths out of the libraries vcpkg builds
# (commonlib-shared). The same options as CMakeLists.txt: /Brepro and /d1trimfile cut source file names down
# to start below the folder above "Skyrim Mods" (it holds the Windows user name). The folder is worked out
# here, so no path is written into this file. Changing this file makes vcpkg rebuild the libraries.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)

# this file: <root>\Skyrim Mods\SolidGroundCampingProject\cmake\triplets
get_filename_component(_trim_root "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
file(TO_NATIVE_PATH "${_trim_root}" _trim_root)
if (_trim_root STREQUAL "" OR _trim_root MATCHES " ")
	message(FATAL_ERROR "x64-windows-static-clean: bad folder for /d1trimfile: '${_trim_root}'")
endif ()
set(VCPKG_C_FLAGS "/Brepro /d1trimfile:${_trim_root}")
set(VCPKG_CXX_FLAGS "/Brepro /d1trimfile:${_trim_root}")
