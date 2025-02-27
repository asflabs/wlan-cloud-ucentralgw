//
//	License type: BSD 3-Clause License
//	License copy: https://github.com/Telecominfraproject/wlan-cloud-ucentralgw/blob/master/LICENSE
//
//	Created by Stephane Bourque on 2021-03-04.
//	Arilia Wireless Inc.
//

#include <cerrno>

#include "Poco/Net/IPAddress.h"
#include "Poco/Net/SSLException.h"
#include "Poco/Net/HTTPServerSession.h"
#include "Poco/Net/HTTPHeaderStream.h"
#include "Poco/Net/HTTPServerRequestImpl.h"
#include "Poco/JSON/Array.h"
#include "Poco/zlib.h"

#include "CommandManager.h"
#include "ConfigurationCache.h"
#include "StorageService.h"
#include "TelemetryStream.h"
#include "WebSocketServer.h"
#include "framework/KafkaTopics.h"
#include "framework/uCentral_Protocol.h"
#include "framework/MicroService.h"
#include "Daemon.h"
#include "FindCountry.h"
#include "SerialNumberCache.h"

namespace OpenWifi {

	bool WebSocketServer::ValidateCertificate(const std::string & ConnectionId, const Poco::Crypto::X509Certificate & Certificate) {
		if(IsCertOk()) {
			Logger().debug(Poco::format("CERTIFICATE(%s): issuer='%s' cn='%s'", ConnectionId, Certificate.issuerName(),Certificate.commonName()));
			if(!Certificate.issuedBy(*IssuerCert_)) {
				Logger().debug(Poco::format("CERTIFICATE(%s): issuer mismatch. Local='%s' Incoming='%s'", ConnectionId, IssuerCert_->issuerName(), Certificate.issuerName()));
				return false;
			}
			return true;
		}
		return false;
	}

	int WebSocketServer::Start() {
		ReactorPool_.Start();
        for(const auto & Svr : ConfigServersList_ ) {
            Logger().notice(Poco::format("Starting: %s:%s Keyfile:%s CertFile: %s", Svr.Address(), std::to_string(Svr.Port()),
											 Svr.KeyFile(),Svr.CertFile()));

			Svr.LogCert(Logger());
			if(!Svr.RootCA().empty())
				Svr.LogCas(Logger());

            auto Sock{Svr.CreateSecureSocket(Logger())};

			if(!IsCertOk()) {
				IssuerCert_ = std::make_unique<Poco::Crypto::X509Certificate>(Svr.IssuerCertFile());
				Logger().information(Poco::format("Certificate Issuer Name:%s",IssuerCert_->issuerName()));
			}
			auto NewSocketAcceptor = std::make_unique<Poco::Net::ParallelSocketAcceptor<WSConnection, Poco::Net::SocketReactor>>(Sock, Reactor_);
            Acceptors_.push_back(std::move(NewSocketAcceptor));
        }

		auto ProvString = MicroService::instance().ConfigGetString("autoprovisioning.process","default");
		auto Tokens = Poco::StringTokenizer(ProvString,",");

		for(const auto &i:Tokens) {
			if (i == "prov")
				LookAtProvisioning_ = true;
			else if (i == "default")
				UseDefaultConfig_ = true;
		}

        SimulatorId_ = MicroService::instance().ConfigGetString("simulatorid","");
        SimulatorEnabled_ = !SimulatorId_.empty();

		ReactorThread_.start(Reactor_);

        return 0;
    }

    void WebSocketServer::Stop() {
        Logger().notice("Stopping reactors...");
		ReactorPool_.Stop();
		Reactor_.stop();
		ReactorThread_.join();
    }

	void WSConnection::LogException(const Poco::Exception &E) {
		Logger().information(Poco::format("EXCEPTION(%s): %s",CId_,E.displayText()));
	}

