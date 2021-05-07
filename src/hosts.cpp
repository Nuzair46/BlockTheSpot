#include <array>
#include <future>
#include "hosts.h"
#include "Config.h"
#include "Logger.h"

extern Logger *g_Logger;
extern Config g_Config;

_getaddrinfo getaddrinfo_orig;

static constexpr std::array<std::string_view, 5> blockList = { "googletag", "doubleclick", "qualaroo.com",/* "fbsbx.com", */ "dns.google", "cloudflare" };
static const bool wpad = g_Config.getConfig ("Skip_wpad");

// check if ads hostname
bool is_blockhost (std::string_view nodename) {
	if (0 == nodename.compare ("wpad"))
		return wpad ? true : false;
	for (auto &hostname : blockList) {
		if (std::string_view::npos != nodename.find (hostname))
			return true;
	}
	return false;
}

int WSAAPI getaddrinfo_hook (
	_In_opt_	PCSTR nodename,
	_In_opt_	PCSTR servname,
	_In_opt_	const ADDRINFOA* hints,
	_Out_		PADDRINFOA* res)
{

	if (nodename == nullptr)
		return getaddrinfo_orig (nodename, servname, hints, res);

	std::string nnodename (nodename);
	
	auto isblock = std::async (std::launch::async, is_blockhost, nnodename);
	auto result = getaddrinfo_orig (nodename, servname, hints, res);
	if (0 == result) {
		if (isblock.get ()) {
			for (auto ptr = *res; nullptr != ptr; ptr = ptr->ai_next) {
				auto ipv4 = reinterpret_cast<sockaddr_in*>(ptr->ai_addr);
				ipv4->sin_addr.S_un.S_addr = INADDR_ANY;
			}
			g_Logger->Log ("blocked - " + nnodename);
		}
		else {
			g_Logger->Log ("allowed - " + nnodename);
		}
	}

	return result;
}

