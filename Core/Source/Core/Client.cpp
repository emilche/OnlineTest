#include "Client.h"
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <random>
#include <chrono>
#include <thread>
#include <mutex>
#include <queue>
#include <map>
#include <cctype>

#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>
#ifndef STEAMNETWORKINGSOCKETS_OPENSOURCE
#include <steam/steam_api.h>
#endif

#ifdef _WIN32
#include <windows.h> // Ug, for NukeProcess -- see below
#else
#include <unistd.h>
#include <signal.h>
#endif

#include <iostream>
static Client* s_pCallbackInstance = nullptr;
void Client::PollIncomingMessages()
{
	while (!Core::g_bQuit)
	{
		ISteamNetworkingMessage* pIncomingMsg = nullptr;
		int numMsgs = m_pInterface->ReceiveMessagesOnConnection(m_hConnection, &pIncomingMsg, 1);
		if (numMsgs == 0)
			break;
		if (numMsgs < 0)
			Core::FatalError("Error checking for messages");

		// Just echo anything we get from the server
		fwrite(pIncomingMsg->m_pData, 1, pIncomingMsg->m_cbSize, stdout);
		fputc('\n', stdout);

		// We don't need this anymore.
		pIncomingMsg->Release();
	}
}

void Client::PollLocalUserInput()
{
	std::string cmd;
	while (!Core::g_bQuit && Core::LocalUserInput_GetNext(cmd))
	{

		// Check for known commands
		if (strcmp(cmd.c_str(), "/quit") == 0)
		{
			Core::g_bQuit = true;
			Core::Printf("Disconnecting from chat server");

			// Close the connection gracefully.
			// We use linger mode to ask for any remaining reliable data
			// to be flushed out.  But remember this is an application
			// protocol on UDP.  See ShutdownSteamDatagramConnectionSockets
			m_pInterface->CloseConnection(m_hConnection, 0, "Goodbye", true);
			break;
		}

		// Anything else, just send it to the server and let them parse it
		m_pInterface->SendMessageToConnection(m_hConnection, cmd.c_str(), (uint32)cmd.length(), k_nSteamNetworkingSend_Reliable, nullptr);
	}
}

void Client::OnSteamNetConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* pInfo)
{
	assert(pInfo->m_hConn == m_hConnection || m_hConnection == k_HSteamNetConnection_Invalid);

	// What's the state of the connection?
	switch (pInfo->m_info.m_eState)
	{
	case k_ESteamNetworkingConnectionState_None:
		// NOTE: We will get callbacks here when we destroy connections.  You can ignore these.
		break;

	case k_ESteamNetworkingConnectionState_ClosedByPeer:
	case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
	{
		Core::g_bQuit = true;

		// Print an appropriate message
		if (pInfo->m_eOldState == k_ESteamNetworkingConnectionState_Connecting)
		{
			// Note: we could distinguish between a timeout, a rejected connection,
			// or some other transport problem.
			Core::Printf("We sought the remote host, yet our efforts were met with defeat.  (%s)", pInfo->m_info.m_szEndDebug);
		}
		else if (pInfo->m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally)
		{
			Core::Printf("Alas, troubles beset us; we have lost contact with the host.  (%s)", pInfo->m_info.m_szEndDebug);
		}
		else
		{
			// NOTE: We could check the reason code for a normal disconnection
			Core::Printf("The host hath bidden us farewell.  (%s)", pInfo->m_info.m_szEndDebug);
		}

		// Clean up the connection.  This is important!
		// The connection is "closed" in the network sense, but
		// it has not been destroyed.  We must close it on our end, too
		// to finish up.  The reason information do not matter in this case,
		// and we cannot linger because it's already closed on the other end,
		// so we just pass 0's.
		m_pInterface->CloseConnection(pInfo->m_hConn, 0, nullptr, false);
		m_hConnection = k_HSteamNetConnection_Invalid;
		break;
	}

	case k_ESteamNetworkingConnectionState_Connecting:
		// We will get this callback when we start connecting.
		// We can ignore this.
		break;

	case k_ESteamNetworkingConnectionState_Connected:
		Core::Printf("Connected to server OK");
		break;

	default:
		// Silences -Wswitch
		break;
	}
}
void Client::SteamNetConnectionStatusChangedCallback(SteamNetConnectionStatusChangedCallback_t* pInfo)
{
	s_pCallbackInstance->OnSteamNetConnectionStatusChanged(pInfo);
}

void Client::PollConnectionStateChanges()
{
	s_pCallbackInstance = this;
	m_pInterface->RunCallbacks();
}

void Client::StartClient(const std::string serverAddress)
{
	std::string nickname;
	std::cout << "Please enter a nickname: ";
	std::cin >> nickname;
	nickname = "/nick " + nickname;
	s_pCallbackInstance = this;
	// Select instance to use.  For now we'll always use the default.
	m_pInterface = SteamNetworkingSockets();
	// Start connecting
	SteamNetworkingIPAddr address;
	if (!address.ParseString(serverAddress.c_str()))
	{
		Core::FatalError("Invalid IP address - could not parse %s", serverAddress);
		return;
	}
	Core::Printf(std::format("Connecting to chat server {}", serverAddress).c_str());
	SteamNetworkingConfigValue_t opt;
	opt.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, (void*)SteamNetConnectionStatusChangedCallback);
	m_hConnection = m_pInterface->ConnectByIPAddress(address, 1, &opt);
	if (m_hConnection == k_HSteamNetConnection_Invalid)
		Core::FatalError("Failed to create connection");
	m_pInterface->SendMessageToConnection(m_hConnection, nickname.c_str(), (uint32)nickname.length(), k_nSteamNetworkingSend_Reliable, nullptr);

	while (!Core::g_bQuit)
	{
		PollIncomingMessages();
		PollConnectionStateChanges();
		PollLocalUserInput();
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
}
