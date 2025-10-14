#pragma once
#include <vector>


#ifndef TERMINAL_SNAKE_MULTYPLAYER_NETWORK_H
#define TERMINAL_SNAKE_MULTYPLAYER_NETWORK_H

#ifdef _WIN32
//woindows specific imports and defines
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib") // link the windows networking libs apperently

#define socketDscriptor SOCKET
#define socketFailure INVALID_SOCKET

#else
//non windows imports and defines

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <signal.h>
#include <netdb.h>
#include <unistd.h>

#define socketDscriptor int
#define socketFailure (-400)
#endif

#define INPUT_NETWORK_BUFFER_SIZE 32768


//cross platform things can go here
socketDscriptor startServerSocket_native(int port) {
#ifdef _WIN32
    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == INVALID_SOCKET) {
        std::cerr << "Socket creation failed "<< WSAGetLastError() <<std::endl;
        WSACleanup();
        return INVALID_SOCKET;
    }

    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(static_cast<u_short>(port));

    //bind the socket
    if (bind(serverSocket, (sockaddr*)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR) {
        std::cerr << "Bind failed.\n";
        closesocket(serverSocket);
        WSACleanup();
        return INVALID_SOCKET;
    }

    listen(serverSocket, SOMAXCONN);

    return serverSocket;
#else
    addrinfo addressInfo{}, *result;
    addressInfo.ai_family = AF_INET;
    addressInfo.ai_socktype = SOCK_STREAM;
    addressInfo.ai_flags = AI_PASSIVE;//use my ip i guess
    constexpr int yes = 1;

    std::string portString = std::to_string(port);

    if (int rv; (rv = getaddrinfo(nullptr,portString.c_str(),&addressInfo, &result)) != 0) {
        std::cerr << "get address info error: " << gai_strerror(rv) << std::endl;
        return socketFailure;//failure
    }

    // loop through all the results and bind to the first we can
    int socketFileDescriptor = socketFailure;
    addrinfo *p;
    for(p  = result; p != nullptr; p = p->ai_next) {
        //if this one failes then move on to the next
        if ((socketFileDescriptor = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }
        //if it did not fail,
        //make sure this address is not already in use i guess
        if (setsockopt(socketFileDescriptor, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            perror("setsockopt");
            exit(1);
        }

        //attempt to bind to the socket, if it fails then close the connection and try the next one
        if (bind(socketFileDescriptor, p->ai_addr, p->ai_addrlen) == -1) {
            close(socketFileDescriptor);
            perror("server: bind");
            continue;
        }

        break;
    }
    freeaddrinfo(result);//prevent memory leaks
    if (p == nullptr) {
        std::cerr << "Server failed to bind" << std::endl;
        return socketFailure;
    }

    //tell the computer to listen for incoming connections
    listen(socketFileDescriptor, 5);

    return socketFileDescriptor;
#endif
}

socketDscriptor acceptIncomingConnection_native(socketDscriptor serverSocket) {
#ifdef _WIN32
    sockaddr_in clientAddress;
    int clientAddressLength = sizeof(clientAddress);
    SOCKET clientSocket = accept(serverSocket, (sockaddr*)&clientAddress, &clientAddressLength);
    if (clientSocket == INVALID_SOCKET) {
        std::cerr << "Accept failed.\n";
        closesocket(serverSocket);
        WSACleanup();
        return INVALID_SOCKET;
    }
    closesocket(serverSocket);
    return clientSocket;
#else
    sockaddr_storage theirAddress{};
    socklen_t theirAddressSize = sizeof(theirAddress);
    socketDscriptor clientSocket = accept(serverSocket, reinterpret_cast<sockaddr*>(&theirAddress), &theirAddressSize);
    if (clientSocket == -1) {
        std::cerr << "Failed to accept connection" << std::endl;
        perror("accept");
        return socketFailure;
    }
    //optionally close the server socket here
    close(serverSocket);
    return clientSocket;

#endif
}

void closeSocket_native(socketDscriptor socket) {
#ifdef _WIN32
    if (socket != INVALID_SOCKET) {
        closesocket(socket);
    }
#else
    if (socket != socketFailure && socket != 0){
        close(socket);
    }
    #endif
}

socketDscriptor connectToServer_native(const std::string &ip,int port) {
#ifdef _WIN32
    sockaddr_in serverAddress;
    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket == INVALID_SOCKET) {
        std::cerr << "Socket creation failed.\n";
        WSACleanup();
        return INVALID_SOCKET;
    }

    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = inet_addr(ip.c_str());
    serverAddress.sin_port = htons(port);

    //the connection
    if (connect(clientSocket, (sockaddr*)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR) {
        std::cerr << "Connection failed.\n";
        closesocket(clientSocket);
        WSACleanup();
        return INVALID_SOCKET;
    }

    return clientSocket;
#else
    addrinfo addressInfo{}, *serverInfo;
    addressInfo.ai_family = AF_UNSPEC;
    addressInfo.ai_socktype = SOCK_STREAM;

    std::string portString = std::to_string(port);

    if (int rv; (rv = getaddrinfo(ip.c_str(),portString.c_str(),&addressInfo, &serverInfo)) != 0) {
        std::cerr << "get address info error: " << gai_strerror(rv) << std::endl;
        return socketFailure;//failure
    }

    int socketFileDescriptor = socketFailure;
    addrinfo *p;
    // loop through all the results and connect to the first we can
    for(p = serverInfo; p != nullptr; p = p->ai_next) {
        if ((socketFileDescriptor = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("client: socket");
            continue;
        }

        //attempt tp connect
        if (connect(socketFileDescriptor, p->ai_addr, p->ai_addrlen) == -1) {
            //if the connection fails then move to the next one
            perror("client: connect");
            close(socketFileDescriptor);
            continue;
        }

        break;
    }
    freeaddrinfo(serverInfo);
    if (p == nullptr) {
        std::cerr << "Socket failed to connect" << std::endl;
        return socketFailure;
    }
    return socketFileDescriptor;
#endif
}


void sendData_native(socketDscriptor socket, const uint8_t * data, size_t length) {
#ifdef _WIN32
    send(socket,reinterpret_cast<const char *>(data),static_cast<int>(length),0);
#else
    send(socket, data, length * sizeof(uint8_t), 0);
#endif
}

long receiveData_native(socketDscriptor socket, uint8_t *dataBuffer, size_t length) {
#ifdef _WIN32
    return recv(socket,reinterpret_cast<char *>(dataBuffer),static_cast<int>(length),0);
#else
    return recv(socket, dataBuffer, length * sizeof(uint8_t), 0);
#endif
}

class SocketInterface {

    bool isServer;
    bool connected = false;
    std::string ip;
    int port;
    socketDscriptor clientSocket;
    uint8_t dataBuffer[INPUT_NETWORK_BUFFER_SIZE];

    public:
        SocketInterface(std::string &ip, int port) {
            isServer = false;
            this->ip = ip;
            this->port = port;
#ifdef _WIN32
            //the IDE is showing an error here but it builds fine sooooooooooooo
            WSADATA wsaData;
            int result;//init the windows network lib
            if ((result = WSAStartup(MAKEWORD(2, 2), &wsaData))) {
                std::cerr << "WSAStartup failed. "<<result<<std::endl;
                return;
            }
#endif
        }

        explicit SocketInterface(int port) {
            isServer = true;
            this->port = port;
#ifdef _WIN32
            //the IDE is showing an error here but it builds fine sooooooooooooo
            WSADATA wsaData;
            int result;//init the windows network lib
            if ((result = WSAStartup(MAKEWORD(2, 2), &wsaData))) {
                std::cerr << "WSAStartup failed. "<<result<<std::endl;
                return;
            }
#endif
        }

        bool connect() {
            //start listening or try to connect to the host
            if (isServer) {
                //crate the server socket
                socketDscriptor serverSocket = startServerSocket_native(port);
                if (serverSocket == socketFailure) {
                    std::cerr << "Failed to create server!" << std::endl;
                    return false;
                }
                std::cout << "Server created, waiting for connection" << std::endl;
                //connect, getting the client socket
                clientSocket = acceptIncomingConnection_native(serverSocket);
                if (clientSocket == socketFailure) {
                    std::cerr << "Failed to accept connection" << std::endl;
                    return false;
                }
                connected = true;
                return true;
            } else {
                clientSocket = connectToServer_native(ip, port);
                if (clientSocket == socketFailure) {
                    std::cerr << "Failed to connect to server!" << std::endl;
                    return false;
                }
                connected = true;
                return true;
            }
        }

        [[nodiscard]] bool isConnected() const {
            return connected;
        }

        void send(const std::vector<uint8_t>& data) {
            if (!connected) {
                std::cerr << "attempted to send data but socket is not connected" << std::endl;
                return;
            }
            //send the data
            sendData_native(clientSocket,data.data(),data.size());
        }

        std::vector<uint8_t> receive() {
            if (!connected) {
                //if not connected then just return nothing
                return {};
            }
            //read the incomming data
            long bytesRecieved = receiveData_native(clientSocket, dataBuffer, sizeof(dataBuffer));
            if (bytesRecieved == 0 || bytesRecieved == -1) {
                connected = false;
                return {};
            }
            //package the data into a vector
            std::vector<uint8_t> data(bytesRecieved);
            for (int i=0;i<bytesRecieved;i++) {
                data[i] = dataBuffer[i];
            }

            return data;
        }

        void close() const {
            closeSocket_native(clientSocket);
            connected = false;
        }
};

#endif //TERMINAL_SNAKE_MULTYPLAYER_NETWORK_H