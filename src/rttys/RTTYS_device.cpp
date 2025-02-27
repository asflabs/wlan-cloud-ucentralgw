//
// Created by stephane bourque on 2021-11-23.
//

#include "RTTYS_device.h"
#include "rttys/RTTYS_server.h"

namespace OpenWifi {

	RTTY_Device_ConnectionHandler::RTTY_Device_ConnectionHandler(Poco::Net::StreamSocket& socket,
															 Poco::Net::SocketReactor & reactor):
		socket_(socket),
		reactor_(reactor)
	{
		reactor_.addEventHandler(socket_, Poco::NObserver<RTTY_Device_ConnectionHandler, Poco::Net::ReadableNotification>(*this, &RTTY_Device_ConnectionHandler::onSocketReadable));
		reactor_.addEventHandler(socket_, Poco::NObserver<RTTY_Device_ConnectionHandler, Poco::Net::ShutdownNotification>(*this, &RTTY_Device_ConnectionHandler::onSocketShutdown));
		// reactor_.addEventHandler(socket_, Poco::NObserver<RTTY_Device_ConnectionHandler, Poco::Net::WritableNotification>(*this, &RTTY_Device_ConnectionHandler::onSocketWritable));
	}

	RTTY_Device_ConnectionHandler::~RTTY_Device_ConnectionHandler()
	{
		reactor_.removeEventHandler(socket_, Poco::NObserver<RTTY_Device_ConnectionHandler, Poco::Net::ReadableNotification>(*this, &RTTY_Device_ConnectionHandler::onSocketReadable));
		// reactor_.removeEventHandler(socket_, Poco::NObserver<RTTY_Device_ConnectionHandler, Poco::Net::WritableNotification>(*this, &RTTY_Device_ConnectionHandler::onSocketWritable));
		reactor_.removeEventHandler(socket_, Poco::NObserver<RTTY_Device_ConnectionHandler, Poco::Net::ShutdownNotification>(*this, &RTTY_Device_ConnectionHandler::onSocketShutdown));
		socket_.close();
		if(!id_.empty()) {
			RTTYS_server()->DeRegister(id_, this);
			RTTYS_server()->Close(id_);
		} else {
			std::cout << "Device going down that never registered" << std::endl;
		}
	}

	std::string RTTY_Device_ConnectionHandler::SafeCopy( const u_char * buf, int MaxSize, int & NewPos) {
		std::string     S;
		while(NewPos<MaxSize && buf[NewPos]!=0) {
			S += buf[NewPos++];
		}

		if(buf[NewPos]==0)
			NewPos++;

		return S;
	}

	void RTTY_Device_ConnectionHandler::PrintBuf(const u_char * buf, int size) {

		std::cout << "======================================" << std::endl;
		while(size) {
			std::cout << std::hex << (int) *buf++ << " ";
			size--;
		}
		std::cout << std::endl;
		std::cout << "======================================" << std::endl;
	}

	int RTTY_Device_ConnectionHandler::SendMessage(RTTY_MSG_TYPE Type, const u_char * Buf, int BufLen) {
		u_char outBuf[ 256 ]{0};
		auto msg_len = BufLen + 1 ;
		auto total_len = msg_len + 3 ;
		outBuf[0] = Type;
		outBuf[1] = (msg_len >> 8);
		outBuf[2] = (msg_len & 0x00ff);
		outBuf[3] = sid_;
		std::memcpy(&outBuf[4], Buf, BufLen);
		// PrintBuf(outBuf,total_len);
		return socket_.sendBytes(outBuf,total_len) == total_len;
	}

	int RTTY_Device_ConnectionHandler::SendMessage(RTTY_MSG_TYPE Type, std::string &S ) {
		u_char outBuf[ 256 ]{0};
		auto msg_len = S.size()+1 ;
		auto total_len= msg_len+3;
		outBuf[0] = Type;
		outBuf[1] = (msg_len >> 8);
		outBuf[2] = (msg_len & 0x00ff);
		outBuf[3] = sid_;
		std::strcpy((char*)&outBuf[4],S.c_str());
		// PrintBuf(outBuf,total_len);
		return socket_.sendBytes(outBuf,total_len) == total_len;
	}

