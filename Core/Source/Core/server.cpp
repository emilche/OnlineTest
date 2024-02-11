#include "Server.h"
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
#include <limits>

#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>
#ifndef STEAMNETWORKINGSOCKETS_OPENSOURCE
#include <steam/steam_api.h>
#endif

#ifdef _WIN32
#include <windows.h> // Ug, for NukeProcess -- see below
#undef max
#else
#include <unistd.h>
#include <signal.h>
#endif
#include "json.hpp"

#include <iostream>
using json = nlohmann::json;
static Server* s_pCallbackInstance = nullptr;

void Server::SendStringToClient(uint32_t conn, const char* str)
{
	m_pInterface->SendMessageToConnection((HSteamNetConnection)conn, str, (uint32)strlen(str), k_nSteamNetworkingSend_Reliable, nullptr);
}

void Server::SendStringToAllClients(const char* str, uint32_t except = k_HSteamNetConnection_Invalid)
{
	for (auto& c : m_connectedClients)
	{
		if (c.first != except)
			SendStringToClient(c.first, str);
	}
}

void Server::HandlePost(std::map<ClientID, Core::User>::iterator itClient, std::string sCmd)
{
    char _temp[1024];

    // Extract the command arguments
    std::vector<std::string> words;
    std::istringstream iss(sCmd);
    std::string word;
    while (iss >> word) {
        words.emplace_back(std::move(word));
    }

    // Check if the command is malformed
    if (words.size() != 4) {
        snprintf(_temp, sizeof(_temp), "Your post request was malformed and not processed. Make sure to say: /post INVENTORYINDEX PRICE BUYOUT");
        SendStringToClient(itClient->first, _temp);
        return;
    }

    // Parse the command arguments
    int inventoryIndex;
    int price;
    int buyout;
    try {
        inventoryIndex = std::stoi(words[1]) - 1;
        price = std::stoi(words[2]);
        buyout = std::stoi(words[3]);
    }
    catch (const std::exception& e) {
        snprintf(_temp, sizeof(_temp), "Your post request was malformed and not processed. Make sure to say: /post INVENTORYINDEX PRICE BUYOUT");
        SendStringToClient(itClient->first, _temp);
        return;
    }

    // Check if the inventory index is valid
    try {
        itClient->second.inventory.at(inventoryIndex);
    } catch (const std::out_of_range& e) {
        snprintf(_temp, sizeof(_temp), "Your post request was malformed and not processed. The inventory index was not a valid item");
        SendStringToClient(itClient->first, _temp);
        return;
    }

    // Create a new listing
    Core::Listing list;
    Core::Item& item = itClient->second.inventory.at(inventoryIndex);
    list.item.itemId = item.itemId;
    list.item.name = item.name;
    list.sellerId = itClient->second.userId;
    list.price = price;
    list.buyoutPrice = buyout;
    list.ends = ListTimeToString();

    // Send success message to the client
    snprintf(_temp, sizeof(_temp), "You posted %s for auction with the starting price of: %d and the buyout price %d", item.name.c_str(), price, buyout);
    SendStringToClient(itClient->first, _temp);

    // Remove the item from the client's inventory
    itClient->second.inventory.erase(itClient->second.inventory.begin() + inventoryIndex);

    // Save the updated client data
    ToPlayerJson(itClient->second);

    // Add the new listing to the server's listings
    listings.emplace_back(std::move(list));

    // Save the updated listings data
    ToListingsJson();
}

