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
#include <utility>
#include <thread>

#define TRUE 1
#define FALSE 0

void server_routine(int clt_num, int port, int id, int n)
{
    int OPT = TRUE;
    int master_socket, addrlen, new_socket, valread;
    struct sockaddr_in address;

    char buffer[1025];

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

    for (size_t i = 0; i < n; i++)
    {
        if ((new_socket = accept(master_socket, (struct sockaddr *)&address,
                                 (socklen_t *)&addrlen)) < 0)
        {
            perror("accept failure");
            exit(EXIT_FAILURE);
        }
        valread = recv(new_socket, buffer, 1024, 0);
        buffer[valread] = '\0';
        std::cout << buffer << "\n";
    }
}

void client_routine(std::pair<int, int> neighbour, std::string *message)
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
    peer_addr.sin_port = htons(neighbour.second);

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
    std::vector<std::pair<int, int>> neighbours(connection_n);
    for (size_t i = 0; i < connection_n; i++)
        fin >> neighbours[i].first >> neighbours[i].second;
    std::vector<int> status(connection_n);
    for (size_t i = 0; i < connection_n; i++)
        status[i] = i;

    fin >> file_n;
    std::vector<std::string> files(file_n);
    for (size_t i = 0; i < file_n; i++)
        fin >> files[i];

    // setting up sockets

    std::string *message = new std::string();
    *message = "Connected with " + std::to_string(clt_num) + " with unique-ID " + std::to_string(id) + " on port " + std::to_string(port);

    std::thread *server_thread = new std::thread(server_routine, clt_num, port, id, connection_n);

    std::vector<std::thread *> threads(connection_n);
    for (size_t i = 0; i < connection_n; i++)
    {
        threads[i] = new std::thread(client_routine, neighbours[i], message);
    }

    for (std::thread *t : threads)
        t->join();
    server_thread->join();
    return 0;
}
