// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
//
//	License type: BSD 3-Clause License
//	License copy: https://github.com/Telecominfraproject/wlan-cloud-ucentralgw/blob/master/LICENSE
//
//	Created by Stephane Bourque on 2021-03-04.
//	Arilia Wireless Inc.
//

#pragma once

#include <array>
#include <iostream>
#include <cstdlib>
#include <vector>
#include <set>

#include "Poco/Util/Application.h"
#include "Poco/Util/ServerApplication.h"
#include "Poco/Util/Option.h"
#include "Poco/Util/OptionSet.h"
#include "Poco/UUIDGenerator.h"
#include "Poco/ErrorHandler.h"
#include "Poco/Crypto/RSAKey.h"
#include "Poco/Crypto/CipherFactory.h"
#include "Poco/Crypto/Cipher.h"

#include "Dashboard.h"
#include "framework/MicroService.h"
#include "framework/OpenWifiTypes.h"

namespace OpenWifi {

	static const char * vDAEMON_PROPERTIES_FILENAME = "owgw.properties";
	static const char * vDAEMON_ROOT_ENV_VAR = "OWGW_ROOT";
	static const char * vDAEMON_CONFIG_ENV_VAR = "OWGW_CONFIG";
	static const char * vDAEMON_APP_NAME = uSERVICE_GATEWAY.c_str();
	static const uint64_t vDAEMON_BUS_TIMER = 10000;

    class Daemon : public MicroService {
		public:
			explicit Daemon(const std::string & PropFile,
							const std::string & RootEnv,
							const std::string & ConfigEnv,
							const std::string & AppName,
						  	uint64_t 	BusTimer,
							const SubSystemVec & SubSystems) :
				MicroService( PropFile, RootEnv, ConfigEnv, AppName, BusTimer, SubSystems) {};

			bool AutoProvisioning() const { return AutoProvisioning_ ; }
			[[nodiscard]] std::string IdentifyDevice(const std::string & Compatible) const;
			void initialize();
			static Daemon *instance();
			inline DeviceDashboard	& GetDashboard() { return DB_; }
			Poco::Logger & Log() { return Poco::Logger::get(AppName()); }
	  	private:
			bool                        AutoProvisioning_ = false;
			std::vector<std::pair<std::string,std::string>> DeviceTypes_;
			DeviceDashboard				DB_;

    };

	inline Daemon * Daemon() { return Daemon::instance(); }
}