void Server::HandleBid(std::map<ClientID, Core::User>::iterator itClient, std::string sCmd)
{
    // Extract the bid amount and listing index from the bid request command string
    std::string bid = sCmd.substr(4);
    std::istringstream iss(sCmd);

    // Read words from the input stream
    std::vector<std::string> words(std::istream_iterator<std::string>{iss}, std::istream_iterator<std::string>{});

    // Check if the bid request is well-formed and has the correct number of arguments
    if (words.size() != 3)
    {
        std::string errorMessage = "Your bid request was malformed and not processed. Make sure to say: /bid LISTINGINDEX AMOUNT";
        SendStringToClient(itClient->first, errorMessage.c_str());
        return;
    }

    // Convert the extracted bid amount and listing index to integers
    int listingIndex;
    int bidAmount;
    try {
        listingIndex = std::stoi(words[1]) - 1;
        bidAmount = std::stoi(words[2]);
    }
    catch (const std::exception& e) {
        std::string errorMessage = "Your bid request was malformed and not processed. Make sure to say: /bid LISTINGINDEX AMOUNT";
        SendStringToClient(itClient->first, errorMessage.c_str());
        return;
    }

    // Validate the bid amount against the client's balance and the current listing price
    if (bidAmount > itClient->second.balance)
    {
        std::string errorMessage = "Insufficient funds. You only have " + std::to_string(itClient->second.balance) + " G";
        SendStringToClient(itClient->first, errorMessage.c_str());
        return;
    }

    if (listingIndex < 0 || listingIndex >= listings.size())
    {
        std::string errorMessage = "Invalid Listing";
        SendStringToClient(itClient->first, errorMessage.c_str());
        return;
    }

    if (listings[listingIndex].price >= bidAmount)
    {
        std::string errorMessage = "Your bid was too low and won't be accepted";
        SendStringToClient(itClient->first, errorMessage.c_str());
        return;
    }

    // Update previous bidder's balance and send appropriate messages
    if (listings[listingIndex].bidder != "None")
    {
        bool isPrevBidderOnline = false;
        for (auto& client : m_connectedClients)
        {
            if (client.second.userId == listings[listingIndex].bidder)
            {
                client.second.balance += listings[listingIndex].price;
                std::string message = std::format("Someone made a higher bid on {}.{}",
                    listingIndex + 1, listings[listingIndex].item.name
                );
                SendStringToClient(client.first, message.c_str());
                isPrevBidderOnline = true;
                ToPlayerJson(client.second);
                break;
            }
        }
        if (!isPrevBidderOnline)
        {
            Core::User prevBidder = GetUser(listings[listingIndex].bidder);
            prevBidder.balance += listings[listingIndex].price;
            prevBidder.bufferMessages.emplace_back(std::format("Someone made a higher bid on {}.{}",
                listingIndex + 1, listings[listingIndex].item.name
            ));
            ToPlayerJson(prevBidder);
        }
    }

    // Update the listing's price and bidder, deduct the bid amount from the client's balance
    listings[listingIndex].price = bidAmount;
    listings[listingIndex].bidder = itClient->second.userId;
    itClient->second.balance -= bidAmount;
    ToPlayerJson(itClient->second);

    // Send confirmation message to the client
    std::string confirmationMessage = std::format("You bid on {}.{} the new price is now {}",
        listingIndex + 1, listings[listingIndex].item.name,
        listings[listingIndex].price
    );
    SendStringToClient(itClient->first, confirmationMessage.c_str());

    // Send message to all connected clients about the bid
    std::string broadcastMessage = std::format("{} Made a bid on {}.{} the new price is now {}",
        itClient->second.userId,
        listingIndex + 1, listings[listingIndex].item.name,
        listings[listingIndex].price
    );
    ToListingsJson();
    SendStringToAllClients(broadcastMessage.c_str(), itClient->first);
    return;
}

void Server::HandleBuyout(std::map<ClientID, Core::User>::iterator itClient, std::string sCmd)
{
    // Extract the listing index from the buyout command
    std::string listingIndexStr = sCmd.substr(8);
    int listingIndex = 0;

    try {
        // Convert the listing index string to an integer
        listingIndex = std::stoi(listingIndexStr) - 1;
    }
    catch (const std::exception& e) {
        // Handle invalid listing index
        std::string errorMessage = "Your buyout request was malformed and not processed. Make sure to say: /buyout LISTINGINDEX";
        SendStringToClient(itClient->first, errorMessage.c_str());
        return;
    }

    // Check the validity of the listing index
    if (listingIndex < 0 || listingIndex >= listings.size()) {
        std::string errorMessage = "Invalid Listing";
        SendStringToClient(itClient->first, errorMessage.c_str());
        return;
    }

    // Check if the client has sufficient funds to make the buyout
    if (itClient->second.balance < listings[listingIndex].buyoutPrice) {
        std::string errorMessage = "Insufficient funds";
        SendStringToClient(itClient->first, errorMessage.c_str());
        return;
    }

    // Handle the case where the listing has a previous bidder
    if (listings[listingIndex].bidder != "None") {
        bool isPrevBidderOnline = false;

        // Check if the previous bidder is online
        for (auto& client : m_connectedClients) {
            if (client.second.userId == listings[listingIndex].bidder) {
                // Increase the previous bidder's balance and notify them of the higher bid
                client.second.balance += listings[listingIndex].price;
                std::string message = std::format("Someone made a higher bid on {}.{}",
                    listingIndex + 1, listings[listingIndex].item.name);
                SendStringToClient(client.first, message.c_str());
                isPrevBidderOnline = true;
                ToPlayerJson(client.second);
                break;
            }
        }

        // If the previous bidder is not online, update their balance and add a message to their buffer
        if (!isPrevBidderOnline) {
            Core::User prevBidder = GetUser(listings[listingIndex].bidder);
            prevBidder.balance += listings[listingIndex].price;
            std::string message = std::format("Someone made a higher bid on {}.{}",
                listingIndex + 1, listings[listingIndex].item.name);
            prevBidder.bufferMessages.emplace_back(message);
            ToPlayerJson(prevBidder);
        }
    }

    // Update the client's balance and the listing's bidder and price
    itClient->second.balance -= listings[listingIndex].buyoutPrice;
    listings[listingIndex].bidder = itClient->second.userId;
    listings[listingIndex].price = listings[listingIndex].buyoutPrice;

    // Remove the listing from the server's list
    RemoveListing(listings[listingIndex]);
}