	void WSConnection::CompleteStartup() {
		std::lock_guard Guard(Mutex_);
		try {

			auto SS = dynamic_cast<Poco::Net::SecureStreamSocketImpl *>(Socket_.impl());

			SS->completeHandshake();
			PeerAddress_ = SS->peerAddress().host();
			CId_ = Utils::FormatIPv6(SS->peerAddress().toString());
			if (!SS->secure()) {
				Logger().error(Poco::format("%s: Connection is NOT secure.", CId_));
			} else {
				Logger().debug(Poco::format("%s: Connection is secure.", CId_));
			}

			if (SS->havePeerCertificate()) {
				// Get the cert info...
				CertValidation_ = GWObjects::VALID_CERTIFICATE;
				try {
					Poco::Crypto::X509Certificate PeerCert(SS->peerCertificate());

					if (WebSocketServer()->ValidateCertificate(CId_, PeerCert)) {
						CN_ = Poco::trim(Poco::toLower(PeerCert.commonName()));
						CertValidation_ = GWObjects::MISMATCH_SERIAL;
						Logger().debug(Poco::format("%s: Valid certificate: CN=%s", CId_, CN_));
					} else {
						Logger().debug(Poco::format("%s: Certificate is not valid", CId_));
					}
				} catch (const Poco::Exception &E) {
					LogException(E);
				}
			} else {
				Logger().error(Poco::format("%s: No certificates available..", CId_));
			}

			if(WebSocketServer::IsSim(CN_) && !WebSocketServer()->IsSimEnabled()) {
				Logger().debug(Poco::format("CONNECTION(%s): Sim Device %s is not allowed. Disconnecting.", CId_, CN_));
				delete this;
				return;
			}

			SerialNumber_ = CN_;
			if(!CN_.empty() && StorageService()->IsBlackListed(SerialNumber_)) {
				Logger().debug(Poco::format("CONNECTION(%s): Device %s is black listed. Disconnecting.", CId_, CN_));
				delete this;
				return;
			}
			auto Params = Poco::AutoPtr<Poco::Net::HTTPServerParams>(new Poco::Net::HTTPServerParams);
			Poco::Net::HTTPServerSession Session(Socket_, Params);
			Poco::Net::HTTPServerResponseImpl Response(Session);
			Poco::Net::HTTPServerRequestImpl Request(Response, Session, Params);

			auto Now = time(nullptr);
			Response.setDate(Now);
			Response.setVersion(Request.getVersion());
			Response.setKeepAlive(Params->getKeepAlive() && Request.getKeepAlive() && Session.canKeepAlive());
			WS_ = std::make_unique<Poco::Net::WebSocket>(Request, Response);
			WS_->setMaxPayloadSize(BufSize);
			auto TS = Poco::Timespan(240,0);

			WS_->setReceiveTimeout(TS);
			WS_->setNoDelay(true);
			WS_->setKeepAlive(true);
			Reactor_.addEventHandler(*WS_,
									 Poco::NObserver<WSConnection, Poco::Net::ReadableNotification>(
										 *this, &WSConnection::OnSocketReadable));
			Reactor_.addEventHandler(*WS_,
									 Poco::NObserver<WSConnection, Poco::Net::ShutdownNotification>(
										 *this, &WSConnection::OnSocketShutdown));
			Reactor_.addEventHandler(*WS_,
									 Poco::NObserver<WSConnection, Poco::Net::ErrorNotification>(
										 *this, &WSConnection::OnSocketError));
			Registered_ = true;
			Logger().information(Poco::format("CONNECTION(%s): completed.",CId_));
			return;
		} catch (const Poco::Exception &E ) {
			Logger().error("Exception caught during device connection. Device will have to retry.");
			Logger().log(E);
		} catch (...) {
			Logger().error("Exception caught during device connection. Device will have to retry. Unsecure connect denied.");
		}
		delete this;
	}

	WSConnection::WSConnection(Poco::Net::StreamSocket & socket, Poco::Net::SocketReactor & reactor):
            Socket_(socket),
            Reactor_(WebSocketServer()->GetNextReactor()),
			Logger_(WebSocketServer()->Logger())
    {
		std::thread		T([this](){ this->CompleteStartup();});
		T.detach();
    }

	static void NotifyKafkaDisconnect(std::string SerialNumber) {
		try {
			Poco::JSON::Object Disconnect;
			Poco::JSON::Object Details;
			Details.set(uCentralProtocol::SERIALNUMBER, SerialNumber);
			Details.set(uCentralProtocol::TIMESTAMP, std::time(nullptr));
			Disconnect.set(uCentralProtocol::DISCONNECTION, Details);
			Poco::JSON::Stringifier Stringify;
			std::ostringstream OS;
			Stringify.condense(Disconnect, OS);
			KafkaManager()->PostMessage(KafkaTopics::CONNECTION, SerialNumber, OS.str());
		} catch (...) {

		}
	}

    WSConnection::~WSConnection() {
		if(ConnectionId_)
        	DeviceRegistry()->UnRegister(SerialNumber_, ConnectionId_);
        if(Registered_ && WS_)
        {
        	Reactor_.removeEventHandler(*WS_,
										Poco::NObserver<WSConnection,
										Poco::Net::ReadableNotification>(*this,&WSConnection::OnSocketReadable));
        	Reactor_.removeEventHandler(*WS_,
										Poco::NObserver<WSConnection,
										Poco::Net::ShutdownNotification>(*this,&WSConnection::OnSocketShutdown));
        	Reactor_.removeEventHandler(*WS_,
										Poco::NObserver<WSConnection,
										Poco::Net::ErrorNotification>(*this,&WSConnection::OnSocketError));
        	(*WS_).close();
			Socket_.shutdown();
        } else if(WS_) {
        	(*WS_).close();
        	Socket_.shutdown();
		}

        if(KafkaManager()->Enabled() && !SerialNumber_.empty()) {
			std::string s(SerialNumber_);
        	std::thread	t([s](){ NotifyKafkaDisconnect(s);});
			t.detach();
        }
    }

    bool WSConnection::LookForUpgrade(uint64_t UUID) {

		//	A UUID of zero means ignore updates for that connection.
		if(UUID==0)
			return false;

		uint64_t GoodConfig=ConfigurationCache().CurrentConfig(SerialNumber_);
		if(GoodConfig && (GoodConfig==UUID || GoodConfig==Conn_->Conn_.PendingUUID))
			return false;

		GWObjects::Device	D;
		if(StorageService()->GetDevice(SerialNumber_,D)) {

			//	This is the case where the cache is empty after a restart. So GoodConfig will 0. If the device already
			//	has the right UUID, we just return.
			if(D.UUID == UUID ) {
				ConfigurationCache().Add(SerialNumber_,UUID);
				return false;
			}

			Conn_->Conn_.PendingUUID = D.UUID;
			GWObjects::CommandDetails  Cmd;
			Cmd.SerialNumber = SerialNumber_;
			Cmd.UUID = MicroService::CreateUUID();
			Cmd.SubmittedBy = uCentralProtocol::SUBMITTED_BY_SYSTEM;
			Cmd.Status = uCentralProtocol::PENDING;
			Cmd.Command = uCentralProtocol::CONFIGURE;
			Poco::JSON::Parser	P;
			auto ParsedConfig = P.parse(D.Configuration).extract<Poco::JSON::Object::Ptr>();
			Poco::JSON::Object Params;
			Params.set(uCentralProtocol::SERIAL, SerialNumber_);
			Params.set(uCentralProtocol::UUID, D.UUID);
			Params.set(uCentralProtocol::WHEN, 0);
			Params.set(uCentralProtocol::CONFIG, ParsedConfig);

			std::ostringstream O;
			Poco::JSON::Stringifier::stringify(Params, O);
			Cmd.Details = O.str();

			std::string Log = Poco::format("CFG-UPGRADE(%s): Current ID: %Lu, newer configuration %Lu.", CId_, UUID, D.UUID);
			Logger().debug(Log);
			bool Sent;
			CommandManager()->PostCommand(SerialNumber_ , Cmd.Command, Params, Cmd.UUID, Sent);
			StorageService()->AddCommand(SerialNumber_, Cmd, Storage::COMMAND_EXECUTED);
			return true;
		}
        return false;
    }

