#pragma once
#include "Core/Core.h"


class ISteamNetworkingSockets;
class Client
{
private:

	uint32_t m_hConnection;
	ISteamNetworkingSockets* m_pInterface;

	void PollIncomingMessages();
	void PollLocalUserInput();
	void OnSteamNetConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* pInfo);
	static void SteamNetConnectionStatusChangedCallback(SteamNetConnectionStatusChangedCallback_t* pInfo);
	void PollConnectionStateChanges();
public:
	void StartClient(const std::string serverAddress);

};