void Server::HandleMe(std::map<ClientID, Core::User>::iterator itClient)
{
    std::stringstream inventory_string;
    std::stringstream listings_string;
    inventory_string.str("");
    listings_string.str("");
    for (size_t i = 0; i < itClient->second.inventory.size(); i++)
    {
        inventory_string << std::format("{}. {} \n", i + 1, itClient->second.inventory[i].name);
    }
    for (size_t i = 0; i < itClient->second.listings.size(); i++)
    {
        listings_string << std::format("{}. {}. Current bid: {}. Made by: {}. Buyout price: {}. Ends: {} \n",
            i,
            itClient->second.listings[i].item.name,
            itClient->second.listings[i].price,
            itClient->second.listings[i].bidder,
            itClient->second.listings[i].buyoutPrice,
            itClient->second.listings[i].ends
        );
    }
    std::string message = std::format("You are: {}.\nYour account balance: {}G.\nYour inventory:\n{}You have these auctions listed:\n{}",
        itClient->second.userId,
        itClient->second.balance,
        inventory_string.str(),
        listings_string.str()
    );
    SendStringToClient(itClient->first, message.c_str());
}

void Server::SendCommandsList(uint32_t hConn)
{
    std::string commands =
        "/bid LISTINGINDEX AMOUNT - Place a bid on an item.\n"
        "/post INVENTORYINDEX PRICE BUYOUT - Post an item for auction.\n"
        "/buyout LISTINGINDEX - Buyout an item immediately.\n"
        "/listings - View all current listings.\n"
        "/me - View your account details.\n";

    SendStringToClient(hConn, commands.c_str());
}

void Server::PostRandomListing()
{
    // Check if the listings vector is empty
    if (listings.empty())
    {

        // Initialize random device and mt19937 once
        static std::random_device rd;
        static std::mt19937 gen(rd());

        // Generate a random index to select a random item name from the vector
        std::uniform_int_distribution<int> distribution(0, random_names.size() - 1);
        int random_index = distribution(gen);

        // Create a new listing
        Core::Listing list;

        // Generate a random item ID using UUIDs
        std::uniform_int_distribution<int> id_distribution(0, std::numeric_limits<int>::max());
        list.item.itemId = std::to_string(id_distribution(gen));

        // Set the item name, seller ID, price, buyout price, and ends time for the listing
        list.item.name = random_names[random_index];
        list.sellerId = "The State";

        std::uniform_int_distribution<int> price_distribution(100, 1000);
        list.price = price_distribution(gen);
        list.buyoutPrice = list.price * 3;
        list.ends = ListTimeToString();

        // Add the listing to the listings vector
        listings.emplace_back(list);

        // Update the listings database file
        ToListingsJson();
    }
}

