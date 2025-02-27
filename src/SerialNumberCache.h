//
// Created by stephane bourque on 2021-08-11.
//

#pragma once

#include "framework/MicroService.h"

namespace OpenWifi {
	class SerialNumberCache : public SubSystemServer {
		public:

		static auto instance() {
		    static auto instance_ = new SerialNumberCache;
			return instance_;
		}

		int Start() override;
		void Stop() override;
		void AddSerialNumber(const std::string &S);
		void DeleteSerialNumber(const std::string &S);
		void FindNumbers(const std::string &S, uint HowMany, std::vector<uint64_t> &A);
		bool NumberExists(const std::string &S);

	  private:
		uint64_t 					LastUpdate_ = 0 ;
		std::vector<uint64_t>		SNs_;
		std::mutex					M_;

		SerialNumberCache() noexcept:
			SubSystemServer("SerialNumberCache", "SNCACHE-SVR", "serialcache")
			{
				SNs_.reserve(2000);
			}
	};

	inline auto SerialNumberCache() { return SerialNumberCache::instance(); }

} // namespace OpenWiFi