	int RTTY_Device_ConnectionHandler::SendMessage(RTTY_MSG_TYPE Type) {
		u_char outBuf[ 256 ]{0};
		auto msg_len = 0 ;
		auto total_len= msg_len+3+1;
		outBuf[0] = Type;
		outBuf[1] = (msg_len >> 8);
		outBuf[2] = (msg_len & 0x00ff);
		outBuf[3] = sid_;
		return socket_.sendBytes(outBuf,total_len) == total_len;
	}

	void RTTY_Device_ConnectionHandler::SendToClient(const u_char *Buf, int len) {
		auto Client = RTTYS_server()->GetClient(id_);
		if(Client!= nullptr)
			Client->SendData(Buf,len);
	}

	void RTTY_Device_ConnectionHandler::SendToClient(const std::string &S) {
		auto Client = RTTYS_server()->GetClient(id_);
		if(Client!= nullptr)
			Client->SendData(S);
	}

	void RTTY_Device_ConnectionHandler::KeyStrokes(const u_char *buf, int len) {
		u_char outBuf[16]{0};

		if(len>(sizeof(outBuf)-5))
			return;

		auto total_len = 3 + 1 + len-1;
		outBuf[0] = msgTypeTermData;
		outBuf[1] = 0 ;
		outBuf[2] = len +1-1;
		outBuf[3] = sid_;
		memcpy( &outBuf[4], &buf[1], len-1);
		socket_.sendBytes(outBuf, total_len);
		// PrintBuf(outBuf, total_len);
	}

	void RTTY_Device_ConnectionHandler::WindowSize(int cols, int rows) {
		u_char	outBuf[32]{0};
		outBuf[0] = msgTypeWinsize;
		outBuf[1] = 0 ;
		outBuf[2] = 4 + 1 ;
		outBuf[3] = sid_;
		outBuf[4] = cols >> 8 ;
		outBuf[5] = cols & 0x00ff;
		outBuf[6] = rows >> 8;
		outBuf[7] = rows & 0x00ff;
		// PrintBuf(outBuf,8);
		socket_.sendBytes(outBuf,8);
	}

	bool RTTY_Device_ConnectionHandler::Login() {
		u_char outBuf[8]{0};
		outBuf[0] = msgTypeLogin;
		outBuf[1] = 0;
		outBuf[2] = 0;
		// PrintBuf(outBuf,3);
		socket_.sendBytes(outBuf,3 );
		return true;
	}

	bool RTTY_Device_ConnectionHandler::Logout() {
		u_char outBuf[64];
		outBuf[0] = msgTypeLogout;
		outBuf[1] = 0;
		outBuf[2] = 1;
		outBuf[3] = sid_;
		RTTYS_server()->Logger().debug(Poco::format("Device %s logging out", id_));
		// PrintBuf(outBuf,4);
		socket_.sendBytes(outBuf,4 );
		return true;
	}