void Server::PollIncomingMessages()
{
    while (!Core::g_bQuit)
    {
        ISteamNetworkingMessage* pIncomingMsg = nullptr;
        int numMsgs = m_pInterface->ReceiveMessagesOnPollGroup(m_hPollGroup, &pIncomingMsg, 1);

        if (numMsgs == 0)
            break;

        if (numMsgs < 0)
            Core::FatalError("Error checking for messages");

        if (numMsgs != 1 || !pIncomingMsg)
        {
            Core::FatalError("Invalid message");
            return;
        }

        auto itClient = m_connectedClients.find(pIncomingMsg->m_conn);
        if (itClient == m_connectedClients.end())
        {
            Core::FatalError("Invalid client");
            return;
        }

        std::string sCmd((const char*)pIncomingMsg->m_pData, pIncomingMsg->m_cbSize);
        pIncomingMsg->Release();

        std::cout << std::format("[{}]: {}", itClient->second.userId.c_str(), sCmd.c_str()) << std::endl;

        if (sCmd.compare(0, 5, "/nick") == 0)
        {
            if (itClient->second.userId.empty())
            {
                std::string nick = sCmd.substr(5);
                nick.erase(0, nick.find_first_not_of(" \t\n\r\f\v"));
                SetClientNick(itClient->first, nick.c_str());
                continue;
            }
        }
        else if (sCmd.compare(0, 4, "/bid") == 0)
        {
            HandleBid(itClient, sCmd);
            continue;
        }
        else if (sCmd.compare(0, 9, "/listings") == 0)
        {
            SendListings(itClient->first);
            continue;
        }
        else if (sCmd.compare(0, 3, "/me") == 0)
        {
            HandleMe(itClient);
            continue;
        }
        else if (sCmd.compare(0, 5, "/post") == 0)
        {
            HandlePost(itClient, sCmd);
            continue;
        }
        else if (sCmd.compare(0, 7, "/buyout") == 0)
        {
            HandleBuyout(itClient, sCmd);
            continue;
        }
        else if (sCmd.compare(0, 5, "/help") == 0)
        {
            SendCommandsList(itClient->first);
            continue;
        }

        std::string chatMessage = itClient->second.userId + ": " + sCmd;
        SendStringToAllClients(chatMessage.c_str(), itClient->first);
    }
}

void Server::PollLocalUserInput()
{
    std::string cmd;
    const std::string QUIT_COMMAND = "/quit";
    while (!Core::g_bQuit && Core::LocalUserInput_GetNext(cmd))
    {
        if (cmd == QUIT_COMMAND)
        {
            ToListingsJson();
            Core::g_bQuit = true;
            Core::Printf("Shutting down server");
            break;
        }

        // That's the only command we support
        Core::Printf("The server only knows one command: '%s'", QUIT_COMMAND.c_str());
    }
}

void Server::SetClientNick(uint32_t hConn, const std::string& nick)
{
    // Create a new entry in the connectedClients map with the client's connection ID as the key and a new User object with the given nickname
    m_connectedClients[hConn] = GetUser(nick);

    // Set the connection name of the client using the SteamNetworkingSockets interface
    m_pInterface->SetConnectionName(hConn, nick.c_str());

    // Send a welcome message to the client
    std::string welcomeMessage = "Welcome " + nick + " Type /help if you need a list of the commands";
    SendStringToClient(hConn, welcomeMessage.c_str());

    // Send a list of everybody who is already connected
    if (m_connectedClients.empty())
    {
        SendStringToClient(hConn, "No one else is online.");
    }
    else
    {
        std::string onlinePlayersMessage = "There are " + std::to_string(m_connectedClients.size()) + " players online:";
        SendStringToClient(hConn, onlinePlayersMessage.c_str());
        for (const auto& c : m_connectedClients)
        {
            SendStringToClient(hConn, c.second.userId.c_str());
        }
    }

    // Send a list of the available items to the client
    SendStringToClient(hConn, "The listed items are: ");
    SendListings(hConn);

    // Send any buffered messages to the client
    if (!m_connectedClients[hConn].bufferMessages.empty())
    {
        SendStringToClient(hConn, "This happened when you were offline: ");
        for (const std::string& message : m_connectedClients[hConn].bufferMessages)
        {
            SendStringToClient(hConn, message.c_str());
        }
    }

    // Clear the buffered messages for the client
    m_connectedClients[hConn].bufferMessages.clear();

    // Convert the client's information to JSON format
    ToPlayerJson(m_connectedClients[hConn]);

    // Send a message to all other connected clients indicating that a new client has connected
    std::string connectedMessage = "'" + nick + "' has connected";
    SendStringToAllClients(connectedMessage.c_str(), hConn);
}

