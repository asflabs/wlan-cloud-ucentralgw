// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
//
// Created by stephane bourque on 2021-10-14.
//

#pragma once

#include "framework/MicroService.h"

namespace OpenWifi {
	class RESTAPI_blacklist_list : public RESTAPIHandler {
	  public:
		RESTAPI_blacklist_list(const RESTAPIHandler::BindingMap &bindings, Poco::Logger &L, RESTAPI_GenericServer & Server, uint64_t TransactionId, bool Internal)
		: RESTAPIHandler(bindings, L,
						 std::vector<std::string>{Poco::Net::HTTPRequest::HTTP_GET,
												  Poco::Net::HTTPRequest::HTTP_OPTIONS},
												  Server,
							 TransactionId,
												  Internal) {}
												  static const std::list<const char *> PathName() { return std::list<const char *>{"/api/v1/blacklist"};}
		void DoGet() final;
		void DoDelete() final {};
		void DoPost() final {};
		void DoPut() final {};
	};
}

