//
//	License type: BSD 3-Clause License
//	License copy: https://github.com/Telecominfraproject/wlan-cloud-ucentralgw/blob/master/LICENSE
//
//	Created by Stephane Bourque on 2021-03-04.
//	Arilia Wireless Inc.
//

#pragma once

#include <chrono>
#include <future>
#include <map>
#include <utility>
#include <functional>

#include "Poco/JSON/Object.h"
#include "Poco/Net/HTTPServerRequest.h"
#include "Poco/Net/HTTPServerResponse.h"

#include "RESTObjects//RESTAPI_GWobjects.h"
#include "framework/MicroService.h"

namespace OpenWifi {

	struct CommandTagIndex {
		uint64_t 	Id=0;
		std::string SerialNumber;
	};

	inline bool operator <(const CommandTagIndex& lhs, const CommandTagIndex& rhs) {
		if(lhs.Id<rhs.Id)
			return true;
		if(lhs.Id>rhs.Id)
			return false;
		return lhs.SerialNumber<rhs.SerialNumber;
	}

	inline bool operator ==(const CommandTagIndex& lhs, const CommandTagIndex& rhs) {
		if(lhs.Id == rhs.Id && lhs.SerialNumber == rhs.SerialNumber)
			return true;
		return false;
	}

    class CommandManager : public SubSystemServer, Poco::Runnable {
	    public:
		  	typedef Poco::JSON::Object::Ptr objtype_t;
		  	typedef std::promise<objtype_t> promise_type_t;
			struct RpcObject {
				std::string uuid;
				std::chrono::time_point<std::chrono::high_resolution_clock> submitted = std::chrono::high_resolution_clock::now();
				std::shared_ptr<promise_type_t> rpc_entry;
			};

			int Start() override;
			void Stop() override;
			void WakeUp();
			void PostCommandResult(const std::string &SerialNumber, Poco::JSON::Object::Ptr Obj);

			std::shared_ptr<promise_type_t> PostCommandOneWayDisk(
				const std::string &SerialNumber,
				const std::string &Method,
				const Poco::JSON::Object &Params,
				const std::string &UUID,
				bool & Sent) {
					return 	PostCommand(SerialNumber,
									Method,
									Params,
									UUID,
								   	true, true, Sent );
			}

			std::shared_ptr<promise_type_t> PostCommandDisk(
				const std::string &SerialNumber,
				const std::string &Method,
				const Poco::JSON::Object &Params,
				const std::string &UUID,
				bool & Sent) {
					return 	PostCommand(SerialNumber,
								   Method,
								   Params,
								   UUID,
								   false, true, Sent  );
			}

			std::shared_ptr<promise_type_t> PostCommand(
				const std::string &SerialNumber,
				const std::string &Method,
				const Poco::JSON::Object &Params,
				const std::string &UUID,
				bool & Sent) {
					return 	PostCommand(SerialNumber,
								   Method,
								   Params,
								   UUID,
								   false,
								   false, Sent );
			}

			std::shared_ptr<promise_type_t> PostCommandOneWay(
				const std::string &SerialNumber,
				const std::string &Method,
				const Poco::JSON::Object &Params,
				const std::string &UUID,
				bool & Sent) {
					return 	PostCommand(SerialNumber,
								   Method,
								   Params,
								   UUID,
								   true,
								   false, Sent  );
			}


			void Janitor();
			void run() override;

			static auto instance() {
			    static auto instance_ = new CommandManager;
				return instance_;
			}
			inline bool Running() const { return Running_; }

	    private:
			std::atomic_bool 						Running_ = false;
			Poco::Thread    						ManagerThread;
			uint64_t 								Id_=3;	//	do not start @1. We ignore ID=1 & 0 is illegal..
			std::map<CommandTagIndex,RpcObject>		OutStandingRequests_;

			std::shared_ptr<promise_type_t> PostCommand(
				const std::string &SerialNumber,
				const std::string &Method,
				const Poco::JSON::Object &Params,
				const std::string &UUID,
				bool oneway_rpc,
				bool disk_only,
				bool & Sent);

			CommandManager() noexcept:
				SubSystemServer("CommandManager", "CMD-MGR", "command.manager")
				{
				}
	};

	inline auto CommandManager() { return CommandManager::instance(); }

}  // namespace

