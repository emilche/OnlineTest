#include "Core/Client.h"
int main()
{
	uint16_t nPort = 8192;
	std::string adr = "127.0.0.1:8192";
	// Create client and server sockets
	Core::InitSteamDatagramConnectionSockets();
	Core::LocalUserInput_Init();
	Client client;
	client.StartClient(adr);

	Core::ShutdownSteamDatagramConnectionSockets();

	// Ug, why is there no simple solution for portable, non-blocking console user input?
	// Just nuke the process
	//LocalUserInput_Kill();
	Core::NukeProcess(0);
}