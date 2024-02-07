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

void Server::PollIncomingMessages()
{
	// I denna funktionen beh�ver jag checka ifall cmd �r buy, bid, list, osv. Sen beh�ver jag printa ut till alla den nya listan av saker.
	char temp[1024];

	while (!Core::g_bQuit)
	{
		ISteamNetworkingMessage* pIncomingMsg = nullptr;
		int numMsgs = m_pInterface->ReceiveMessagesOnPollGroup(m_hPollGroup, &pIncomingMsg, 1);
		if (numMsgs == 0)
			break;
		if (numMsgs < 0)
			Core::FatalError("Error checking for messages");
		assert(numMsgs == 1 && pIncomingMsg);
		auto itClient = m_connectedClients.find(pIncomingMsg->m_conn);
		assert(itClient != m_connectedClients.end());

		// '\0'-terminate it to make it easier to parse
		std::string sCmd;
		sCmd.assign((const char*)pIncomingMsg->m_pData, pIncomingMsg->m_cbSize);
		const char* cmd = sCmd.c_str();

		// We don't need this anymore.
		pIncomingMsg->Release();
		std::cout << std::format("[{}]: {}", itClient->second.userId.c_str(), cmd) << std::endl;
		// Check for known commands.  None of this example code is secure or robust.
		// Don't write a real server like this, please.

		if (strncmp(cmd, "/nick", 5) == 0)
		{
			if (itClient->second.userId == "")
			{
				const char* nick = cmd + 5;
				while (isspace(*nick))
					++nick;

				// Actually change their name
				SetClientNick(itClient->first, nick);
				continue;
			}
			
		}
		else if (strncmp(cmd, "/bid", 4) == 0)
		{
			//Do something
			const char* bid = cmd + 4;
			std::istringstream iss(sCmd);

			// Vector to store the words
			std::vector<std::string> words;

			// Read words from the input stream
			std::string word;
			while (iss >> word) {
				words.push_back(word);
			}
			if (words.size() != 3)
			{
				sprintf(temp, "Your bid request was mallformed and not processed. Make sure to say: /bid LISTINGINDEX AMOUNT");
				SendStringToClient(itClient->first, temp);
			}
			int listing_index;
			int bid_amount;
			try {
				// Convert string to integer using std::stoi
				listing_index = std::stoi(words[1]) - 1;
				bid_amount = std::stoi(words[2]);
			}
			catch (const std::invalid_argument& e) {
				sprintf(temp, "Your bid request was mallformed and not processed. Make sure to say: /bid LISTINGINDEX AMOUNT");
				SendStringToClient(itClient->first, temp);
				continue;
			}
			catch (const std::out_of_range& e) {
				sprintf(temp, "Your bid request was mallformed and not processed. Make sure to say: /bid LISTINGINDEX AMOUNT");
				SendStringToClient(itClient->first, temp);
				continue;
			}
			if (bid_amount > itClient->second.balance)
			{
				sprintf(temp, "Insufficent funds. You only have %i G", itClient->second.balance);
				SendStringToClient(itClient->first, temp);
				continue;
			}
			if (listing_index < 0 || listing_index > listings.size())
			{
				sprintf(temp, "Invalid Listing");
				SendStringToClient(itClient->first, temp);
				continue;
			}
			if (listings[listing_index].price >= bid_amount)
			{
				sprintf(temp, "Your bid was too low and won't be accepted");
				SendStringToClient(itClient->first, temp);
				continue;
			}
			listings[listing_index].price = bid_amount;
			listings[listing_index].bidder = itClient->second.userId;
			itClient->second.balance -= bid_amount;
			sprintf(temp, std::format("You bid on {}.{} the new price is now {}",
				listing_index + 1, listings[listing_index].item.name,
				listings[listing_index].price
			).c_str());
			SendStringToClient(itClient->first, temp);
			sprintf(temp, std::format("{} Made a bid on {}.{} the new price is now {}", 
				itClient->second.userId, 
				listing_index + 1,listings[listing_index].item.name, 
				listings[listing_index].price
			).c_str());
			SendStringToAllClients(temp, itClient->first);
			continue;
		}
		else if (strncmp(cmd, "/listings", 9) == 0)
		{
			SendListings(itClient->first);
		}

		// Assume it's just a ordinary chat message, dispatch to everybody else
		sprintf(temp, "%s: %s", itClient->second.userId.c_str(), cmd);
		SendStringToAllClients(temp, itClient->first);
	}
}
void Server::PollLocalUserInput()
{
	std::string cmd;
	while (!Core::g_bQuit && Core::LocalUserInput_GetNext(cmd))
	{
		if (strcmp(cmd.c_str(), "/quit") == 0)
		{
			Core::g_bQuit = true;
			Core::Printf("Shutting down server");
			break;
		}

		// That's the only command we support
		Core::Printf("The server only knows one command: '/quit'");
	}
}
void Server::SetClientNick(uint32_t hConn, const char* nick)
{
	char temp[1024];
	m_connectedClients[hConn] = GetUser(nick);
	// Send them a welcome message
	sprintf(temp, "Welcome %s", nick);
	SendStringToClient(hConn, temp);

	// Also send them a list of everybody who is already connected
	if (m_connectedClients.empty())
	{
		SendStringToClient((uint32)hConn, "No one else is online.");
	}
	else
	{
		sprintf(temp, "There are %d players online:", (int)m_connectedClients.size());
		SendStringToClient(hConn, temp);
		for (auto& c : m_connectedClients)
			SendStringToClient((uint32)hConn, c.second.userId.c_str());
	}
	SendStringToClient((uint32)hConn, "The listed items are: ");
	SendListings((uint32)hConn);
	
	// Let everybody else know who they are for now
	sprintf(temp, "'%s' has connected", nick);
	SendStringToAllClients(temp, (uint32)hConn);
	// Set the connection name, too, which is useful for debugging
	m_pInterface->SetConnectionName(hConn, nick);
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
			assert(itClient != m_connectedClients.end());

			// Select appropriate log messages
			const char* pszDebugLogAction;
			if (pInfo->m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally)
			{
				pszDebugLogAction = "problem detected locally";
				sprintf(temp, "Alas, %s hath fallen into shadow.  (%s)", itClient->second.userId.c_str(), pInfo->m_info.m_szEndDebug);
			}
			else
			{
				// Note that here we could check the reason code to see if
				// it was a "usual" connection or an "unusual" one.
				pszDebugLogAction = "closed by peer";
				sprintf(temp, "%s hath departed", itClient->second.userId.c_str());
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
			assert(pInfo->m_eOldState == k_ESteamNetworkingConnectionState_Connecting);
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
		assert(m_connectedClients.find(pInfo->m_hConn) == m_connectedClients.end());

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

		// Generate a random nick.  A random temporary nick
		// is really dumb and not how you would write a real chat server.
		// You would want them to have some sort of signon message,
		// and you would keep their client in a state of limbo (connected,
		// but not logged on) until them.  I'm trying to keep this example
		// code really simple.

		// Add them to the client list, using std::map wacky syntax
		m_connectedClients[(uint32)pInfo->m_hConn];
		break;
	}

	case k_ESteamNetworkingConnectionState_Connected:
		// We will get a callback immediately after accepting the connection.
		// Since we are the server, we can ignore this, it's not news to us.
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

void Server::StartServer(const std::string address)
{
	s_pCallbackInstance = this;
	// Select instance to use.  For now we'll always use the default.
		// But we could use SteamGameServerNetworkingSockets() on Steam.
	m_EventHistoryFilePath = "EventHistory.json";
	m_PlayerDatabaseFilePath = "Players.json";
	if (!std::filesystem::exists(std::filesystem::path(m_EventHistoryFilePath)))
	{
		json new_j;
		new_j["Events"] = {};
		// Write JSON data to a file
		std::ofstream outFile(m_EventHistoryFilePath);
		if (!outFile.is_open()) {
			std::cerr << "Failed to open EventHistory.json for writing." << std::endl;
		}
		outFile << std::setw(4) << new_j << std::endl; // Pretty-print JSON
		outFile.close();
	}
	if (!std::filesystem::exists(std::filesystem::path(m_PlayerDatabaseFilePath)))
	{
		json new_j;
		new_j["Users"] = {};
		// Write JSON data to a file
		std::ofstream outFile(m_PlayerDatabaseFilePath);
		if (!outFile.is_open()) {
			std::cerr << "Failed to open Players.json for writing." << std::endl;
		}
		outFile << std::setw(4) << new_j << std::endl; // Pretty-print JSON
		outFile.close();
	}
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
		json players = json::parse(f);
	}
	catch (json::exception e)
	{
		std::cout << "[ERROR] Failed to load event database " << m_EventHistoryFilePath << std::endl << e.what() << std::endl;
		return;
	}
	m_pInterface = SteamNetworkingSockets();
	listings.resize(1);
	Core::Listing list = Core::Listing();
	list.item.itemId = "2340258";
	list.item.name = "Axe of doom";
	list.sellerId = "The State";
	list.price = 100;
	list.buyoutPrice = 400;
	list.ends = ListTimeToString();
	listings[0] = list;
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
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	// Close all the connections
	Core::Printf("Closing connections...\n");
	for (auto it : m_connectedClients)
	{
		// Send them one more goodbye message.  Note that we also have the
		// connection close reason as a place to send final data.  However,
		// that's usually best left for more diagnostic/debug text not actual
		// protocol strings.
		SendStringToClient(it.first, "Server is shutting down.  Goodbye.");

		// Close the connection.  We use "linger mode" to ask SteamNetworkingSockets
		// to flush this out and close gracefully.
		m_pInterface->CloseConnection(it.first, 0, "Server Shutdown", true);
	}
	m_connectedClients.clear();

	m_pInterface->CloseListenSocket(m_hListenSock);
	m_hListenSock = k_HSteamListenSocket_Invalid;

	m_pInterface->DestroyPollGroup(m_hPollGroup);
	m_hPollGroup = k_HSteamNetPollGroup_Invalid;
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
	for(auto& [key, value] : players["Users"]["inventory"].items())
	{
		Core::Item temp_item = Core::Item();
		temp_item.itemId = key;
		temp_item.name = value["name"];
		temp_user.inventory.emplace_back(temp_item);
	}
	for (auto& [key, value] : players["Users"]["listings"].items())
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

	return temp_user;
}

