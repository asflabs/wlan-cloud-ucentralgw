// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
//
// Created by stephane bourque on 2021-07-21.
//

#include "RESTAPI_deviceDashboardHandler.h"
#include "Daemon.h"
#include "Dashboard.h"

namespace OpenWifi {
	void RESTAPI_deviceDashboardHandler::DoGet() {
		Daemon()->GetDashboard().Create();
		Poco::JSON::Object	Answer;
		Daemon()->GetDashboard().Report().to_json(Answer);
		ReturnObject(Answer);
	}
}
