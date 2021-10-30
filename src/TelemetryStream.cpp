//
// Created by stephane bourque on 2021-09-07.
//
#include <thread>

#include "Poco/JSON/Array.h"
#include "Poco/Net/HTTPHeaderStream.h"
#include "Poco/Net/HTTPServer.h"
#include "Poco/Net/HTTPServerRequest.h"
#include "Poco/Net/HTTPServerRequestImpl.h"
#include "Poco/Net/HTTPServerResponse.h"
#include "Poco/Net/HTTPServerSession.h"
#include "Poco/Net/IPAddress.h"
#include "Poco/Net/SSLException.h"
#include "Poco/Net/SecureStreamSocket.h"
#include "Poco/Net/SecureStreamSocketImpl.h"
#include "Poco/Net/WebSocket.h"
#include "Poco/URI.h"
#include "Poco/zlib.h"

#include "DeviceRegistry.h"
#include "RESTAPI/RESTAPI_TelemetryWebSocket.h"
#include "TelemetryStream.h"
#include "framework/MicroService.h"

namespace OpenWifi {

	class TelemetryStream *TelemetryStream::instance_ = nullptr;

	int TelemetryStream::Start() {
		ReactorPool_.Start();
		Runner.start(*this);
		return 0;
	}

	void TelemetryStream::Stop() {
		if(Running_) {
			Running_ = false;
			Runner.join();
		}

		Logger_.notice("Stopping reactors...");
		ReactorPool_.Stop();
	}

	bool TelemetryStream::CreateEndpoint(const std::string &SerialNumber, std::string &EndPoint, std::string &UUID) {
		std::lock_guard	G(Mutex_);

		Poco::URI	Public(MicroService::instance().ConfigGetString("openwifi.system.uri.public"));
		Poco::URI	U;
		UUID = MicroService::instance().CreateUUID();
		U.setScheme("wss");
		U.setHost(Public.getHost());
		U.setPort(Public.getPort());
		auto RESTAPI_Path = std::string(*(RESTAPI_TelemetryWebSocket::PathName().begin()));
		U.setPath(RESTAPI_Path);
		U.addQueryParameter("uuid", UUID);
		U.addQueryParameter("serialNumber", SerialNumber);
		EndPoint = U.toString();
		auto H = SerialNumbers_.find(SerialNumber);
		if(H == SerialNumbers_.end()) {
			std::set<std::string>	UUIDs{UUID};
			SerialNumbers_[SerialNumber] = UUIDs;
		} else {
			H->second.insert(UUID);
		}
		Clients_[UUID] = nullptr;
		return true;
	}

	void TelemetryStream::UpdateEndPoint(const std::string &SerialNumber, const std::string &PayLoad) {
		std::lock_guard	G(QueueMutex_);
		Queue_.push(QueueUpdate{.SerialNumber=SerialNumber,.Payload=PayLoad});
		Runner.wakeUp();
	}

	void TelemetryStream::run() {
		Running_ = true;
		QueueUpdate	Entry;
		std::unique_lock	QLock(QueueMutex_);
		std::unique_lock	CLock(Mutex_);

		while(Running_) {

			QLock.lock();
			if(Queue_.empty()) {
				QLock.unlock();
				Poco::Thread::trySleep(2000);
				continue;
			}

			if(!Running_)
				break;

			QLock.lock();
			if(Queue_.empty()) {
				QLock.unlock();
				continue;
			}

			Entry = Queue_.front();
			Queue_.pop();
			QLock.unlock();

			CLock.lock();
			auto H1 = SerialNumbers_.find(Entry.SerialNumber);
			if(H1!=SerialNumbers_.end()) {
				for(auto &i:H1->second) {
					auto H2 = Clients_.find(i);
					if(H2!=Clients_.end() && H2->second!= nullptr) {
						try {
							H2->second->Send(Entry.Payload);
						} catch(...) {

						}
					}
				}
			}
			CLock.unlock();
		}
	}

	bool TelemetryStream::RegisterClient(const std::string &UUID, TelemetryClient *Client) {
		std::lock_guard	G(Mutex_);
		Clients_[UUID] = Client;
		return true;
	}

	void TelemetryStream::DeRegisterClient(const std::string &UUID) {
		std::lock_guard		G(Mutex_);

		auto Hint = Clients_.find(UUID);
		if(Hint!=Clients_.end()) {
			Clients_.erase(Hint);
			for(const auto &i:SerialNumbers_) {
				auto S = i.second;
				S.erase(UUID);
			}

			//	remove empty slots
			for( auto i = SerialNumbers_.begin(); i!= SerialNumbers_.end();) {
				if(i->second.empty()) {
					i = SerialNumbers_.erase(i);
				} else {
					++i;
				}
			}
		}
	}

	TelemetryClient::TelemetryClient(
			std::string UUID,
			std::string SerialNumber,
			Poco::SharedPtr<Poco::Net::WebSocket> WSock,
			Poco::Net::SocketReactor& Reactor,
			Poco::Logger &Logger):
			UUID_(std::move(UUID)), SerialNumber_(std::move(SerialNumber)), WS_(WSock),Reactor_(Reactor), Logger_(Logger) {

		try {
			std::thread T([this]() { this->CompleteStartup(); });
			T.detach();
			return;
		} catch (...) {
			delete this;
		}
	}

