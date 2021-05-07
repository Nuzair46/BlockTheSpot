#pragma once
#include <array>
#include <map>
#include <string>

class Config {
	
public:
	Config () {
		m_filename = "./config.ini";
		for (auto &conf : config_data) {
			m_data[conf] = read ("Config", conf);
		}

	}

	bool getConfig (std::string_view key) {
		auto find = m_data.find (key);
		if (find != m_data.end()) {
			return find->second;
		}
		return false;
	}
private:
	std::string_view m_filename;
	std::map<std::string_view, bool> m_data;
	static constexpr std::array<std::string_view, 3> config_data = { "Log", "Skip_wpad", "Block_BannerOnly" };
	bool read (std::string_view app, std::string_view key, int def_value = 0) {
		if (1 == GetPrivateProfileInt (app.data(), key.data(), def_value, m_filename.data())) {
			return true;
		}
		return false;
	}
};