	bool WSConnection::ExtractCompressedData(const std::string & CompressedData, std::string & UnCompressedData)
    {
        std::vector<uint8_t> OB = Utils::base64decode(CompressedData);

        unsigned long MaxSize=OB.size()*10;
        std::vector<char> UncompressedBuffer(MaxSize);
        unsigned long FinalSize = MaxSize;
        if(uncompress((Bytef *)&UncompressedBuffer[0], & FinalSize, (Bytef *)&OB[0],OB.size())==Z_OK) {
			UncompressedBuffer[FinalSize] = 0;
			UnCompressedData = &UncompressedBuffer[0];
			return true;
		}
		return false;
    }

    void WSConnection::ProcessJSONRPCResult(Poco::JSON::Object::Ptr & Doc) {
		CommandManager()->PostCommandResult(SerialNumber_, Doc);
    }

    void WSConnection::ProcessJSONRPCEvent(Poco::JSON::Object::Ptr & Doc) {

        auto Method = Doc->get(uCentralProtocol::METHOD).toString();
		auto EventType = uCentralProtocol::EventFromString(Method);
		if(EventType == uCentralProtocol::ET_UNKNOWN) {
			Logger().error(Poco::format("ILLEGAL-PROTOCOL(%s): Unknown message type '%s'",Method));
			Errors_++;
			return;
		}

        if(!Doc->isObject(uCentralProtocol::PARAMS))
        {
            Logger().warning(Poco::format("MISSING-PARAMS(%s): params must be an object.",CId_));
			Errors_++;
            return;
        }

        //  expand params if necessary
        auto ParamsObj = Doc->get(uCentralProtocol::PARAMS).extract<Poco::JSON::Object::Ptr>();
        if(ParamsObj->has(uCentralProtocol::COMPRESS_64))
        {
			std::string UncompressedData;
            if(ExtractCompressedData(ParamsObj->get(uCentralProtocol::COMPRESS_64).toString(),UncompressedData)) {
				Logger().debug(Poco::format("EVENT(%s): Found compressed payload expanded to '%s'.",CId_, UncompressedData));
				Poco::JSON::Parser	Parser;
				ParamsObj = Parser.parse(UncompressedData).extract<Poco::JSON::Object::Ptr>();
			} else {
				Logger().warning(Poco::format("INVALID-COMPRESSED-DATA(%s): Compressed cannot be uncompressed - content must be corrupt..",CId_));
				Errors_++;
				return;
			}
        }

        if(!ParamsObj->has(uCentralProtocol::SERIAL))
        {
            Logger().warning(Poco::format("MISSING-PARAMS(%s): Serial number is missing in message.",CId_));
            return;
        }

		auto Serial = Poco::trim(Poco::toLower(ParamsObj->get(uCentralProtocol::SERIAL).toString()));
		if(!Utils::ValidSerialNumber(Serial)) {
			Poco::Exception	E(Poco::format("ILLEGAL-DEVICE-NAME(%s): device name is illegal and not allowed to connect.",Serial), EACCES);
			E.rethrow();
		}

		if(StorageService()->IsBlackListed(Serial)) {
			Poco::Exception	E(Poco::format("BLACKLIST(%s): device is blacklisted and not allowed to connect.",Serial), EACCES);
			E.rethrow();
		}

		if(Conn_!= nullptr)
			Conn_->Conn_.LastContact = std::time(nullptr);

		switch(EventType) {
			case uCentralProtocol::ET_CONNECT: {
				if( ParamsObj->has(uCentralProtocol::UUID) &&
					ParamsObj->has(uCentralProtocol::FIRMWARE) &&
					ParamsObj->has(uCentralProtocol::CAPABILITIES)) {
						uint64_t UUID = ParamsObj->get(uCentralProtocol::UUID);
						auto Firmware = ParamsObj->get(uCentralProtocol::FIRMWARE).toString();
						auto Capabilities = ParamsObj->get(uCentralProtocol::CAPABILITIES).toString();

						Conn_ = DeviceRegistry()->Register(Serial, this, ConnectionId_);
						SerialNumber_ = Serial;
						Conn_->Conn_.SerialNumber = Serial;
						Conn_->Conn_.UUID = UUID;
						Conn_->Conn_.Firmware = Firmware;
						Conn_->Conn_.PendingUUID = 0;
						Conn_->Conn_.LastContact = std::time(nullptr);
						Conn_->Conn_.Address = Utils::FormatIPv6(WS_->peerAddress().toString());
						CId_ = SerialNumber_ + "@" + CId_ ;

						//	We need to verify the certificate if we have one
						if((!CN_.empty() && Utils::SerialNumberMatch(CN_,SerialNumber_)) || WebSocketServer()->IsSimSerialNumber(CN_)) {
							CertValidation_ = GWObjects::VERIFIED;
							Logger().information(Poco::format("CONNECT(%s): Fully validated and authenticated device..", CId_));
						} else {
							if(CN_.empty())
								Logger().information(Poco::format("CONNECT(%s): Not authenticated or validated.", CId_));
							else
								Logger().information(Poco::format("CONNECT(%s): Authenticated but not validated. Serial='%s' CN='%s'", CId_, Serial, CN_));
						}
						Conn_->Conn_.VerifiedCertificate = CertValidation_;

						auto DeviceExists = SerialNumberCache()->NumberExists(SerialNumber_);
						if (Daemon()->AutoProvisioning() && !DeviceExists) {
							StorageService()->CreateDefaultDevice(SerialNumber_, Capabilities, Firmware, Compatible_, PeerAddress_);
							Conn_->Conn_.Compatible = Compatible_;
						} else if (DeviceExists) {
							StorageService()->UpdateDeviceCapabilities(SerialNumber_, Capabilities, Compatible_);
							Conn_->Conn_.Compatible = Compatible_;
							if(!Firmware.empty()) {
								StorageService()->SetConnectInfo(SerialNumber_, Firmware );
							}
							LookForUpgrade(UUID);
						}

						StatsProcessor_ = std::make_unique<StateProcessor>(Conn_, Logger());
						StatsProcessor_->Initialize(Serial);

						if(KafkaManager()->Enabled()) {
							Poco::JSON::Stringifier		Stringify;
							ParamsObj->set(uCentralProtocol::CONNECTIONIP,CId_);
							std::ostringstream OS;
							Stringify.condense(ParamsObj,OS);
							KafkaManager()->PostMessage(KafkaTopics::CONNECTION, SerialNumber_, OS.str());
						}
						Connected_ = true;
					} else {
						Logger().warning(Poco::format("CONNECT(%s): Missing one of uuid, firmware, or capabilities",CId_));
						Errors_++;
						return;
					}
				}
				break;

			case uCentralProtocol::ET_STATE: {
					if(!Connected_) {
						Logger().debug(Poco::format("INVALID-PROTOCOL(%s): Device '%s' is not following protocol", CId_, CN_));
						Errors_++;
						return;
					}
					if (ParamsObj->has(uCentralProtocol::UUID) && ParamsObj->has(uCentralProtocol::STATE)) {
						uint64_t UUID = ParamsObj->get(uCentralProtocol::UUID);
						auto State = ParamsObj->get(uCentralProtocol::STATE).toString();

						std::string request_uuid;
						if (ParamsObj->has(uCentralProtocol::REQUEST_UUID))
							request_uuid = ParamsObj->get(uCentralProtocol::REQUEST_UUID).toString();

						if (request_uuid.empty())
							Logger().debug(Poco::format("STATE(%s): UUID=%Lu Updating.", CId_, UUID));
						else
							Logger().debug(Poco::format("STATE(%s): UUID=%Lu Updating for CMD=%s.", CId_,
													   UUID, request_uuid));
						Conn_->Conn_.UUID = UUID;
						LookForUpgrade(UUID);
						GWObjects::Statistics	Stats{ .SerialNumber = SerialNumber_, .UUID = UUID, .Data = State};
						Stats.Recorded = std::time(nullptr);
						StorageService()->AddStatisticsData(Stats);
						if (!request_uuid.empty()) {
							StorageService()->SetCommandResult(request_uuid, State);
						}

						StatsProcessor_->Add(State);

						if(KafkaManager()->Enabled()) {
							Poco::JSON::Stringifier		Stringify;
							std::ostringstream OS;
							Stringify.condense(ParamsObj,OS);
							KafkaManager()->PostMessage(KafkaTopics::STATE, SerialNumber_, OS.str());
						}
					} else {
						Logger().warning(Poco::format(
							"STATE(%s): Invalid request. Missing serial, uuid, or state", CId_));
					}
				}
				break;

			case uCentralProtocol::ET_HEALTHCHECK: {
					if(!Connected_) {
						Logger().debug(Poco::format("INVALID-PROTOCOL(%s): Device '%s' is not following protocol", CId_, CN_));
						Errors_++;
						return;
					}
					if (ParamsObj->has(uCentralProtocol::UUID) && ParamsObj->has(uCentralProtocol::SANITY) && ParamsObj->has(uCentralProtocol::DATA)) {
						uint64_t UUID = ParamsObj->get(uCentralProtocol::UUID);
						auto Sanity = ParamsObj->get(uCentralProtocol::SANITY);
						auto CheckData = ParamsObj->get(uCentralProtocol::DATA).toString();
						if (CheckData.empty())
							CheckData = uCentralProtocol::EMPTY_JSON_DOC;

						std::string request_uuid;
						if (ParamsObj->has(uCentralProtocol::REQUEST_UUID))
							request_uuid = ParamsObj->get(uCentralProtocol::REQUEST_UUID).toString();

						if (request_uuid.empty())
							Logger().debug(
								Poco::format("HEALTHCHECK(%s): UUID=%Lu Updating.", CId_, UUID));
						else
							Logger().debug(Poco::format("HEALTHCHECK(%s): UUID=%Lu Updating for CMD=%s.",
													   CId_, UUID, request_uuid));

						Conn_->Conn_.UUID = UUID;
						LookForUpgrade(UUID);

						GWObjects::HealthCheck Check;

						Check.SerialNumber = SerialNumber_;
						Check.Recorded = std::time(nullptr);
						Check.UUID = UUID;
						Check.Data = CheckData;
						Check.Sanity = Sanity;

						StorageService()->AddHealthCheckData(Check);

						if (!request_uuid.empty()) {
							StorageService()->SetCommandResult(request_uuid, CheckData);
						}

						DeviceRegistry()->SetHealthcheck(Serial, Check);
						if(KafkaManager()->Enabled()) {
							Poco::JSON::Stringifier		Stringify;
							std::ostringstream OS;
							ParamsObj->set("timestamp",std::time(nullptr));
							Stringify.condense(ParamsObj,OS);
							KafkaManager()->PostMessage(KafkaTopics::HEALTHCHECK, SerialNumber_, OS.str());
						}
					} else {
						Logger().warning(Poco::format("HEALTHCHECK(%s): Missing parameter", CId_));
						return;
					}
				}
				break;

			case uCentralProtocol::ET_LOG: {
					if(!Connected_) {
						Logger().debug(Poco::format("INVALID-PROTOCOL(%s): Device '%s' is not following protocol", CId_, CN_));
						Errors_++;
						return;
					}
					if (ParamsObj->has(uCentralProtocol::LOG) && ParamsObj->has(uCentralProtocol::SEVERITY)) {
						Logger().debug(Poco::format("LOG(%s): new entry.", CId_));
						auto Log = ParamsObj->get(uCentralProtocol::LOG).toString();
						auto Severity = ParamsObj->get(uCentralProtocol::SEVERITY);
						std::string DataStr = uCentralProtocol::EMPTY_JSON_DOC;
						if (ParamsObj->has(uCentralProtocol::DATA)) {
							auto DataObj = ParamsObj->get(uCentralProtocol::DATA);
							if (DataObj.isStruct())
								DataStr = DataObj.toString();
						}

						GWObjects::DeviceLog DeviceLog{		.SerialNumber = SerialNumber_,
															.Log = Log,
														   .Data = DataStr,
														   .Severity = Severity,
														   .Recorded = (uint64_t)time(nullptr),
														   .LogType = 0,
														   .UUID = Conn_->Conn_.UUID};
						StorageService()->AddLog(DeviceLog);
					} else {
						Logger().warning(Poco::format("LOG(%s): Missing parameters.", CId_));
						return;
					}
				}
				break;

			case uCentralProtocol::ET_CRASHLOG: {
					if (ParamsObj->has(uCentralProtocol::UUID) && ParamsObj->has(uCentralProtocol::LOGLINES)) {

						Logger().debug(Poco::format("CRASH-LOG(%s): new entry.", CId_));
						auto LogLines = ParamsObj->get(uCentralProtocol::LOGLINES);
						std::string LogText;
						if (LogLines.isArray()) {
							auto LogLinesArray = LogLines.extract<Poco::JSON::Array::Ptr>();
							for (const auto &i : *LogLinesArray)
								LogText += i.toString() + "\r\n";
						}

						GWObjects::DeviceLog DeviceLog{
							.SerialNumber = SerialNumber_,
							.Log = LogText,
							.Data = "",
							.Severity = GWObjects::DeviceLog::LOG_EMERG,
							.Recorded = (uint64_t)time(nullptr),
							.LogType = 1,
							.UUID = 0};
						StorageService()->AddLog(DeviceLog);

					} else {
						Logger().warning(Poco::format("LOG(%s): Missing parameters.", CId_));
						return;
					}
				}
				break;

			case uCentralProtocol::ET_PING: {
					if (ParamsObj->has(uCentralProtocol::UUID)) {
						uint64_t UUID = ParamsObj->get(uCentralProtocol::UUID);
						Logger().debug(Poco::format("PING(%s): Current config is %Lu", CId_, UUID));
					} else {
						Logger().warning(Poco::format("PING(%s): Missing parameter.", CId_));
					}
				}
				break;

			case uCentralProtocol::ET_CFGPENDING: {
					if(!Connected_) {
						Logger().debug(Poco::format("INVALID-PROTOCOL(%s): Device '%s' is not following protocol", CId_, CN_));
						Errors_++;
						return;
					}
					if (ParamsObj->has(uCentralProtocol::UUID) && ParamsObj->has(uCentralProtocol::ACTIVE)) {

						uint64_t UUID = ParamsObj->get(uCentralProtocol::UUID);
						uint64_t Active = ParamsObj->get(uCentralProtocol::ACTIVE);

						Logger().debug(Poco::format("CFG-PENDING(%s): Active: %Lu Target: %Lu", CId_,
												   Active, UUID));
					} else {
						Logger().warning(Poco::format("CFG-PENDING(%s): Missing some parameters", CId_));
					}
				}
				break;

			case uCentralProtocol::ET_RECOVERY: {
					if (ParamsObj->has(uCentralProtocol::SERIAL) && ParamsObj->has(uCentralProtocol::FIRMWARE) &&
						ParamsObj->has(uCentralProtocol::UUID) && ParamsObj->has(uCentralProtocol::REBOOT) &&
						ParamsObj->has(uCentralProtocol::LOGLINES)) {

						auto LogLines = ParamsObj->get(uCentralProtocol::LOGLINES);
						std::string LogText;

						LogText = "Firmware: " + ParamsObj->get(uCentralProtocol::FIRMWARE).toString() + "\r\n";
						if (LogLines.isArray()) {
							auto LogLinesArray = LogLines.extract<Poco::JSON::Array::Ptr>();
							for (const auto &i : *LogLinesArray)
								LogText += i.toString() + "\r\n";
						}

						GWObjects::DeviceLog DeviceLog{
							.SerialNumber = SerialNumber_,
							.Log = LogText,
							.Data = "",
							.Severity = GWObjects::DeviceLog::LOG_EMERG,
							.Recorded = (uint64_t)time(nullptr),
							.LogType = 1,
							.UUID = 0};

						StorageService()->AddLog(DeviceLog);

						if(ParamsObj->get(uCentralProtocol::REBOOT).toString()=="true") {
							GWObjects::CommandDetails  Cmd;
							Cmd.SerialNumber = SerialNumber_;
							Cmd.UUID = MicroService::CreateUUID();
							Cmd.SubmittedBy = uCentralProtocol::SUBMITTED_BY_SYSTEM;
							Cmd.Status = uCentralProtocol::PENDING;
							Cmd.Command = uCentralProtocol::REBOOT;
							Poco::JSON::Object Params;
							Params.set(uCentralProtocol::SERIAL, SerialNumber_);
							Params.set(uCentralProtocol::WHEN, 0);
							std::ostringstream O;
							Poco::JSON::Stringifier::stringify(Params, O);
							Cmd.Details = O.str();
							bool Sent;
							CommandManager()->PostCommand(SerialNumber_ , Cmd.Command, Params, Cmd.UUID, Sent);
							StorageService()->AddCommand(SerialNumber_, Cmd, Storage::COMMAND_EXECUTED);
							Logger().information(Poco::format("RECOVERY(%s): Recovery mode received, need for a reboot.",CId_));
						} else {
							Logger().information(Poco::format("RECOVERY(%s): Recovery mode received, no need for a reboot.",CId_));
						}
					} else {
						Logger().error(Poco::format(
							"RECOVERY(%s): Recovery missing one of serialnumber, firmware, uuid, loglines, reboot",
							Serial));
					}
				}
				break;

			case uCentralProtocol::ET_DEVICEUPDATE: {
					if(!Connected_) {
						Logger().debug(Poco::format("INVALID-PROTOCOL(%s): Device '%s' is not following protocol", CId_, CN_));
						Errors_++;
						return;
					}
					if (ParamsObj->has("currentPassword")) {
						auto Password = ParamsObj->get("currentPassword").toString();

						StorageService()->SetDevicePassword(Serial, Password);
						Logger().error(Poco::format(
							"DEVICEUPDATE(%s): Device is updating its login password.", Serial));
					}
				}
				break;

			case uCentralProtocol::ET_TELEMETRY: {
					if(!Connected_) {
						Logger().debug(Poco::format("INVALID-PROTOCOL(%s): Device '%s' is not following protocol", CId_, CN_));
						Errors_++;
						return;
					}
					std::cout << "Telemetry received" << std::endl;
					if(TelemetryReporting_) {
						if (ParamsObj->has("data")) {
							std::cout << "Telemetry received has data" << std::endl;
							auto Payload =
								ParamsObj->get("data").extract<Poco::JSON::Object::Ptr>();
							Payload->set("timestamp", std::time(nullptr));
							std::ostringstream SS;
							Payload->stringify(SS);
							if(TelemetryWebSocketRefCount_) {
								std::cout << "Updating websocket..." << std::endl;
								TelemetryWebSocketPackets_++;
								Conn_->Conn_.websocketPackets = TelemetryWebSocketPackets_;
								TelemetryStream()->UpdateEndPoint(SerialNumber_, SS.str());
							}
							if (TelemetryKafkaRefCount_ && KafkaManager()->Enabled()) {
								TelemetryKafkaPackets_++;
								Conn_->Conn_.kafkaPackets = TelemetryKafkaPackets_;
								KafkaManager()->PostMessage(KafkaTopics::DEVICE_TELEMETRY,
															SerialNumber_, SS.str());
							}
						} else {
							std::ostringstream SS;
							ParamsObj->stringify(SS);
							std::cout << "Bad telemetry packet: " << SS.str() << std::endl;
						}
					}
				}
				break;

			// 	this will never be called but some compilers will complain if we do not have a case for
			//	every single values of an enum
			case uCentralProtocol::ET_UNKNOWN: {
				Logger().error(Poco::format("ILLEGAL-EVENT(%s): Event '%s' unknown. CN=%s", CId_, Method, CN_));
				Errors_++;
			}
		}
    }

