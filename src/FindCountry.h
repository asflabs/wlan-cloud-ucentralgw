//
// Created by stephane bourque on 2021-11-20.
//

#pragma once

#include "framework/MicroService.h"
#include "Poco/Net/IPAddress.h"
#include "nlohmann/json.hpp"

namespace OpenWifi {

	class FindCountryFromIP : public SubSystemServer {
	  public:
		static auto *instance() {
			static auto instance_ = new FindCountryFromIP;
			return instance_;
		}

		inline int Start() final {
			Token_ = MicroService::instance().ConfigGetString("iptocountry.ipinfo.token","");
			Default_ = MicroService::instance().ConfigGetString("iptocountry.ipinfo.default","US");
			return 0;
		}

		inline void Stop() final {
		}

		[[nodiscard]] inline std::string ReformatAddress(const std::string & I )
		{
			if(I.substr(0,7) == "::ffff:")
			{
				std::string ip = I.substr(7 );
				return ip;
			}
			return I;
		}

		inline std::string Get(const Poco::Net::IPAddress & IP) {
			if(Token_.empty())
				return Default_;

			try {
				std::string URL = "https://ipinfo.io/" + ReformatAddress(IP.toString()) + "?token=" + Token_;
				std::string Response;
				if (Utils::wgets(URL, Response)) {
					nlohmann::json 		IPInfo = nlohmann::json::parse(Response);
					if(IPInfo.contains("country") && IPInfo["country"].is_string()) {
						return IPInfo["country"];
					}
				} else {
					return Default_;
				}
			} catch(...) {
			}
			return Default_;
		}

	  private:
		std::string Token_;
		std::string Default_;

		FindCountryFromIP() noexcept:
			SubSystemServer("IpToCountry", "IPTOC-SVR", "iptocountry")
		{
		}
	};

	inline FindCountryFromIP * FindCountryFromIP() { return FindCountryFromIP::instance(); }

}
