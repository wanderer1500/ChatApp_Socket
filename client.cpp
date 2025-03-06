#include <bits/stdc++.h>
using namespace std;
#include <netinet/in.h>
#include <netdb.h>

#define pb push_back
#define f(n) for(int i=0;i<(n);i++)

void error(const char *msg){
    perror(msg);
    exit(0);
}

const int N = 256;
int sockfd;

bool exit_=false,print_=false;;
void *Write(void *useless){
    while(true){
        // Changes below
        cout<<"You: ";
        string msg;
        getline(cin,msg); cout<<endl;
        ssize_t n = write(sockfd,msg.c_str(),msg.size()); 
        if(n < 0) error("ERROR writing to socket");
        exit_=(msg=="exit");
        if(exit_){
            if(!print_){
                print_=true;
                cout<<"Bye!!"<<endl; cout<<endl;
            }
            exit(0);
        }
    }
}
void *Read(void *useless) {
    char buffer[N];
    while(true){
        if(exit_){
            if(!print_){
                print_=true;
                cout<<"Bye!!"<<endl; cout<<endl;
            }
            exit(0);
        }
        bzero(buffer,N);
        ssize_t n = read(sockfd,buffer,N-1);
        if(n < 0) error("ERROR reading from socket");
        int len = strlen(buffer);
        string msg = buffer;
        cout<<endl; cout<<endl; cout<< msg; fflush(stdout);
        // CHanges below
        if(msg == "You has been banned from the server.  [Reason: Time Out]")
            exit(0);
    }
}

int main(int argc, char *argv[]){
    if (argc < 3){
        fprintf(stderr,"usage %s hostname port\n", argv[0]);
        exit(0);
    }
    int portno = atoi(argv[2]);
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) error("ERROR opening socket");
    struct hostent *server;
    server = gethostbyname(argv[1]);
    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host\n");
        exit(0);
    }
    struct sockaddr_in serv_addr;
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr, server->h_length);
    serv_addr.sin_port = htons(portno);

    if(connect(sockfd,(struct sockaddr *)&serv_addr,sizeof(serv_addr))<0) error("ERROR connecting");
    
    // Changes below
    cout << "+----------------------------------------------------------------------------------+" << endl;
    cout << "|                                                                                  |" << endl;
    cout << "| How to send a message?                                                           |" << endl;
    cout << "| 1. By default the message will be sent to 'All' users.                           |" << endl;
    cout << "| 2. $[NAME] MESSAGE            -> To send the message to a particular user        |" << endl;
    cout << "| 3. $[NAME1,NAME2,...] MESSAGE -> To send message to a particular set of users    |" << endl;
    cout << "| 4. /rooms                     -> To list all the rooms with the users in them    |" << endl;
    cout << "| 5. /join ROOM_NAME            -> To join an existing room or to create a room    |" << endl;
    cout << "| 6. /leave                     -> To leave the joined room                        |" << endl;
    cout << "| 7. exit                       -> To exit the chat                                |" << endl;
    cout << "|                                                                                  |" << endl;
    cout << "+----------------------------------------------------------------------------------+" << endl;

    string name;
    cout << "+----------------------------------------------------------------------------------+" << endl;
    cout<<  "|  Enter your name: ";
    getline(cin,name);
    // cout << "+----------------------------------------------------------------------------------+" << endl;
    int name_len=name.size();
    char nm[name_len];
    f(name_len) nm[i]=name[i];
    ssize_t n = write(sockfd,nm,name_len);
    if(n < 0) error("ERROR writing to socket");

    pthread_t t1,t2;
    pthread_create(&t1,NULL,Write,NULL);
    pthread_create(&t2,NULL,Read,NULL);
    pthread_join(t1,NULL);
    pthread_join(t2,NULL);

    close(sockfd);
    return 0;
}