	bool  WSConnection::StartTelemetry() {
		std::cout << "Starting telemetry for " << SerialNumber_ << std::endl;
		Logger().information(Poco::format("TELEMETRY(%s): Starting.",CId_));
		Poco::JSON::Object	StartMessage;
		StartMessage.set("jsonrpc","2.0");
		StartMessage.set("method","telemetry");
		Poco::JSON::Object	Params;
		Params.set("serial", SerialNumber_);
		Params.set("interval",TelemetryInterval_);
		Poco::JSON::Array	Types;
		Types.add("wifi-frames");
		Types.add("dhcp-snooping");
		Types.add("state");
		Params.set(RESTAPI::Protocol::TYPES, Types);
		StartMessage.set("id",1);
		StartMessage.set("params",Params);
		Poco::JSON::Stringifier		Stringify;
		std::ostringstream OS;
		Stringify.condense(StartMessage,OS);
		Send(OS.str());
		// std::cout << "Starting: " << OS.str() << std::endl;
		return true;
	}

	bool  WSConnection::StopTelemetry() {
		std::cout << "Stopping telemetry for " << SerialNumber_ << std::endl;
		Logger().information(Poco::format("TELEMETRY(%s): Stopping.",CId_));
		Poco::JSON::Object	StopMessage;
		StopMessage.set("jsonrpc","2.0");
		StopMessage.set("method","telemetry");
		Poco::JSON::Object	Params;
		Params.set("serial", SerialNumber_);
		Params.set("interval",0);
		StopMessage.set("id",1);
		StopMessage.set("params",Params);
		Poco::JSON::Stringifier		Stringify;
		std::ostringstream OS;
		Stringify.condense(StopMessage,OS);
		Send(OS.str());
		TelemetryKafkaPackets_ = TelemetryWebSocketPackets_ = TelemetryInterval_ =
			TelemetryKafkaTimer_ = TelemetryWebSocketTimer_ = 0 ;
		// std::cout << "Stopping: " << OS.str() << std::endl;
		return true;
	}

