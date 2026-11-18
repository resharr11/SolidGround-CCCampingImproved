#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

using namespace std::literals;

namespace fs = std::filesystem;

// CommonLibSSE (powerof3 dev) dropped the SKSE::log::info/error helpers; route them to CommonLib's own logger (REX),
// which SKSE::Init sets up (the same as Free Cooking's PCH, so the shared camp code logs the same way)
namespace SKSE::log
{
	namespace detail
	{
		inline void Write(REX::ELogLevel a_level, const std::string& a_msg)
		{
			REX::Impl::Log(std::source_location::current(), a_level, std::string_view{ a_msg });
		}
	}
	template <class... A>
	void info(std::format_string<A...> a_fmt, A&&... a_args) { detail::Write(REX::ELogLevel::Info, std::format(a_fmt, std::forward<A>(a_args)...)); }
	template <class... A>
	void warn(std::format_string<A...> a_fmt, A&&... a_args) { detail::Write(REX::ELogLevel::Warning, std::format(a_fmt, std::forward<A>(a_args)...)); }
	template <class... A>
	void error(std::format_string<A...> a_fmt, A&&... a_args) { detail::Write(REX::ELogLevel::Error, std::format(a_fmt, std::forward<A>(a_args)...)); }
	template <class... A>
	void critical(std::format_string<A...> a_fmt, A&&... a_args) { detail::Write(REX::ELogLevel::Critical, std::format(a_fmt, std::forward<A>(a_args)...)); }
}
namespace logger = SKSE::log;

#include "Version.h"
