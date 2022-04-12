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
#include <filesystem>

#define TRUE 1
#define FALSE 0
#define MAX_PEERS 15

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

std::string path_to_name(std::string path)
{
    size_t pos = 0;
    std::string token;
    while ((pos = path.find("/")) != std::string::npos)
    {
        token = path.substr(0, pos);
        path.erase(0, pos + 1);
    }
    return path;
}

class barrier
{
    int reached_count;
    int max_count;
    std::counting_semaphore<1> *count_protector;
    std::counting_semaphore<MAX_PEERS> *barrier_semaphore;
    std::counting_semaphore<1> *main_barrier;

public:
    bool running;
    barrier(int max)
    {
        reached_count = 0;
        max_count = max;
        count_protector = new std::counting_semaphore<1>(1);
        barrier_semaphore = new std::counting_semaphore<MAX_PEERS>(0);
        main_barrier = new std::counting_semaphore<1>(0);
        running = true;
    }
    ~barrier()
    {
        delete count_protector;
        delete barrier_semaphore;
        delete main_barrier;
    }

    void hit()
    {
        count_protector->acquire();
        reached_count++;
        count_protector->release();

        if (reached_count == max_count)
        {
            reached_count = 0;
            main_barrier->release(1);
        }

        barrier_semaphore->acquire();
    }
    void hit_main()
    {
        main_barrier->acquire();
    }
    void release()
    {
        barrier_semaphore->release(max_count);
    }
};

void server_routine(int clt_num, int port, int id, int n, std::vector<std::string> *replies, barrier *b)
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

    b->hit();
    while (b->running)
    {
        std::set<std::string> s;
        while (s.size() != n)
        {
            if ((new_socket = accept(master_socket, (struct sockaddr *)&address,
                                     (socklen_t *)&addrlen)) < 0)
            {
                perror("accept failure");
                exit(EXIT_FAILURE);
            }
            valread = recv(new_socket, buffer, 1024, 0);
            buffer[valread] = '\0';
            replies->push_back(buffer);
            close(new_socket);
            // std::cout << buffer << "\n";
            s.insert(split(buffer)[0]);
        }
        b->hit();
    }
}

void client_routine(int port, std::string *message, barrier *b)
{
    int sock = 0, valread1;
    struct sockaddr_in peer_addr;
    char buffer[1024] = {0};

    b->hit();
    while (b->running)
    {

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
        int tries = 0;
        while (connect(sock, (struct sockaddr *)&peer_addr, sizeof(peer_addr)) < 0 && tries < 5)
        {
            tries++;
            sleep(1);
        }
        if (send(sock, message->c_str(), message->length(), 0) != message->length())
        {
            perror("send failure");
        }
        close(sock);
        b->hit();
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

    std::string path_to_files = argv[2];
    std::map<std::string, std::string> dir;
    for (const auto &entry : std::filesystem::directory_iterator(path_to_files))
        dir[path_to_name(entry.path())] = "";

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

    barrier *b = new barrier(connection_n + 1);

    std::map<int, std::string *> message_map;
    std::string *message = new std::string();
    *message = std::to_string(id) + "," + std::to_string(clt_num) + ",";

    std::vector<std::string> *replies = new std::vector<std::string>();

    std::thread *server_thread = new std::thread(server_routine, clt_num, port, id, connection_n, replies, b);

    std::vector<std::thread *> threads(connection_n);
    for (size_t i = 0; i < connection_n; i++)
    {
        message_map[neighbours[i]] = new std::string(*message);
        threads[i] = new std::thread(client_routine, port_map[neighbours[i]], message_map[neighbours[i]], b);
    }

    b->hit_main();
    sleep(2);
    b->release();
    b->hit_main();
    sleep(2);

    std::map<int, std::string> id_map;
    std::map<std::string, int> clt_num_map;
    for (std::string reply : *replies)
    {
        std::vector<std::string> reply_segments = split(reply);
        id_map[std::stoi(reply_segments[1])] = reply_segments[0];
        clt_num_map[reply_segments[0]] = std::stoi(reply_segments[1]);
    }
    for (std::pair<int, int> port_entry : port_map)
    {
        std::cout << "Connected with " + std::to_string(port_entry.first) + " with unique-ID " + id_map[port_entry.first] + " on port " + std::to_string(port_entry.second) + "\n";
    }
    delete message;
    message = new std::string();
    *message = std::to_string(id) + ",";
    for (std::string file : files)
    {
        *message += file + ",";
    }
    for (std::pair<int, std::string *> message_entry : message_map)
    {
        *message_entry.second = *message;
    }
    replies->clear();
    std::map<std::string, std::string> files_info;

    b->release();
    b->hit_main();
    sleep(2);

    delete message;
    for (std::string reply : *replies)
    {
        std::vector<std::string> reply_segments = split(reply);
        int peer_clt_num = clt_num_map[reply_segments[0]];

        *message_map[peer_clt_num] = std::string(std::to_string(id) + ",");
        for (size_t i = 1; i < reply_segments.size(); i++)
            if (dir.find(reply_segments[i]) != dir.end())
                *message_map[peer_clt_num] += "y,";
            else
                *message_map[peer_clt_num] += "n,";
    }
    replies->clear();

    b->release();
    b->hit_main();
    sleep(2);

    for (std::string reply : *replies)
    {
        std::vector<std::string> reply_segments = split(reply);
        int peer_clt_num = clt_num_map[reply_segments[0]];
        *message_map[peer_clt_num] = "";

        for (size_t i = 1; i < reply_segments.size(); i++)
            if (reply_segments[i] == "y")
                if (files_info.find(files[i-1]) == files_info.end())
                    files_info[files[i-1]] = reply_segments[0];
                else if (std::stoi(files_info[files[i-1]]) < std::stoi(reply_segments[0]))
                    files_info[files[i-1]] = reply_segments[0];
    }

    b->running = false;
    b->release();

    for (std::thread *t : threads)
        t->join();
    server_thread->join();

    for (std::string file : files)
    {
        if (files_info.find(file) != files_info.end())
            std::cout << "Found " << file << " at " << files_info[file] << " with MD5 0 at depth 1\n";
        else
            std::cout << "Found " << file << " at 0 with MD5 0 at depth 0\n";
    }
    return 0;
}
