#pragma once
#include <fstream>
#include <string>
#include "Config.h"

class Logger {

public:
	Logger (Config* config) {
		
		m_active = config->getConfig ("Log");
		if (m_active) {
			m_log.open ("blockthespot_log.txt", std::ios::out | std::ios::app);
			//m_log << "BlockTheSpot - Build date: " << __TIMESTAMP__ << std::endl;
		}
	}

	~Logger () {
		if (m_active) {
			m_log.flush ();
			m_log.close ();
		}
	}

	bool is_active () {
		return m_active;
	}

	void Log (std::string_view log) {
		if (m_active)
			m_log << log << '\n';
	}

private:
	bool m_active = false;
	std::ofstream m_log;

};
