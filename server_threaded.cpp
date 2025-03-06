#include <bits/stdc++.h>
#include <netinet/in.h>
#include <pthread.h>
#include <unistd.h>
using namespace std;

#define pb push_back
#define f(n) for (int i = 0; i < (n); i++)

const int N = 256;
const int BAN_TIME = 120;
const int BAN_LEN = 56; 
const string ban_msg = "You has been banned from the server.  [Reason: Time Out]";

void error(const char *msg) {
    perror(msg);
    exit(1);
}

atomic<int> client_index(-1);
vector<double> timeout(256);
set<int> active_clients;
map<string, int> nameToSockfd;

map<string, set<int>> roomClients;
map<int, string> clientRooms;

pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

struct clientData {
    int idx;
    int socket_fd;
    string name;
};

void *timeoutCheck(void *arg) {
    clientData client_data = *(clientData *)arg; 

    while (true) {
        this_thread::sleep_for(chrono::seconds(1));

        pthread_mutex_lock(&clients_mutex);

        auto now = chrono::high_resolution_clock::now();
        double current_time = chrono::duration<double>(now.time_since_epoch()).count();
        if (current_time - timeout[client_data.idx] >= BAN_TIME) 
        {
            if (active_clients.find(client_data.socket_fd) != active_clients.end()) 
            {
                active_clients.erase(client_data.socket_fd);
                
                if (clientRooms.find(client_data.socket_fd) != clientRooms.end()) {
                    string roomName = clientRooms[client_data.socket_fd];
                    if (roomClients.find(roomName) != roomClients.end()) {
                        roomClients[roomName].erase(client_data.socket_fd);
                    }
                    clientRooms.erase(client_data.socket_fd);
                }

                // Changes below
                string msg =  "Client " + client_data.name + " has been banned from the server. ";
                msg += "[Reason: Time Out]";
                msg += "\nActive Clients: " + to_string(active_clients.size());
                cout<< msg << endl; cout<<endl;
                msg+="\n\nYou: ";
                for (int sockfd : active_clients) write(sockfd, msg.c_str(), msg.size());
                write(client_data.socket_fd, ban_msg.c_str(), BAN_LEN);

                shutdown(client_data.socket_fd, SHUT_RDWR);
                close(client_data.socket_fd);
            }
            pthread_mutex_unlock(&clients_mutex);
            pthread_exit(NULL);
        }

        pthread_mutex_unlock(&clients_mutex);
    }
}

