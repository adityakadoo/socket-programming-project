#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <iostream>
#include <fstream>
#include <string.h>
#include <vector>
#include <map>
#include <set>
#include <utility>
#include <thread>

#define TRUE 1
#define FALSE 0

std::vector<std::string> split(std::string s, std::string delimiter = ",")
{
    size_t pos = 0;
    std::string token;
    std::vector<std::string> res;
    while ((pos = s.find(delimiter)) != std::string::npos)
    {
        token = s.substr(0, pos);
        res.push_back(token);
        s.erase(0, pos + delimiter.length());
    }
    return res;
}

void recv_routine(int new_socket, std::vector<std::string> *replies)
{
    int valread;
    char buffer[1025] = {0};

    valread = recv(new_socket, buffer, 1024, 0);
    buffer[valread] = '\0';
    replies->push_back(buffer);
}

void send_routine(int port, std::string *message)
{
    int sock = 0, valread1;
    struct sockaddr_in peer_addr;
    char buffer[1024] = {0};

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        printf("socket failure");
        exit(1);
    }

    peer_addr.sin_family = AF_INET;
    peer_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, "127.0.0.1", &peer_addr.sin_addr) <= 0)
    {
        printf("\nInvalid address/ Address not supported\n");
        exit(1);
    }

    while (connect(sock, (struct sockaddr *)&peer_addr, sizeof(peer_addr)) < 0)
    {
        sleep(1);
    }
    if (send(sock, message->c_str(), message->length(), 0) != message->length())
    {
        perror("send failure");
    }
}

int main(int argc, char *argv[])
{
    // reading config file and given folder

    if (argc < 3)
    {
        std::cout << "Usage : executable argument1-config-file argument2-directory-path\n";
        exit(1);
    }
    std::string path_to_file = argv[2];
    system(("ls " + path_to_file + " | sed \'s/ /\\n/g\'").c_str());

    std::string config_file = argv[1];
    std::ifstream fin(config_file);

    int clt_num, port, id, connection_n, file_n;
    fin >> clt_num >> port >> id;

    fin >> connection_n;
    std::vector<int> neighbours(connection_n);
    std::map<int, int> port_map;
    for (size_t i = 0; i < connection_n; i++)
    {
        int v;
        fin >> neighbours[i] >> v;
        port_map[neighbours[i]] = v;
    }
    std::vector<int> status(connection_n);
    for (size_t i = 0; i < connection_n; i++)
        status[i] = i;

    fin >> file_n;
    std::vector<std::string> files(file_n);
    for (size_t i = 0; i < file_n; i++)
        fin >> files[i];

    // setting up sockets

    std::string *message = new std::string();
    *message = std::to_string(id) + "," + std::to_string(clt_num) + ",";

    std::vector<std::string> *replies = new std::vector<std::string>();

    std::vector<std::thread *> send_threads(connection_n);
    for (size_t i = 0; i < connection_n; i++)
    {
        send_threads[i] = new std::thread(send_routine, port_map[neighbours[i]], message);
    }

    std::vector<std::thread *> recv_threads(connection_n);

    int OPT = TRUE;
    int master_socket, addrlen, new_socket;
    struct sockaddr_in address;

    if ((master_socket = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("socket failure");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(master_socket, SOL_SOCKET, SO_REUSEADDR, (char *)&OPT, sizeof(OPT)) < 0)
    {
        perror("setsockopt failure");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(master_socket, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("bind failure");
        exit(EXIT_FAILURE);
    }

    if (listen(master_socket, 3) < 0)
    {
        perror("listen failure");
        exit(EXIT_FAILURE);
    }

    addrlen = sizeof(address);

    for (size_t i = 0; i < connection_n; i++)
    {
        if ((new_socket = accept(master_socket, (struct sockaddr *)&address,
                                 (socklen_t *)&addrlen)) < 0)
        {
            perror("accept failure");
            exit(EXIT_FAILURE);
        }
        recv_threads[i] = new std::thread(recv_routine, new_socket, replies);
    }

    for (std::thread *t : send_threads)
        t->join();
    for (std::thread *t : recv_threads)
        t->join();

    std::map<int, std::string> id_map;
    for (std::string reply : *replies)
    {
        std::vector<std::string> reply_segments = split(reply);
        id_map[std::stoi(reply_segments[1])] = reply_segments[0];
    }
    for (std::pair<int, int> port_entry : port_map)
    {
        std::cout << "Connected with " + std::to_string(port_entry.first) + " with unique-ID " + id_map[port_entry.first] + " on port " + std::to_string(port_entry.second) + "\n";
    }
    return 0;
}