void Server::OnSteamNetConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* pInfo)
{
    char temp[1024];

    // What's the state of the connection?
    switch (pInfo->m_info.m_eState)
    {
    case k_ESteamNetworkingConnectionState_None:
        // NOTE: We will get callbacks here when we destroy connections.  You can ignore these.
        break;

    case k_ESteamNetworkingConnectionState_ClosedByPeer:
    case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
    {
        // Ignore if they were not previously connected.  (If they disconnected
        // before we accepted the connection.)
        if (pInfo->m_eOldState == k_ESteamNetworkingConnectionState_Connected)
        {

            // Locate the client.  Note that it should have been found, because this
            // is the only codepath where we remove clients (except on shutdown),
            // and connection change callbacks are dispatched in queue order.
            auto itClient = m_connectedClients.find(pInfo->m_hConn);
            if (itClient == m_connectedClients.end()) {
                // Handle error: client not found
                return;
            }

            // Select appropriate log messages
            const char* pszDebugLogAction;
            if (pInfo->m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally)
            {
                pszDebugLogAction = "problem detected locally";
                snprintf(temp, sizeof(temp), "%s ran into an issue and has disconnected.  (%s)", itClient->second.userId.c_str(), pInfo->m_info.m_szEndDebug);
            }
            else
            {
                // Note that here we could check the reason code to see if
                // it was a "usual" connection or an "unusual" one.
                pszDebugLogAction = "closed by peer";
                snprintf(temp, sizeof(temp), "%s left", itClient->second.userId.c_str());
            }

            // Spew something to our own log.  Note that because we put their nick
            // as the connection description, it will show up, along with their
            // transport-specific data (e.g. their IP address)
            Core::Printf("Connection %s %s, reason %d: %s\n",
                pInfo->m_info.m_szConnectionDescription,
                pszDebugLogAction,
                pInfo->m_info.m_eEndReason,
                pInfo->m_info.m_szEndDebug
            );

            m_connectedClients.erase(itClient);

            // Send a message so everybody else knows what happened
            SendStringToAllClients(temp);
        }
        else
        {
            if (pInfo->m_eOldState != k_ESteamNetworkingConnectionState_Connecting) {
                // Handle error: invalid old state
                return;
            }
        }

        // Clean up the connection.  This is important!
        // The connection is "closed" in the network sense, but
        // it has not been destroyed.  We must close it on our end, too
        // to finish up.  The reason information do not matter in this case,
        // and we cannot linger because it's already closed on the other end,
        // so we just pass 0's.
        m_pInterface->CloseConnection(pInfo->m_hConn, 0, nullptr, false);
        break;
    }

    case k_ESteamNetworkingConnectionState_Connecting:
    {
        // This must be a new connection
        if (m_connectedClients.find(pInfo->m_hConn) != m_connectedClients.end()) {
            // Handle error: client already exists
            return;
        }

        Core::Printf("Connection request from %s", pInfo->m_info.m_szConnectionDescription);

        // A client is attempting to connect
        // Try to accept the connection.
        if (m_pInterface->AcceptConnection(pInfo->m_hConn) != k_EResultOK)
        {
            // This could fail.  If the remote host tried to connect, but then
            // disconnected, the connection may already be half closed.  Just
            // destroy whatever we have on our side.
            m_pInterface->CloseConnection(pInfo->m_hConn, 0, nullptr, false);
            Core::Printf("Can't accept connection.  (It was already closed?)");
            break;
        }

        // Assign the poll group
        if (!m_pInterface->SetConnectionPollGroup(pInfo->m_hConn, m_hPollGroup))
        {
            m_pInterface->CloseConnection(pInfo->m_hConn, 0, nullptr, false);
            Core::Printf("Failed to set poll group?");
            break;
        }

        m_connectedClients[(uint32)pInfo->m_hConn];
        break;
    }

    case k_ESteamNetworkingConnectionState_Connected:
        break;

    default:
        // Silences -Wswitch
        break;
    }
}

void Server::PollConnectionStateChanges()
{
	s_pCallbackInstance = this;
	m_pInterface->RunCallbacks();
}

void Server::ConnectionStatusChangedCallback(SteamNetConnectionStatusChangedCallback_t* info) 
{ 
	if(info)
		s_pCallbackInstance->OnSteamNetConnectionStatusChanged(info);
}

void Server::CreateFileIfNotExists(std::filesystem::path path, std::string name)
{
	if (!std::filesystem::exists(path))
	{
		json new_j;
		new_j["Events"] = {};
		// Write JSON data to a file
		std::ofstream outFile(path);
		if (!outFile.is_open()) {
			std::cerr << std::format( "Failed to open {}.json for writing.", name) << std::endl;
		}
		outFile << std::setw(4) << new_j << std::endl; // Pretty-print JSON
		outFile.close();
	}
}