	void WSConnection::UpdateCounts() {
		if(Conn_) {
			Conn_->Conn_.kafkaClients = TelemetryKafkaRefCount_;
			Conn_->Conn_.webSocketClients = TelemetryWebSocketRefCount_;
		}
	}

	bool WSConnection::SetWebSocketTelemetryReporting(uint64_t Interval, uint64_t TelemetryWebSocketTimer) {
		std::lock_guard	G(Mutex_);
		TelemetryWebSocketRefCount_++;
		TelemetryInterval_ = TelemetryInterval_ ? std::min(Interval, TelemetryInterval_) : Interval;
		TelemetryWebSocketTimer_ = std::max(TelemetryWebSocketTimer,TelemetryWebSocketTimer_);
		UpdateCounts();
		if(!TelemetryReporting_) {
			TelemetryReporting_ = true;
			return StartTelemetry();
		}
		return true;
	}

	bool WSConnection::SetKafkaTelemetryReporting(uint64_t Interval, uint64_t TelemetryKafkaTimer) {
		std::lock_guard	G(Mutex_);
		TelemetryKafkaRefCount_++;
		TelemetryInterval_ = TelemetryInterval_ ? std::min(Interval, TelemetryInterval_) : Interval;
		TelemetryKafkaTimer_ = std::max(TelemetryKafkaTimer,TelemetryKafkaTimer_);
		UpdateCounts();
		if(!TelemetryReporting_) {
			TelemetryReporting_ = true;
			return StartTelemetry();
		}
		return true;
	}

