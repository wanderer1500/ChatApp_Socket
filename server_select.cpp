#include <bits/stdc++.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <unistd.h>
using namespace std;

#define N 256
const int BAN_TIME = 60;
const int BAN_LEN = 56;
const string ban_msg = "You has been banned from the server.  [Reason: Time Out]";

void error(const char *msg) {
    perror(msg);
    exit(1);
}

// Global data structures (similar to your original code)
set<int> active_clients;
map<string, int> nameToSockfd;
map<int, string> sockfdToName; // inverse mapping for convenience
map<string, set<int>> roomClients;
map<int, string> clientRooms;
map<int, double> lastActive; // last active time for each client

// Helper function: broadcast message to all active clients except the sender
void broadcastMessage(const string &msg, int exclude_fd = -1) {
    for (int fd : active_clients) {
        if (fd != exclude_fd) {
            write(fd, msg.c_str(), msg.size());
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "ERROR, no port provided\n");
        exit(1);
    }
    
    int portno = atoi(argv[1]);
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0)
        error("ERROR opening socket");
    
    struct sockaddr_in serv_addr;
    bzero((char *)&serv_addr, sizeof(serv_addr));
    
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);
    
    if (bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
        error("ERROR on binding");
    
    if (listen(listenfd, 5) < 0)
        error("ERROR on listen");
    
    cout << "Server started on port " << portno << endl;
    
    // Prepare for select()
    fd_set masterSet, readSet;
    FD_ZERO(&masterSet);
    FD_SET(listenfd, &masterSet);
    int maxfd = listenfd;
    
    while (true) {
        readSet = masterSet;
        // Use a 1-second timeout for select() so we can check for timeouts
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        
        int activity = select(maxfd + 1, &readSet, NULL, NULL, &tv);
        if (activity < 0) {
            error("ERROR on select");
        }
        
        // --- 1. Check for new incoming connection ---
        if (FD_ISSET(listenfd, &readSet)) {
            struct sockaddr_in cli_addr;
            socklen_t clilen = sizeof(cli_addr);
            int newsockfd = accept(listenfd, (struct sockaddr *)&cli_addr, &clilen);
            if (newsockfd < 0)
                error("ERROR on accept");
            
            // Add new socket to master set and active_clients set
            FD_SET(newsockfd, &masterSet);
            if (newsockfd > maxfd)
                maxfd = newsockfd;
            active_clients.insert(newsockfd);
            
            // Read the client's name as the first message
            char buffer[N];
            bzero(buffer, N);
            int n = read(newsockfd, buffer, N - 1);
            if (n <= 0) {
                close(newsockfd);
                FD_CLR(newsockfd, &masterSet);
                active_clients.erase(newsockfd);
            } else {
                string name(buffer);
                // Remove potential newline characters
                name.erase(remove(name.begin(), name.end(), '\n'), name.end());
                name.erase(remove(name.begin(), name.end(), '\r'), name.end());
                
                nameToSockfd[name] = newsockfd;
                sockfdToName[newsockfd] = name;
                lastActive[newsockfd] = time(NULL);
                
                // Notify others about the new client
                string joinMsg = "\n" + name + " joined the chat.\n";
                joinMsg += "\nActive Clients: " + to_string(active_clients.size()) + "\n\nYou: ";
                broadcastMessage(joinMsg, newsockfd);
                
                // Send welcome message to the new client
                string welcomeMsg = "Welcome " + name + "!\n\nYou: ";
                write(newsockfd, welcomeMsg.c_str(), welcomeMsg.size());
                
                cout << joinMsg << endl;
            }
        }
        
        // --- 2. Check each client for incoming messages ---
        for (auto it = active_clients.begin(); it != active_clients.end(); ) {
            int clientfd = *it;
            if (FD_ISSET(clientfd, &readSet)) {
                char buffer[N];
                bzero(buffer, N);
                int n = read(clientfd, buffer, N - 1);
                
                if (n <= 0) {
                    // Connection closed or error; clean up the client
                    string clientName = sockfdToName[clientfd];
                    close(clientfd);
                    FD_CLR(clientfd, &masterSet);
                    it = active_clients.erase(it);
                    
                    nameToSockfd.erase(clientName);
                    sockfdToName.erase(clientfd);
                    if (clientRooms.find(clientfd) != clientRooms.end()) {
                        string roomName = clientRooms[clientfd];
                        roomClients[roomName].erase(clientfd);
                        clientRooms.erase(clientfd);
                    }
                    lastActive.erase(clientfd);
                    
                    string leaveMsg = clientName + " left the chat.\n";
                    leaveMsg += "\nActive Clients: " + to_string(active_clients.size());
                    broadcastMessage(leaveMsg);
                    cout << leaveMsg << endl;
                    continue;  // Move to next client
                } else {
                    string input(buffer);
                    // Update the client's last active timestamp
                    lastActive[clientfd] = time(NULL);
                    string clientName = sockfdToName[clientfd];
                    
                    // --- 2a. Process Commands ---
                    if (input.find("/join ") == 0) {
                        string roomName = input.substr(6);
                        // Trim trailing newline/whitespace
                        roomName.erase(roomName.find_last_not_of(" \n\r\t") + 1);
                        // Remove from old room if any
                        if (clientRooms.find(clientfd) != clientRooms.end()) {
                            string oldRoom = clientRooms[clientfd];
                            roomClients[oldRoom].erase(clientfd);
                        }
                        roomClients[roomName].insert(clientfd);
                        clientRooms[clientfd] = roomName;
                        
                        string ack = "Joined private room: " + roomName + "\n\nYou: ";
                        write(clientfd, ack.c_str(), ack.size());
                    }
                    else if (input.find("/leave") == 0) {
                        if (clientRooms.find(clientfd) != clientRooms.end()) {
                            string roomName = clientRooms[clientfd];
                            roomClients[roomName].erase(clientfd);
                            clientRooms.erase(clientfd);
                        }
                        string ack = "Left private room. You are now in public chat.\n\nYou: ";
                        write(clientfd, ack.c_str(), ack.size());
                    }
                    else if (input.find("/rooms") == 0) {
                        string roomList = "Available Rooms:\n";
                        for (auto &room : roomClients) {
                            roomList += room.first + " (" + to_string(room.second.size()) + " users): ";
                            for (int fd : room.second) {
                                roomList += sockfdToName[fd] + " ";
                            }
                            roomList += "\n";
                        }
                        roomList += "\nYou: ";
                        write(clientfd, roomList.c_str(), roomList.size());
                    }
                    // --- 2b. Process regular messages ---
                    else {
                        // If the client is in a private room, send the message only to that room
                        if (clientRooms.find(clientfd) != clientRooms.end()) {
                            string roomName = clientRooms[clientfd];
                            string roomMessage = clientName + " (in " + roomName + "): " + input;
                            for (int fd : roomClients[roomName]) {
                                if (fd != clientfd) {
                                    write(fd, roomMessage.c_str(), roomMessage.size());
                                }
                            }
                            roomMessage += "\n\nYou: ";
                            write(clientfd, roomMessage.c_str(), roomMessage.size());
                        }
                        // Check for private messaging using the $[...] syntax
                        else if (input.substr(0, 2) == "$[") {
                            size_t closeBracket = input.find(']');
                            if (closeBracket != string::npos) {
                                string receiversStr = input.substr(2, closeBracket - 2);
                                vector<string> receivers;
                                stringstream ss(receiversStr);
                                string token;
                                while (getline(ss, token, ',')) {
                                    token.erase(remove(token.begin(), token.end(), ' '), token.end());
                                    if (nameToSockfd.find(token) != nameToSockfd.end())
                                        receivers.push_back(token);
                                }
                                string actualMsg = input.substr(closeBracket + 1);
                                string privateMsg = clientName + "->";
                                for (auto &r : receivers) {
                                    privateMsg += r + ",";
                                }
                                privateMsg += ": " + actualMsg;
                                for (auto &r : receivers) {
                                    int targetfd = nameToSockfd[r];
                                    write(targetfd, privateMsg.c_str(), privateMsg.size());
                                }
                            }
                        }
                        // Otherwise, it is a public message.
                        else {
                            // If the message is "exit", then disconnect this client.
                            if (input == "exit") {
                                close(clientfd);
                                FD_CLR(clientfd, &masterSet);
                                active_clients.erase(clientfd);
                                nameToSockfd.erase(clientName);
                                sockfdToName.erase(clientfd);
                                if (clientRooms.find(clientfd) != clientRooms.end()) {
                                    string roomName = clientRooms[clientfd];
                                    roomClients[roomName].erase(clientfd);
                                    clientRooms.erase(clientfd);
                                }
                                lastActive.erase(clientfd);
                                
                                string leaveMsg = clientName + " left the chat.\n";
                                leaveMsg += "\nActive Clients: " + to_string(active_clients.size());
                                broadcastMessage(leaveMsg);
                                cout << leaveMsg << endl;
                                continue;
                            }
                            
                            string publicMsg = clientName + ": " + input;
                            broadcastMessage(publicMsg, clientfd);
                            publicMsg += "\n\nYou: ";
                            write(clientfd, publicMsg.c_str(), publicMsg.size());
                        }
                    }
                }
            }
            ++it;
        }
        
        // --- 3. Check for client timeouts ---
        double now = time(NULL);
        vector<int> toBan;
        for (auto &entry : lastActive) {
            int fd = entry.first;
            double last = entry.second;
            if (now - last >= BAN_TIME) {
                toBan.push_back(fd);
            }
        }
        for (int fd : toBan) {
            string bannedName = sockfdToName[fd];
            // Close and clean up the banned client
            close(fd);
            FD_CLR(fd, &masterSet);
            active_clients.erase(fd);
            nameToSockfd.erase(bannedName);
            sockfdToName.erase(fd);
            if (clientRooms.find(fd) != clientRooms.end()) {
                string roomName = clientRooms[fd];
                roomClients[roomName].erase(fd);
                clientRooms.erase(fd);
            }
            lastActive.erase(fd);
            
            string banNotification = "Client " + bannedName + " has been banned from the server. [Reason: Time Out]";
            banNotification += "\nActive Clients: " + to_string(active_clients.size());
            broadcastMessage(banNotification);
            // Optionally, attempt to write the ban message to the client (may fail if already closed)
            write(fd, ban_msg.c_str(), ban_msg.size());
            cout << banNotification << endl;
        }
    }
    
    return 0;
}