void *Clients(void *arg) {
    int newsockfd = *(int *)arg;

    pthread_mutex_lock(&clients_mutex);
    client_index++;
    int idx = client_index;
    active_clients.insert(newsockfd);
    pthread_mutex_unlock(&clients_mutex);

    char buffer[N];
    bzero(buffer, N);
    int n = read(newsockfd, buffer, N - 1);
    if (n <= 0) {
        close(newsockfd);
        pthread_exit(NULL);
    }

    string name(buffer);
    nameToSockfd[name] = newsockfd;

    string message = "\n" + name + " joined the chat.\n";
    message += "\nActive Clients: " + to_string(active_clients.size());
    cout << message << endl; cout<<endl;
    message += "\n\nYou: "; 
    
    pthread_mutex_lock(&clients_mutex);
    for (int sockfd : active_clients) {
        if (sockfd != newsockfd) {
            write(sockfd, message.c_str(), message.size());
        }
    }
    pthread_mutex_unlock(&clients_mutex);

    auto now = chrono::high_resolution_clock::now();
    pthread_mutex_lock(&clients_mutex);
    timeout[idx] = chrono::duration<double>(now.time_since_epoch()).count();
    pthread_mutex_unlock(&clients_mutex);

    clientData *client_data = (clientData *)malloc(sizeof(clientData));
    client_data->idx = idx;
    client_data->socket_fd = newsockfd;
    client_data->name = name;
    pthread_t timeCheck;
    pthread_create(&timeCheck, NULL, timeoutCheck, client_data);
    pthread_detach(timeCheck);

    while (true) {
        if(active_clients.find(newsockfd)==active_clients.end()) break; // Changes here
        
        bzero(buffer, N);
        n = read(newsockfd, buffer, N - 1);
        if(n < 0) error("ERROR reading from socket");

        string input(buffer);

        if(input.find("/join ") == 0) {
            string roomName = input.substr(6);

            pthread_mutex_lock(&clients_mutex);
            if (clientRooms.find(newsockfd) != clientRooms.end()) {
                string oldRoom = clientRooms[newsockfd];
                roomClients[oldRoom].erase(newsockfd);
            }
            roomClients[roomName].insert(newsockfd);
            clientRooms[newsockfd] = roomName;
            pthread_mutex_unlock(&clients_mutex);

            string ack = "Joined private room: " + roomName + "\n\nYou: ";
            write(newsockfd, ack.c_str(), ack.size());
            continue; 
        }
        else if(input.find("/leave", 0) == 0) {
            pthread_mutex_lock(&clients_mutex);
            if (clientRooms.find(newsockfd) != clientRooms.end()){
                string roomName = clientRooms[newsockfd];
                roomClients[roomName].erase(newsockfd);
                clientRooms.erase(newsockfd);
            }
            pthread_mutex_unlock(&clients_mutex);
            
            string ack = "Left private room. You are now in public chat.\n\nYou: ";
            write(newsockfd, ack.c_str(), ack.size());
            continue;
        }
        else if (input.find("/rooms") == 0) {  
            pthread_mutex_lock(&clients_mutex);
            string roomList = "Available Rooms:\n";
            for (auto &room : roomClients) {
                roomList += room.first + " (" + to_string(room.second.size()) + " users): ";

                for (int sockfd : room.second) {
                    string clientName;
                    for (auto &pair : nameToSockfd) {
                        if (pair.second == sockfd) {
                            clientName = pair.first;
                            break;
                        }
                    }
                    roomList += clientName + " ";
                }
                roomList += "\n";
            }
            roomList += "\nYou: ";
            pthread_mutex_unlock(&clients_mutex);
            write(newsockfd, roomList.c_str(), roomList.size());
            continue;
        }
        
        if (clientRooms.find(newsockfd) != clientRooms.end()) {
            string roomName = clientRooms[newsockfd];
            string roomMessage = name + " (in " + roomName + "): " + input;
            pthread_mutex_lock(&clients_mutex);
            for (int sockfd : roomClients[roomName]) {
                if (sockfd != newsockfd) {  // send to everyone else in the room
                    write(sockfd, roomMessage.c_str(), roomMessage.size());
                }
            }
            pthread_mutex_unlock(&clients_mutex);
            cout << roomMessage << endl;
            
            roomMessage += "\n\nYou: ";
            write(newsockfd, roomMessage.c_str(), roomMessage.size());

            now = chrono::high_resolution_clock::now();
            pthread_mutex_lock(&clients_mutex);
            timeout[idx] = chrono::duration<double>(now.time_since_epoch()).count();
            pthread_mutex_unlock(&clients_mutex);
            continue;
        }

        // Changes below
        string message;
        int len = strlen(buffer);
        int cnt = (len>=5 && buffer[0]=='$' && buffer[1]=='[');
        string msg="";
        vector<string> Receivers;
        string cur="";
        for(int i=2;i<len;i++){
            if(cnt==0 || cnt==2) msg.pb(buffer[i]);
            else if(cnt==1){
                if(buffer[i]==']'){
                    if(nameToSockfd.find(cur)!=nameToSockfd.end()) Receivers.pb(cur);
                    cnt++;
                    i++;
                }
                else if(buffer[i]==','){
                    if(nameToSockfd.find(cur)!=nameToSockfd.end()) Receivers.pb(cur);
                    cur="";
                }
                else cur.pb(buffer[i]);
            }
        }
        bool public_ = (cnt<2);
        if(public_){
            msg = buffer;
            message = name + ": " + msg;
        }
        else{
            message = name + "->";
            for(string Receiver:Receivers) message += Receiver + ",";
            message += ": " + msg;
        }
        
        if (public_ && msg == "exit") { // CHange here
            pthread_mutex_lock(&clients_mutex);
            if(active_clients.find(newsockfd)!=active_clients.end()) // Changes here
                active_clients.erase(newsockfd);
            pthread_mutex_unlock(&clients_mutex);

            message = name + " left the chat.\n";
            message += "\nActive Clients: " + to_string(active_clients.size());
        }
        cout << message << endl; cout<<endl;
        message += "\n\nYou: "; // Changes here

        pthread_mutex_lock(&clients_mutex);

        if(public_){
            for (int sockfd : active_clients) {
                if (sockfd != newsockfd) {
                    write(sockfd, message.c_str(), message.size());
                }
            }
        }
        else {
            // Changes here
            for(string Receiver:Receivers){
                auto it = nameToSockfd.find(Receiver);
                if(it != nameToSockfd.end()){
                    message = name + "->You: " + msg;
                    message += "\n\nYou: ";
                    int sockfd = (*it).second;
                    write(sockfd, message.c_str(), message.size());
                }
            }
        }
        
        pthread_mutex_unlock(&clients_mutex);

        now = chrono::high_resolution_clock::now();
        pthread_mutex_lock(&clients_mutex);
        timeout[idx] = chrono::duration<double>(now.time_since_epoch()).count();
        pthread_mutex_unlock(&clients_mutex);
    }

    shutdown(newsockfd, SHUT_RDWR);
    close(newsockfd);
    pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "ERROR, no port provided\n");
        exit(1);
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) error("ERROR opening socket");

    int portno = atoi(argv[1]);
    struct sockaddr_in serv_addr, cli_addr;
    bzero((char *)&serv_addr, sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);

    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
        error("ERROR on binding");

    cout << "Active Clients = 0" << endl;

    while (true) {
        listen(sockfd, 5);
        socklen_t clilen = sizeof(cli_addr);
        int newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
        if (newsockfd < 0) error("ERROR on accept");

        pthread_mutex_lock(&clients_mutex);
        if (active_clients.find(newsockfd) != active_clients.end()) {
            pthread_mutex_unlock(&clients_mutex);
            continue;
        }
        pthread_mutex_unlock(&clients_mutex);

        pthread_t thread_id;
        pthread_create(&thread_id, NULL, Clients, &newsockfd);
        pthread_detach(thread_id);
    }

    return 0;
}
