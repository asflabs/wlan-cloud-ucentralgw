// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
//
// Created by stephane bourque on 2021-12-20.
//

#pragma once

#include "framework/MicroService.h"

namespace OpenWifi {
	class SDKCalls {
	  public:
		static bool GetProvisioningConfiguration(const std::string & SerialNumber, std::string & Config);
	  private:
	};
}