void Server::SendListings(uint32_t hConn)
{
	for (size_t i = 0; i < listings.size(); i++)
	{
		SendStringToClient(
			(uint32)hConn,
			std::format("{}. {}. Current bid: {}. Buyout price: {}. Current bid by: {}. Seller: {}. Ends at {}",
				i+1,
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
	oss << std::put_time(localTime, "%H:%M");
	return oss.str();
}

void Server::CheckforExpiredListings()
{
	auto currentTime = std::chrono::system_clock::now();
	std::time_t timeT = std::chrono::system_clock::to_time_t(currentTime);
	std::tm* localTime = std::localtime(&timeT);
	std::vector<Core::Listing> expired_listings;
	localTime->tm_hour;
	for(Core::Listing listing : listings)
	{

		// Vector to store the split parts
		std::vector<uint16> parts;

		// Find the first occurrence of ':'
		size_t pos = listing.ends.find(':');

		// Check if ':' exists in the string
		if (pos != std::string::npos) {
			// Split the string into two parts
			std::string s_Hour = listing.ends.substr(0, pos);
			std::string s_Minute = listing.ends.substr(pos + 1);

			// Add the parts to the vector
			parts.push_back(std::stoi(s_Hour));
			parts.push_back(std::stoi(s_Minute));
		}
		if (parts[0] == localTime->tm_hour && parts[1] > localTime->tm_min)
		{
			expired_listings.emplace_back(listing);
		}
	}
	for (Core::Listing listing : expired_listings)
	{
		RemoveListing(listing);
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
			ToPlayerJson(user);
		}
		if (!bIsSellerOnline && listing.sellerId != "The State")
		{
			Core::User user = GetUser(listing.bidder);
			user.balance += listing.price;
			user.listings.erase(
				std::remove_if(user.listings.begin(), user.listings.end(), [&](Core::Listing const& l) {
				return l.item.itemId == listing.item.itemId;
			}),
				user.listings.end());
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
	
}

bool Server::ToPlayerJson(Core::User user)
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
		players["Users"][user.userId] = {};
		players["Users"][user.userId]["balance"] = user.balance;
		players["Users"][user.userId]["inventory"] = {};
		for (Core::Item my_item : user.inventory)
		{
			players["Users"][user.userId]["inventory"][my_item.itemId] = {};
			players["Users"][user.userId]["inventory"][my_item.itemId]["name"] = my_item.name;
		}
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

