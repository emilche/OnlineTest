#include "Core.h"
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
#include "yaml-cpp/yaml.h"

#include <iostream>
#include <fstream>
#include <filesystem>

namespace Core {

	/////////////////////////////////////////////////////////////////////////////
	//
	// Common stuff -- These are from the example_chat.cpp in the GNS Repo
	//
	/////////////////////////////////////////////////////////////////////////////
	std::mutex mutexUserInputQueue;
	std::queue< std::string > queueUserInput;

	std::thread* s_pThreadUserInput = nullptr;
	

	int64_t g_logTimeZero;
	

	// We do this because I won't want to figure out how to cleanly shut
	// down the thread that is reading from stdin.
	void NukeProcess(int rc)
	{
	#ifdef _WIN32
		ExitProcess(rc);
	#else
		(void)rc; // Unused formal parameter
		kill(getpid(), SIGKILL);
	#endif
	}

	void DebugOutput(ESteamNetworkingSocketsDebugOutputType eType, const char* pszMsg)
	{
		SteamNetworkingMicroseconds time = SteamNetworkingUtils()->GetLocalTimestamp() - g_logTimeZero;
		printf("%10.6f %s\n", time * 1e-6, pszMsg);
		fflush(stdout);
		if (eType == k_ESteamNetworkingSocketsDebugOutputType_Bug)
		{
			fflush(stdout);
			fflush(stderr);
			NukeProcess(1);
		}
	}

	void FatalError(const char* fmt, ...)
	{
		char text[2048];
		va_list ap;
		va_start(ap, fmt);
		vsprintf(text, fmt, ap);
		va_end(ap);
		char* nl = strchr(text, '\0') - 1;
		if (nl >= text && *nl == '\n')
			*nl = '\0';
		DebugOutput(k_ESteamNetworkingSocketsDebugOutputType_Bug, text);
}

	void Printf(const char* fmt, ...)
	{
		char text[2048];
		va_list ap;
		va_start(ap, fmt);
		vsprintf(text, fmt, ap);
		va_end(ap);
		char* nl = strchr(text, '\0') - 1;
		if (nl >= text && *nl == '\n')
			*nl = '\0';
		DebugOutput(k_ESteamNetworkingSocketsDebugOutputType_Msg, text);
	}
	void InitSteamDatagramConnectionSockets()
	{
#ifdef STEAMNETWORKINGSOCKETS_OPENSOURCE
		SteamDatagramErrMsg errMsg;
		if (!GameNetworkingSockets_Init(nullptr, errMsg))
			FatalError("GameNetworkingSockets_Init failed.  %s", errMsg);
#else
		SteamDatagram_SetAppID(570); // Just set something, doesn't matter what
		SteamDatagram_SetUniverse(false, k_EUniverseDev);

		SteamDatagramErrMsg errMsg;
		if (!SteamDatagramClient_Init(errMsg))
			FatalError("SteamDatagramClient_Init failed.  %s", errMsg);

		// Disable authentication when running with Steam, for this
		// example, since we're not a real app.
		//
		// Authentication is disabled automatically in the open-source
		// version since we don't have a trusted third party to issue
		// certs.
		SteamNetworkingUtils()->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_IP_AllowWithoutAuth, 1);
#endif

		g_logTimeZero = SteamNetworkingUtils()->GetLocalTimestamp();

		SteamNetworkingUtils()->SetDebugOutputFunction(k_ESteamNetworkingSocketsDebugOutputType_Msg, DebugOutput);
	}

	void ShutdownSteamDatagramConnectionSockets()
	{
		// Give connections time to finish up.  This is an application layer protocol
		// here, it's not TCP.  Note that if you have an application and you need to be
		// more sure about cleanup, you won't be able to do this.  You will need to send
		// a message and then either wait for the peer to close the connection, or
		// you can pool the connection to see if any reliable data is pending.
		std::this_thread::sleep_for(std::chrono::milliseconds(500));

#ifdef STEAMNETWORKINGSOCKETS_OPENSOURCE
		GameNetworkingSockets_Kill();
#else
		SteamDatagramClient_Kill();
#endif
	}

	void LocalUserInput_Init()
	{
		s_pThreadUserInput = new std::thread([]()
		{
			while (!g_bQuit)
			{
				char szLine[4000];
				if (!fgets(szLine, sizeof(szLine), stdin))
				{
					// Well, you would hope that you could close the handle
					// from the other thread to trigger this.  Nope.
					if (g_bQuit)
						return;
					g_bQuit = true;
					Printf("Failed to read on stdin, quitting\n");
					break;
				}

				mutexUserInputQueue.lock();
				queueUserInput.push(std::string(szLine));
				mutexUserInputQueue.unlock();
			}
		});
	}

	void LocalUserInput_Kill()
	{
		// Does not work.  We won't clean up, we'll just nuke the process.
		//	g_bQuit = true;
		//	_close( fileno( stdin ) );
		//
		//	if ( s_pThreadUserInput )
		//	{
		//		s_pThreadUserInput->join();
		//		delete s_pThreadUserInput;
		//		s_pThreadUserInput = nullptr;
		//	}
	}

	// You really gotta wonder what kind of pedantic garbage was
	// going through the minds of people who designed std::string
	// that they decided not to include trim.
	// https://stackoverflow.com/questions/216823/whats-the-best-way-to-trim-stdstring


	// Read the next line of input from stdin, if anything is available.
	bool LocalUserInput_GetNext(std::string& result)
	{
		bool got_input = false;
		mutexUserInputQueue.lock();
		while (!queueUserInput.empty() && !got_input)
		{
			result = queueUserInput.front();
			queueUserInput.pop();
			ltrim(result);
			rtrim(result);
			got_input = !result.empty(); // ignore blank lines
		}
		mutexUserInputQueue.unlock();
		return got_input;
	}

	

	

	


}