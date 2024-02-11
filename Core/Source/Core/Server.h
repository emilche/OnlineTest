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
    std::filesystem::path m_ListingsDatabaseFilePath;
    // Define a vector of random item names
    std::vector<std::string> random_names{
        "Axe of doom", "Wool", "Copper Sword",
        "Silver Sword", "Bow", "Crossbow",
        "Leather", "Healing Potion", "Mana Potion",
        "Mail Helmet", "Cap", "Boots",
        "Gloves", "Cloak", "Amulet"
    };
    // SERVER LOGIC

    //Send server to client info
    void SendStringToClient(uint32_t conn, const char* str);
    void SendStringToAllClients(const char* str, uint32_t except);
    void OnSteamNetConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* pInfo);
    static void ConnectionStatusChangedCallback(SteamNetConnectionStatusChangedCallback_t* pInfo);
    void PollConnectionStateChanges();

    //Client to server
    void PollIncomingMessages();
    void PollLocalUserInput();
    void SetClientNick(uint32_t hConn, const std::string& nick);
    
    //Event handlers
    void HandleBid(std::map<ClientID, Core::User>::iterator itClient, std::string sCmd);
    void HandlePost(std::map<ClientID, Core::User>::iterator itClient, std::string sCmd);
    void HandleBuyout(std::map<ClientID, Core::User>::iterator itClient, std::string sCmd);
    void HandleMe(std::map<ClientID, Core::User>::iterator itClient);
    void SendCommandsList(uint32_t hConn);

    //Server utility functions
    Core::User GetUser(std::string username);
    bool ToPlayerJson(const Core::User user);
    bool ToListingsJson();
    void LoadListings();
    void SendListings(uint32_t hConn);
    std::string ListTimeToString();
    void CheckforExpiredListings();
    void RemoveListing(Core::Listing listing);
    void PostRandomListing();
    void CreateFileIfNotExists(std::filesystem::path path, std::string name);

public:
    void StartServer(const std::string address);
};

