//
// Created by stephane bourque on 2021-06-17.
//
#include <thread>
#include <fstream>
#include <vector>

#include "OUIServer.h"

#include "Poco/String.h"
#include "Poco/StringTokenizer.h"
#include "Poco/URIStreamOpener.h"
#include "Poco/StreamCopier.h"
#include "Poco/URI.h"
#include "Poco/File.h"

#include "OUIServer.h"
#include "framework/MicroService.h"

namespace OpenWifi {

	int OUIServer::Start() {
		UpdaterThread_.start(*this);
		return 0;
	}

	void OUIServer::Stop() {
		Running_=false;
		UpdaterThread_.wakeUp();
		UpdaterThread_.join();
	}

	void OUIServer::reinitialize(Poco::Util::Application &self) {
		MicroService::instance().LoadConfigurationFile();
		Logger().information("Reinitializing.");
		Stop();
		Start();
	}

	bool OUIServer::GetFile(const std::string &FileName) {
		try {
			std::unique_ptr<std::istream> pStr(
				Poco::URIStreamOpener::defaultOpener().open(MicroService::instance().ConfigGetString("oui.download.uri")));
			std::ofstream OS;
			Poco::File	F(FileName);
			if(F.exists())
				F.remove();
			OS.open(FileName);
			Poco::StreamCopier::copyStream(*pStr, OS);
			OS.close();
			return true;
		} catch (const Poco::Exception &E) {
			Logger().log(E);
		}
		return false;
	}

	bool OUIServer::ProcessFile( const std::string &FileName, OUIMap &Map) {
		try {
			std::ifstream Input;
			Input.open(FileName, std::ios::binary);

			while (!Input.eof()) {
				if(!Running_)
					return false;
				char buf[256];
				Input.getline(buf, sizeof(buf));
				std::string Line{buf};
				auto Tokens = Poco::StringTokenizer(Line, " \t",
													Poco::StringTokenizer::TOK_TRIM |
														Poco::StringTokenizer::TOK_IGNORE_EMPTY);

				if (Tokens.count() > 2) {
					if (Tokens[1] == "(hex)") {
						auto MAC = Utils::SerialNumberToOUI(Tokens[0]);
						if (MAC > 0) {
							std::string Manufacturer;
							for (auto i = 2; i < Tokens.count(); i++)
								Manufacturer += Tokens[i] + " ";
							auto M = Poco::trim(Manufacturer);
							if (!M.empty())
								Map[MAC] = M;
						}
					}
				}
			}
			return true;
		} catch ( const Poco::Exception &E) {
			Logger().log(E);
		}
		return false;
	}

	void OUIServer::run() {

		Running_ = true;
		while(Running_) {
			Poco::Thread::trySleep(24 * 60 * 60 * 1000);

			if(!Running_)
				break;

			UpdateImpl();

		}
	}

	void OUIServer::UpdateImpl() {
		if(Updating_)
			return;
		Updating_ = true;

		//	fetch data from server, if not available, just use the file we already have.
		std::string LatestOUIFileName{ MicroService::instance().DataDir() + "/newOUIFile.txt"};
		std::string CurrentOUIFileName{ MicroService::instance().DataDir() + "/current_oui.txt"};

		OUIMap TmpOUIs;
		if(GetFile(LatestOUIFileName) && ProcessFile(LatestOUIFileName, TmpOUIs)) {
			std::lock_guard G(Mutex_);
			OUIs_ = std::move(TmpOUIs);
			LastUpdate_ = time(nullptr);
			Poco::File F1(CurrentOUIFileName);
			if(F1.exists())
				F1.remove();
			Poco::File F2(LatestOUIFileName);
			F2.renameTo(CurrentOUIFileName);
			Logger().information(Poco::format("New OUI file %s downloaded.",LatestOUIFileName));
		} else if(OUIs_.empty()) {
			if(ProcessFile(CurrentOUIFileName, TmpOUIs)) {
				LastUpdate_ = time(nullptr);
				std::lock_guard G(Mutex_);
				OUIs_ = std::move(TmpOUIs);
			}
		}
		Updating_ = false;
	}

	std::string OUIServer::GetManufacturer(const std::string &MAC) {
		std::lock_guard Guard(Mutex_);
		auto Manufacturer = OUIs_.find(Utils::SerialNumberToOUI(MAC));
		if(Manufacturer != OUIs_.end())
			return Manufacturer->second;
		return "";
	}
};
