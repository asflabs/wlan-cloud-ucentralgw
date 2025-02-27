//
//	License type: BSD 3-Clause License
//	License copy: https://github.com/Telecominfraproject/wlan-cloud-ucentralgw/blob/master/LICENSE
//
//	Created by Stephane Bourque on 2021-03-04.
//	Arilia Wireless Inc.
//

#pragma once

#include "Poco/Net/HTTPRequestHandler.h"
#include "Poco/Net/HTTPRequestHandlerFactory.h"
#include "Poco/Net/HTTPServer.h"
#include "Poco/Net/HTTPServerRequest.h"

#include "framework/MicroService.h"

namespace OpenWifi {

    class FileUploader : public SubSystemServer {
    public:
		int Start() override;
		void Stop() override;
		void reinitialize(Poco::Util::Application &self) override;
		const std::string & FullName();
		bool AddUUID( const std::string & UUID);
		bool ValidRequest(const std::string & UUID);
		void RemoveRequest(const std::string &UUID);
		const std::string & Path() { return Path_; };

        static auto instance() {
            static auto instance_ = new FileUploader;
			return instance_;
        }

		[[nodiscard]] inline uint64_t MaxSize() const { return MaxSize_; }

    private:
        std::vector<std::unique_ptr<Poco::Net::HTTPServer>>   Servers_;
		Poco::ThreadPool				Pool_;
        std::string                     FullName_;
        std::map<std::string,uint64_t>  OutStandingUploads_;
        std::string                     Path_;
		uint64_t 						MaxSize_=10000000;

		explicit FileUploader() noexcept:
			SubSystemServer("FileUploader", "FILE-UPLOAD", "openwifi.fileuploader"),
		   	Pool_("FileUpLoaderPool")
		{
		}
    };

    class FileUpLoaderRequestHandlerFactory : public Poco::Net::HTTPRequestHandlerFactory {
    public:
        explicit FileUpLoaderRequestHandlerFactory(Poco::Logger &L) :
                Logger_(L){}

        Poco::Net::HTTPRequestHandler *createRequestHandler(const Poco::Net::HTTPServerRequest &request) override;
		inline Poco::Logger & Logger() { return Logger_; }
    private:
        Poco::Logger    & Logger_;
    };

	inline auto FileUploader() { return FileUploader::instance(); }
} //   namespace

