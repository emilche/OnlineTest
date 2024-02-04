#include "Core/Server.h"

int main()
{;
	uint16_t nPort = 8192;

	// Create client and server sockets
	Core::InitSteamDatagramConnectionSockets();
	Core::LocalUserInput_Init();
	Server server;
	server.StartServer("127.0.0.1:8192");

	Core::ShutdownSteamDatagramConnectionSockets();

	// Ug, why is there no simple solution for portable, non-blocking console user input?
	// Just nuke the process
	//LocalUserInput_Kill();
	Core::NukeProcess(0);
}