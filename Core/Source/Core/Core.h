#pragma once
#include <iostream>
#include <vector>
#include <unordered_map>
#include <mutex>
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

struct SteamNetConnectionStatusChangedCallback_t;
class ISteamNetworkingSockets;
enum ESteamNetworkingSocketsDebugOutputType;

namespace Core {
    
    struct Item {
        std::string name;
        std::string itemId;
        uint16_t quantity;
        // Other item properties
    };

    enum AuctionhouseInteractions
    {
        Posted,
        Bid,
        Buyout,
        Sold,
        Expired,
        Cancelled,
        Won
    };

    struct Listing {
        std::string itemId;
        std::string itemName;
        std::string sellerId;
        double price;
        double buyoutPrice;
        // Other listing properties
    };
    struct AuctionhouseEvents {
        std::string timestamp;
        std::string user;
        Listing listed_item;
        AuctionhouseInteractions ahEvent;


    };

    struct User {
        std::string userId;
        std::vector<Listing> listings;
        std::vector<Item> inventory;
        double balance = 0;
        // Other user properties
    };

    static bool g_bQuit = false;
    

    void NukeProcess(int rc);
    void DebugOutput(ESteamNetworkingSocketsDebugOutputType eType, const char* pszMsg);
    void FatalError(const char* fmt, ...);
    void Printf(const char* fmt, ...);
    void InitSteamDatagramConnectionSockets();
    void ShutdownSteamDatagramConnectionSockets();
    // trim from start (in place)
    static inline void ltrim(std::string& s) {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) {
            return !std::isspace(ch);
        }));
    }

    // trim from end (in place)
    static inline void rtrim(std::string& s) {
        s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) {
            return !std::isspace(ch);
        }).base(), s.end());
    }
    void LocalUserInput_Init();
    void LocalUserInput_Kill();
    bool LocalUserInput_GetNext(std::string& result);

    User GetUser(std::string username);
    


}