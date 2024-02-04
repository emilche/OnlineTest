#pragma once
#include "Core/Core.h"

struct SteamNetConnectionStatusChangedCallback_t;
class ISteamNetworkingSockets;
enum ESteamNetworkingSocketsDebugOutputType;
class Server
{
    using ClientID = uint32_t;
    //HSteamListenSocket
    uint32_t m_hListenSock = 0u;
    //HSteamNetPollGroup
    uint32_t m_hPollGroup = 0u;
    ISteamNetworkingSockets* m_pInterface = nullptr;

    std::map<ClientID, Core::User> m_connectedClients;
    

    // SERVER LOGIC

    
    void SendStringToClient(uint32_t conn, const char* str);
    void SendStringToAllClients(const char* str, uint32_t except);
    void PollIncomingMessages();
    void PollLocalUserInput();
    void SetClientNick(uint32_t hConn, const char* nick); // Place holder att ändra till ah action.
    void OnSteamNetConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* pInfo);
    static void ConnectionStatusChangedCallback(SteamNetConnectionStatusChangedCallback_t* info);
    void PollConnectionStateChanges();
public:
    void StartServer(uint16_t nPort);
};