Core::User Server::GetUser(std::string username)
{
    json players;
    try
    {
        std::ifstream f(m_PlayerDatabaseFilePath.string());
        players = json::parse(f);
    }
    catch (json::exception e)
    {
        std::cout << "[ERROR] Failed to load player database " << m_PlayerDatabaseFilePath << std::endl << e.what() << std::endl;
        return Core::User();
    }

    Core::User temp_user = Core::User();
    temp_user.userId = username;
    if (!players["Users"].contains(username))
    {
        temp_user.balance = 1000;
        ToPlayerJson(temp_user);
        return temp_user;
    }
    temp_user.balance = players["Users"][username]["balance"];
    for (auto& [key, value] : players["Users"][username]["inventory"].items())
    {
        Core::Item temp_item = Core::Item();
        temp_item.itemId = key;
        temp_item.name = value["name"];
        temp_user.inventory.emplace_back(temp_item);
    }
    for (auto& [key, value] : players["Users"][username]["listings"].items())
    {
        Core::Listing temp_listing = Core::Listing();
        temp_listing.item.itemId = key;
        temp_listing.item.name = value["itemname"];
        temp_listing.bidder = value["bidder"];
        temp_listing.sellerId = value["seller"];
        temp_listing.price = value["price"];
        temp_listing.buyoutPrice = value["buyoutprice"];
        temp_user.listings.emplace_back(temp_listing);
    }
    temp_user.bufferMessages = players["Users"][username]["buffer"];
    return temp_user;
}

void Server::SendListings(uint32_t hConn)
{
    for (size_t i = 0; i < listings.size(); i++)
    {
        SendStringToClient(
            (uint32)hConn,
            std::format("{}. {}. Current bid: {}. Buyout price: {}. Current bid by: {}. Seller: {}. Ends at {}",
                i + 1,
                listings[i].item.name,
                listings[i].price,
                listings[i].buyoutPrice,
                listings[i].bidder,
                listings[i].sellerId,
                listings[i].ends
            ).c_str()
        );
    }
}