	void RTTY_Device_ConnectionHandler::onSocketReadable(const Poco::AutoPtr<Poco::Net::ReadableNotification>& pNf)
	{
		try
		{
			u_char	inBuf[16000]{0};
			int len = socket_.receiveBytes(&inBuf[0],sizeof(inBuf));
			// std::cout << "DEVICE MSG RECEIVED: " << std::dec << len << " bytes" << std::endl;
			if (len > 0) {
				// std::cout << "DEVICE MSG RECEIVED: " << std::dec << len << " bytes" << std::endl;
				// PrintBuf(inBuf,len);
				RTTY_MSG_TYPE   msg;
				if(inBuf[0]>=(u_char)msgTypeMax) {
					RTTYS_server()->Logger().debug(Poco::format("Bad message for Session: %s", id_));
					return delete this;
				}

				msg = (RTTY_MSG_TYPE) inBuf[0];
				int MsgLen = (int) inBuf[1] * 256 + (int) inBuf[2];

				if(MsgLen > sizeof(inBuf))
					return;

				switch(msg) {
					case msgTypeRegister: {
						id_ = std::string((char*)&inBuf[3]);
						desc_ = std::string((char*)&inBuf[3 + id_.size() + 1]);
						token_ = std::string((char*)&inBuf[3 + id_.size() + 1 + desc_.size() + 1]);

						if(RTTYS_server()->ValidEndPoint(id_,token_)) {
							if(!RTTYS_server()->AmIRegistered(id_,token_,this)) {
								u_char OutBuf[12];
								OutBuf[0] = msgTypeRegister;
								OutBuf[1] = 0;
								OutBuf[2] = 4;
								OutBuf[3] = 0;
								OutBuf[4] = 'O';
								OutBuf[5] = 'K';
								OutBuf[6] = 0;
								socket_.sendBytes(OutBuf, 7);
								RTTYS_server()->Register(id_, this);
								serial_ = RTTYS_server()->SerialNumber(id_);
								RTTYS_server()->Logger().debug(Poco::format(
										"Registration for SerialNumber: %s, Description: %s",
										serial_, desc_));
							} else {
								RTTYS_server()->Logger().debug(Poco::format(
									"Registration for SerialNumber: %s, already done",
									serial_));
							}
						} else {
							RTTYS_server()->Logger().debug(Poco::format(
								"Registration failed - invalid (id,token) pair. for Session: %s, Description: %s",
								id_, desc_));
							return delete this;
						}
					}
					break;

					case msgTypeLogin: {
					    RTTYS_server()->Logger().debug(Poco::format("Device created session for SerialNumber: %s, session: %s", serial_, id_));
						nlohmann::json doc;
						auto error = inBuf[3];
						sid_ = inBuf[4];
						doc["type"] = "login";
						doc["err"] = error;
						const auto login_msg = to_string(doc);
						SendToClient(login_msg);
					}
					break;

					case msgTypeLogout: {
						std::cout << "msgTypeLogout" << std::endl;
					}
					break;

					case msgTypeTermData: {
						SendToClient(&inBuf[3],MsgLen);
					}
					break;

					case msgTypeWinsize: {
						std::cout << "msgTypeWinsize" << std::endl;
					}
					break;

					case msgTypeCmd: {
						std::cout << "msgTypeCmd" << std::endl;
					}
					break;

					case msgTypeHeartbeat: {
						// std::cout << "msgTypeHeartbeat: " << MsgLen << " bytes" << std::endl;
						// PrintBuf(&inBuf[0], len);
						u_char MsgBuf[32]{0};
						MsgBuf[0] = msgTypeHeartbeat;
						socket_.sendBytes(MsgBuf,3);
					}
					break;

					case msgTypeFile: {
						std::cout << "msgTypeFile" << std::endl;
					}
					break;

					case msgTypeHttp: {
						std::cout << "msgTypeHttp" << std::endl;
					}
					break;

					case msgTypeAck: {
						std::cout << "msgTypeAck" << std::endl;
					}
					break;

					case msgTypeMax: {
						std::cout << "msgTypeMax" << std::endl;
					}
					break;
				}
			} else {
				RTTYS_server()->Logger().debug(Poco::format("DeRegistration: %s shutting down session %s.", serial_, id_));
				return delete this;
			}
		}
		catch (const Poco::Exception & E)
		{
			RTTYS_server()->Logger().debug(Poco::format("DeRegistration: %s exception, session %s.", serial_, id_));
			RTTYS_server()->Logger().log(E);
			return delete this;
		}
	}

	void RTTY_Device_ConnectionHandler::onSocketShutdown(const Poco::AutoPtr<Poco::Net::ShutdownNotification>& pNf)
	{
		std::cout << "Device " << id_ << " closing socket." << std::endl;
		delete this;
	}
}