	void TelemetryClient::CompleteStartup() {
		std::lock_guard Guard(Mutex_);
		try {
			Socket_ = *WS_;
			CId_ = Utils::FormatIPv6(Socket_.peerAddress().toString());

			// auto SS = static_cast<Poco::Net::SecureStreamSocketImpl*>((WS_->impl()));
			// SS->havePeerCertificate();

			if (TelemetryStream()->RegisterClient(UUID_, this)) {
				auto TS = Poco::Timespan(240, 0);

				WS_->setReceiveTimeout(TS);
				WS_->setNoDelay(true);
				WS_->setKeepAlive(true);
				Reactor_.addEventHandler(
					*WS_, Poco::NObserver<TelemetryClient, Poco::Net::ReadableNotification>(
						*this, &TelemetryClient::OnSocketReadable));
				Reactor_.addEventHandler(
					*WS_, Poco::NObserver<TelemetryClient, Poco::Net::ShutdownNotification>(
						*this, &TelemetryClient::OnSocketShutdown));
				Reactor_.addEventHandler(
					*WS_, Poco::NObserver<TelemetryClient, Poco::Net::ErrorNotification>(
						*this, &TelemetryClient::OnSocketError));
				Registered_ = true;
				Logger_.information(Poco::format("CONNECTION(%s): completed.", CId_));
				return;
			}
		} catch (const Poco::Net::SSLException &E) {
			Logger_.log(E);
		}
		catch (const Poco::Exception &E) {
			Logger_.log(E);
		}
		delete this;
	}

	TelemetryClient::~TelemetryClient() {
		Logger_.information("Closing telemetry session.");
		if(Registered_ && WS_)
		{
			Reactor_.removeEventHandler(*WS_,
										Poco::NObserver<TelemetryClient,
										Poco::Net::ReadableNotification>(*this,&TelemetryClient::OnSocketReadable));
			Reactor_.removeEventHandler(*WS_,
										Poco::NObserver<TelemetryClient,
										Poco::Net::ShutdownNotification>(*this,&TelemetryClient::OnSocketShutdown));
			Reactor_.removeEventHandler(*WS_,
										Poco::NObserver<TelemetryClient,
										Poco::Net::ErrorNotification>(*this,&TelemetryClient::OnSocketError));
			(*WS_).close();
			Socket_.shutdown();
		} else {
			if(WS_)
				(*WS_).close();
			Socket_.shutdown();
		}
	}

	bool TelemetryClient::Send(const std::string &Payload) {
		std::lock_guard Guard(Mutex_);
		auto BytesSent = WS_->sendFrame(Payload.c_str(),(int)Payload.size());
		return  BytesSent == Payload.size();
	}

	void TelemetryClient::SendTelemetryShutdown() {
		Logger_.information(Poco::format("TELEMETRY-SHUTDOWN(%s): Closing.",CId_));
		TelemetryStream()->DeRegisterClient(UUID_);
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
		DeviceRegistry()->SendFrame(SerialNumber_, OS.str());
		TelemetryStream()->DeRegisterClient(UUID_);
		delete this;
	}

	void TelemetryClient::OnSocketShutdown(const Poco::AutoPtr<Poco::Net::ShutdownNotification>& pNf) {
		std::lock_guard Guard(Mutex_);
		Logger_.information(Poco::format("SOCKET-SHUTDOWN(%s): Orderly shutdown.", CId_));
		SendTelemetryShutdown();
	}

	void TelemetryClient::OnSocketError(const Poco::AutoPtr<Poco::Net::ErrorNotification>& pNf) {
		std::lock_guard Guard(Mutex_);
		Logger_.information(Poco::format("SOCKET-ERROR(%s): Closing.",CId_));
		SendTelemetryShutdown();
	}

	void TelemetryClient::OnSocketReadable(const Poco::AutoPtr<Poco::Net::ReadableNotification>& pNf) {
		std::lock_guard Guard(Mutex_);
		try
		{
			ProcessIncomingFrame();
		}
		catch (const Poco::Exception & E)
		{
			Logger_.log(E);
			SendTelemetryShutdown();
		}
		catch (const std::exception & E) {
			std::string W = E.what();
			Logger_.information(Poco::format("std::exception caught: %s. Connection terminated with %s",W,CId_));
			SendTelemetryShutdown();
		}
		catch ( ... ) {
			Logger_.information(Poco::format("Unknown exception for %s. Connection terminated.",CId_));
			SendTelemetryShutdown();
		}
	}

	void TelemetryClient::ProcessIncomingFrame() {

		bool MustDisconnect=false;
		Poco::Buffer<char>			IncomingFrame(0);

		try {
			int Op,flags;
			int IncomingSize;
			IncomingSize = WS_->receiveFrame(IncomingFrame,flags);
			Op = flags & Poco::Net::WebSocket::FRAME_OP_BITMASK;

			if (IncomingSize == 0 && flags == 0 && Op == 0) {
				Logger_.information(Poco::format("DISCONNECT(%s): device has disconnected.", CId_));
				MustDisconnect = true;
			} else {
				if (Op == Poco::Net::WebSocket::FRAME_OP_PING) {
					Logger_.debug(Poco::format("WS-PING(%s): received. PONG sent back.", CId_));
					WS_->sendFrame("", 0,
								   (int)Poco::Net::WebSocket::FRAME_OP_PONG |
									   (int)Poco::Net::WebSocket::FRAME_FLAG_FIN);
				} else if (Op == Poco::Net::WebSocket::FRAME_OP_CLOSE) {
					Logger_.information(Poco::format("DISCONNECT(%s): device wants to disconnect.", CId_));
					MustDisconnect = true ;
				}
			}
		} catch (...) {
			MustDisconnect = true ;
		}

		if(!MustDisconnect)
			return;

		SendTelemetryShutdown();
	}


}