std::string Server::ListTimeToString()
{
    auto currentTime = std::chrono::system_clock::now();
    auto eightHoursLater = currentTime + std::chrono::hours(8);
    std::time_t timeT = std::chrono::system_clock::to_time_t(eightHoursLater);
    std::tm* localTime = std::localtime(&timeT);
    std::ostringstream oss;
    oss << std::put_time(localTime, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

void Server::CheckforExpiredListings()
{
    auto currentTime = std::chrono::system_clock::now();
    std::time_t timeT = std::chrono::system_clock::to_time_t(currentTime);
    std::tm* _localTime = std::localtime(&timeT);
    std::vector<Core::Listing> expired_listings;
    _localTime->tm_hour;
    for (Core::Listing listing : listings)
    {

        std::tm timeInfo = {};
        std::istringstream iss(listing.ends);
        iss >> std::get_time(&timeInfo, "%Y-%m-%d %H:%M:%S");
        std::chrono::system_clock::time_point endpoint = std::chrono::system_clock::from_time_t(std::mktime(&timeInfo));
        if (currentTime > endpoint)
        {
            expired_listings.emplace_back(listing);
        }
    }
    for (Core::Listing listing : expired_listings)
    {
        RemoveListing(listing);
    }
    PostRandomListing();
}

void Server::LoadListings()
{
    json j_listings;
    try
    {
        std::ifstream f(m_ListingsDatabaseFilePath.string());
        j_listings = json::parse(f);
    }
    catch (json::exception e)
    {
        std::cout << "[ERROR] Failed to load listings database " << m_ListingsDatabaseFilePath << std::endl << e.what() << std::endl;
        return;
    }
    for (auto& [key, value] : j_listings["Listings"].items())
    {
        Core::Listing list = Core::Listing();
        list.item.itemId = key;
        list.item.name = value["name"];
        list.bidder = value["bidder"];
        list.sellerId = value["seller"];
        list.price = value["price"];
        list.buyoutPrice = value["buyout"];
        list.ends = value["ends"];
        listings.emplace_back(list);
    }
    if (listings.size() > 0)
    {
        CheckforExpiredListings();
    }
}



void Server::RemoveListing(Core::Listing listing)
{
    bool bIsUserOnline = false;
    bool bIsSellerOnline = false;
    uint32 excepted_client = 0;
    if (listing.bidder != "None")
    {
        for (auto& client : m_connectedClients)
        {
            if (client.second.userId == listing.bidder)
            {
                SendStringToClient(
                    (uint32)client.first,
                    std::format("You won the auction of {} for {}G", listing.item.name, listing.price).c_str());
                client.second.inventory.emplace_back(listing.item);
                bIsUserOnline = true;
                ToPlayerJson(client.second);
                excepted_client = client.first;
            }
            if (client.second.userId == listing.sellerId && listing.sellerId != "The State")
            {
                SendStringToClient(
                    (uint32)client.first,
                    std::format("You sold the auction of {} for {}G", listing.item.name, listing.price).c_str());
                client.second.balance += listing.price;
                client.second.listings.erase(
                    std::remove_if(client.second.listings.begin(), client.second.listings.end(), [&](Core::Listing const& l) {
                    return l.item.itemId == listing.item.itemId;
                }),
                    client.second.listings.end());
                bIsSellerOnline = true;
                ToPlayerJson(client.second);
            }
        }
        if (!bIsUserOnline)
        {
            Core::User user = GetUser(listing.bidder);
            user.inventory.emplace_back(listing.item);
            user.bufferMessages.emplace_back(std::format("You won the auction of {} for {}G", listing.item.name, listing.price));
            ToPlayerJson(user);
        }
        if (!bIsSellerOnline && listing.sellerId != "The State")
        {
            Core::User user = GetUser(listing.sellerId);
            user.balance += listing.price;
            user.listings.erase(
                std::remove_if(user.listings.begin(), user.listings.end(), [&](Core::Listing const& l) {
                return l.item.itemId == listing.item.itemId;
            }),
                user.listings.end());
            user.bufferMessages.emplace_back(std::format("You sold the auction of {} for {}G", listing.item.name, listing.price));
            ToPlayerJson(user);
        }

    }
    SendStringToAllClients(
        std::format("{} won the auction of {} for {}G", listing.bidder, listing.item.name, listing.price).c_str(), (uint32)excepted_client);
    // https://stackoverflow.com/questions/32062126/how-to-remove-a-struct-element-from-a-vector
    listings.erase(
        std::remove_if(listings.begin(), listings.end(), [&](Core::Listing const& l) {
        return l.item.itemId == listing.item.itemId;
    }),
        listings.end());
    ToListingsJson();

}

bool Server::ToPlayerJson(const Core::User user)
{
    json players;
    try
    {
        std::ifstream f(m_PlayerDatabaseFilePath.string());
        players = json::parse(f);
    }
    catch (json::exception e)
    {
        std::cout << "[ERROR] Failed to load player database " << m_PlayerDatabaseFilePath << std::endl << e.what() << std::endl;
        return false;
    }
    try
    {
        if (!players.contains("Users"))
            players["Users"] = {};
        if (!players["Users"].contains(user.userId))
            players["Users"][user.userId] = {};
        players["Users"][user.userId]["balance"] = user.balance;
        if (!players["Users"][user.userId].contains("inventory"))
            players["Users"][user.userId]["inventory"] = {};
        for (Core::Item my_item : user.inventory)
        {
            if (!players["Users"][user.userId]["inventory"].contains(my_item.itemId))
                players["Users"][user.userId]["inventory"][my_item.itemId] = {};
            players["Users"][user.userId]["inventory"][my_item.itemId]["name"] = my_item.name;
        }
        players["Users"][user.userId]["buffer"] = user.bufferMessages;
        if (!players["Users"][user.userId].contains("listings"))
            players["Users"][user.userId]["listings"] = {};
        for (Core::Listing my_listings : user.listings)
        {
            players["Users"][user.userId]["listings"][my_listings.item.itemId] = {};
            players["Users"][user.userId]["listings"][my_listings.item.itemId]["itemname"] = my_listings.item.name;
            players["Users"][user.userId]["listings"][my_listings.item.itemId]["bidder"] = my_listings.bidder;
            players["Users"][user.userId]["listings"][my_listings.item.itemId]["seller"] = my_listings.sellerId;
            players["Users"][user.userId]["listings"][my_listings.item.itemId]["price"] = my_listings.price;
            players["Users"][user.userId]["listings"][my_listings.item.itemId]["buyoutprice"] = my_listings.buyoutPrice;
            players["Users"][user.userId]["listings"][my_listings.item.itemId]["ends"] = my_listings.ends;
        }
        if (std::filesystem::exists(std::filesystem::path(m_PlayerDatabaseFilePath)))
        {
            // Write JSON data to a file
            std::ofstream outFile(m_PlayerDatabaseFilePath);
            if (!outFile.is_open()) {
                std::cerr << "Failed to open Players.json for writing." << std::endl;
            }
            outFile << std::setw(4) << players << std::endl; // Pretty-print JSON
            outFile.close();
        }
        return true;
    }
    catch (json::exception e)
    {
        std::cout << "[ERROR] Failed to add player to database " << m_PlayerDatabaseFilePath << std::endl << e.what() << std::endl;
        return false;
    }
}
bool Server::ToListingsJson()
{
    json j_listings;
    try
    {
        std::ifstream f(m_ListingsDatabaseFilePath.string());
        j_listings = json::parse(f);
    }
    catch (json::exception e)
    {
        std::cout << "[ERROR] Failed to load listings database " << m_ListingsDatabaseFilePath << std::endl << e.what() << std::endl;
        return false;
    }
    try
    {
        if (!j_listings.contains("Listings"))
            j_listings["Listings"] = {};
        for (Core::Listing list : listings)
        {
            j_listings["Listings"][list.item.itemId] = {};
            j_listings["Listings"][list.item.itemId]["name"] = list.item.name;
            j_listings["Listings"][list.item.itemId]["bidder"] = list.bidder;
            j_listings["Listings"][list.item.itemId]["seller"] = list.sellerId;
            j_listings["Listings"][list.item.itemId]["price"] = list.price;
            j_listings["Listings"][list.item.itemId]["buyout"] = list.buyoutPrice;
            j_listings["Listings"][list.item.itemId]["ends"] = list.ends;
        }
        if (std::filesystem::exists(std::filesystem::path(m_ListingsDatabaseFilePath)))
        {
            // Write JSON data to a file
            std::ofstream outFile(m_ListingsDatabaseFilePath);
            if (!outFile.is_open()) {
                std::cerr << "Failed to open Listings.json for writing." << std::endl;
            }
            outFile << std::setw(4) << j_listings << std::endl; // Pretty-print JSON
            outFile.close();
        }
        return true;
    }
    catch (json::exception e)
    {
        std::cout << "[ERROR] Failed to add listings to database " << m_ListingsDatabaseFilePath << std::endl << e.what() << std::endl;
        return false;
    }
}

void Server::StartServer(const std::string address)
{
    s_pCallbackInstance = this;
    m_EventHistoryFilePath = "EventHistory.json";
    m_PlayerDatabaseFilePath = "Players.json";
    m_ListingsDatabaseFilePath = "Listings.json";
    CreateFileIfNotExists(m_EventHistoryFilePath, "Events");
    CreateFileIfNotExists(m_PlayerDatabaseFilePath, "Users");
    CreateFileIfNotExists(m_ListingsDatabaseFilePath, "Listings");
    try
    {
        std::ifstream f(m_PlayerDatabaseFilePath.string());
        json players = json::parse(f);
    }
    catch (json::exception e)
    {
        std::cout << "[ERROR] Failed to load player database " << m_PlayerDatabaseFilePath << std::endl << e.what() << std::endl;
        return;
    }
    try
    {
        std::ifstream f(m_EventHistoryFilePath.string());
        json events = json::parse(f);
    }
    catch (json::exception e)
    {
        std::cout << "[ERROR] Failed to load event database " << m_EventHistoryFilePath << std::endl << e.what() << std::endl;
        return;
    }

	m_pInterface = SteamNetworkingSockets();
    LoadListings();
    PostRandomListing();
    // Start listening
    SteamNetworkingIPAddr serverLocalAddr;
    serverLocalAddr.Clear();
    serverLocalAddr.ParseString(address.c_str());

    SteamNetworkingConfigValue_t opt;
    opt.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, (void*)Server::ConnectionStatusChangedCallback);
    m_hListenSock = m_pInterface->CreateListenSocketIP(serverLocalAddr, 1, &opt);
    if (m_hListenSock == k_HSteamListenSocket_Invalid)
        Core::FatalError("Failed to listen on port %s", address);
    m_hPollGroup = m_pInterface->CreatePollGroup();
    if (m_hPollGroup == k_HSteamNetPollGroup_Invalid)
        Core::FatalError("Failed to listen on port %s", address);
    Core::Printf(std::format("Server listening on port {}\n", address).c_str());

    while (!Core::g_bQuit)
    {
        PollIncomingMessages();
        PollConnectionStateChanges();
        PollLocalUserInput();
        CheckforExpiredListings();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Close all the connections
    Core::Printf("Closing connections...\n");
    for (auto it : m_connectedClients)
    {
        SendStringToClient(it.first, "Server is shutting down.  Goodbye.");
        m_pInterface->CloseConnection(it.first, 0, "Server Shutdown", true);
    }
    m_connectedClients.clear();

    m_pInterface->CloseListenSocket(m_hListenSock);
    m_hListenSock = k_HSteamListenSocket_Invalid;

    m_pInterface->DestroyPollGroup(m_hPollGroup);
    m_hPollGroup = k_HSteamNetPollGroup_Invalid;
}