	bool WSConnection::StopWebSocketTelemetry() {
		std::lock_guard	G(Mutex_);
		if(TelemetryWebSocketRefCount_)
			TelemetryWebSocketRefCount_--;
		UpdateCounts();
		if(TelemetryWebSocketRefCount_==0 && TelemetryKafkaRefCount_==0) {
			TelemetryReporting_ = false;
			StopTelemetry();
		}
		return true;
	}

	bool WSConnection::StopKafkaTelemetry() {
		std::lock_guard	G(Mutex_);
		if(TelemetryKafkaRefCount_)
			TelemetryKafkaRefCount_--;
		UpdateCounts();
		if(TelemetryWebSocketRefCount_==0 && TelemetryKafkaRefCount_==0) {
			TelemetryReporting_ = false;
			StopTelemetry();
		}
		return true;
	}

	void WSConnection::OnSocketShutdown(const Poco::AutoPtr<Poco::Net::ShutdownNotification>& pNf) {
		std::lock_guard Guard(Mutex_);
        Logger().information(Poco::format("SOCKET-SHUTDOWN(%s): Closing.",CId_));
        delete this;
    }

    void WSConnection::OnSocketError(const Poco::AutoPtr<Poco::Net::ErrorNotification>& pNf) {
		std::lock_guard Guard(Mutex_);
        Logger().information(Poco::format("SOCKET-ERROR(%s): Closing.",CId_));
        delete this;
    }

