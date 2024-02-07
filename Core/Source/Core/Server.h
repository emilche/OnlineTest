#pragma once
#include "Core/Core.h"
#include <iostream>
#include <vector>
#include <fstream>
#include <filesystem>

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
    std::vector<Core::Listing> listings;
    std::vector<Core::AuctionhouseEvents> ahEvents;
    std::filesystem::path m_EventHistoryFilePath;
    std::filesystem::path m_PlayerDatabaseFilePath;
    // SERVER LOGIC


    void SendStringToClient(uint32_t conn, const char* str);
    void SendStringToAllClients(const char* str, uint32_t except);
    void PollIncomingMessages();
    void PollLocalUserInput();
    void SetClientNick(uint32_t hConn, const char* nick); // Place holder att ändra till ah action.
    void OnSteamNetConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* pInfo);
    static void ConnectionStatusChangedCallback(SteamNetConnectionStatusChangedCallback_t* pInfo);
    void PollConnectionStateChanges();
    Core::User GetUser(std::string username);
    bool ToPlayerJson(Core::User user);
    void SendListings(uint32_t hConn);
    std::string ListTimeToString();
    void CheckforExpiredListings();
    void RemoveListing(Core::Listing);
public:
    void StartServer(const std::string address);
};