    void WSConnection::OnSocketReadable(const Poco::AutoPtr<Poco::Net::ReadableNotification>& pNf) {
		std::lock_guard Guard(Mutex_);
        try
        {
            ProcessIncomingFrame();
        }
        catch (const Poco::Exception & E)
        {
            Logger().log(E);
            delete this;
        }
		catch (const std::exception & E) {
			std::string W = E.what();
			Logger().information(Poco::format("std::exception caught: %s. Connection terminated with %s",W,CId_));
			delete this;
		}
		catch ( ... ) {
			Logger().information(Poco::format("Unknown exception for %s. Connection terminated.",CId_));
			delete this;
		}
    }

	std::string asString(Poco::Buffer<char> & buf ) {
		if(buf.sizeBytes()>0) {
			buf.append(0);
			return buf.begin();
		}
		return "";
	}

    void WSConnection::ProcessIncomingFrame() {

        // bool MustDisconnect=false;
		Poco::Buffer<char>			IncomingFrame(0);

        try {
			int Op,flags;
			int IncomingSize;
			IncomingSize = WS_->receiveFrame(IncomingFrame,flags);
            Op = flags & Poco::Net::WebSocket::FRAME_OP_BITMASK;

            if (IncomingSize == 0 && flags == 0 && Op == 0) {
                Logger().information(Poco::format("DISCONNECT(%s): device has disconnected.", CId_));
				return delete this;
            } else {

            	if (Conn_ != nullptr) {
            		Conn_->Conn_.RX += IncomingSize;
            		Conn_->Conn_.MessageCount++;
            	}

                switch (Op) {
                    case Poco::Net::WebSocket::FRAME_OP_PING: {
							Logger().debug(Poco::format("WS-PING(%s): received. PONG sent back.", CId_));
							WS_->sendFrame("", 0,
										   (int)Poco::Net::WebSocket::FRAME_OP_PONG |
											   (int)Poco::Net::WebSocket::FRAME_FLAG_FIN);
							if (Conn_ != nullptr) {
								Conn_->Conn_.MessageCount++;
							}

							if (KafkaManager()->Enabled() && Conn_) {
								Poco::JSON::Object PingObject;
								Poco::JSON::Object PingDetails;
								PingDetails.set(uCentralProtocol::FIRMWARE, Conn_->Conn_.Firmware);
								PingDetails.set(uCentralProtocol::SERIALNUMBER, SerialNumber_);
								PingDetails.set(uCentralProtocol::COMPATIBLE, Compatible_);
								PingDetails.set(uCentralProtocol::CONNECTIONIP, CId_);
								PingObject.set(uCentralProtocol::PING,PingDetails);
								Poco::JSON::Stringifier Stringify;
								std::ostringstream OS;
								Stringify.condense(PingObject, OS);
								KafkaManager()->PostMessage(KafkaTopics::CONNECTION, SerialNumber_,
															OS.str());
							}
							return;
						}
                        break;

                    case Poco::Net::WebSocket::FRAME_OP_PONG: {
                        	Logger().debug(Poco::format("PONG(%s): received and ignored.",CId_));
							return;
                        }
                        break;

                    case Poco::Net::WebSocket::FRAME_OP_TEXT: {
							std::string IncomingMessageStr = asString(IncomingFrame);
							Logger().debug(Poco::format("FRAME(%s): Frame received (length=%d, flags=0x%x). Msg=%s",
										CId_, IncomingSize, unsigned(flags),IncomingMessageStr));

							Poco::JSON::Parser parser;
							auto ParsedMessage = parser.parse(IncomingMessageStr);
							auto IncomingJSON = ParsedMessage.extract<Poco::JSON::Object::Ptr>();

							if (IncomingJSON->has(uCentralProtocol::JSONRPC)) {
								if(IncomingJSON->has(uCentralProtocol::METHOD) &&
									IncomingJSON->has(uCentralProtocol::PARAMS)) {
										ProcessJSONRPCEvent(IncomingJSON);
								} else if (IncomingJSON->has(uCentralProtocol::RESULT) &&
									IncomingJSON->has(uCentralProtocol::ID)) {
									Logger().debug(Poco::format("RPC-RESULT(%s): payload: %s",CId_,IncomingMessageStr));
									ProcessJSONRPCResult(IncomingJSON);
								} else {
									Logger().warning(Poco::format(
										"INVALID-PAYLOAD(%s): Payload is not JSON-RPC 2.0: %s", CId_,
										IncomingMessageStr));
								}
							} else {
								Logger().error(Poco::format("FRAME(%s): illegal transaction header, missing 'jsonrpc'",CId_));
								Errors_++;
							}
							return;
	                    }
    	                break;

					case Poco::Net::WebSocket::FRAME_OP_CLOSE: {
							Logger().warning(Poco::format("CLOSE(%s): Device is closing its connection.",CId_));
							return delete this;
						}
						break;

                    default: {
                            Logger().warning(Poco::format("UNKNOWN(%s): unknownWS Frame operation: %s",CId_, std::to_string(Op)));
                        }
                        break;
                    }
                }
            }
        catch (const Poco::Net::ConnectionResetException & E)
        {
			std::string IncomingMessageStr = asString(IncomingFrame);
            Logger().warning(Poco::format("%s(%s): Caught a ConnectionResetException: %s, Message: %s",
							std::string(__func__), CId_, E.displayText(),IncomingMessageStr));
            return delete this;
        }
        catch (const Poco::JSON::JSONException & E)
        {
			std::string IncomingMessageStr = asString(IncomingFrame);
			Logger().warning(Poco::format("%s(%s): Caught a JSONException: %s. Message: %s",
								std::string(__func__), CId_, E.displayText(), IncomingMessageStr ));
        }
        catch (const Poco::Net::WebSocketException & E)
        {
			std::string IncomingMessageStr = asString(IncomingFrame);
			Logger().warning(Poco::format("%s(%s): Caught a websocket exception: %s. Message: %s",
							std::string(__func__), CId_, E.displayText(), IncomingMessageStr ));
			return delete this;
        }
        catch (const Poco::Net::SSLConnectionUnexpectedlyClosedException & E)
        {
			std::string IncomingMessageStr = asString(IncomingFrame);
			Logger().warning(Poco::format("%s(%s): Caught a SSLConnectionUnexpectedlyClosedException: %s. Message: %s",
							std::string(__func__), CId_, E.displayText(), IncomingMessageStr ));
			return delete this;
        }
        catch (const Poco::Net::SSLException & E)
        {
			std::string IncomingMessageStr = asString(IncomingFrame);
			Logger().warning(Poco::format("%s(%s): Caught a SSL exception: %s. Message: %s",
							std::string(__func__), CId_, E.displayText(), IncomingMessageStr ));
			return delete this;
        }
        catch (const Poco::Net::NetException & E) {
			std::string IncomingMessageStr = asString(IncomingFrame);
			Logger().warning( Poco::format("%s(%s): Caught a NetException: %s. Message: %s",
							 std::string(__func__), CId_, E.displayText(), IncomingMessageStr ));
			return delete this;
        }
        catch (const Poco::IOException & E) {
			std::string IncomingMessageStr = asString(IncomingFrame);
			Logger().warning( Poco::format("%s(%s): Caught a IOException: %s. Message: %s",
							 std::string(__func__), CId_, E.displayText(), IncomingMessageStr ));
			return delete this;
        }
        catch (const Poco::Exception &E) {
			std::string IncomingMessageStr = asString(IncomingFrame);
            Logger().warning( Poco::format("%s(%s): Caught a more generic Poco exception: %s. Message: %s",
							 std::string(__func__), CId_, E.displayText(), IncomingMessageStr ));
            return delete this;
        }
        catch (const std::exception & E) {
			std::string IncomingMessageStr = asString(IncomingFrame);
            Logger().warning( Poco::format("%s(%s): Caught a std::exception: %s. Message: %s",
							 std::string{__func__}, CId_, std::string{E.what()}, IncomingMessageStr) );
            return delete this;
        }
		catch (...) {
			return delete this;
		}

		if(Errors_<10)
			return;

		Logger().information(Poco::format("DISCONNECTING(%s): Too many errors",CId_));
        delete this;
    }

	bool WSConnection::Send(const std::string &Payload) {
		std::lock_guard Guard(Mutex_);

		auto BytesSent = WS_->sendFrame(Payload.c_str(),(int)Payload.size());
		if(Conn_)
			Conn_->Conn_.TX += BytesSent;
		return  BytesSent == Payload.size();
	}

}      //namespace
