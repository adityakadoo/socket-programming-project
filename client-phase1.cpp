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
#include <fcntl.h>

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

std::vector<std::string> split_once(std::string s, std::string delimiter = ",")
{
    size_t pos = 0;
    std::string token;
    std::vector<std::string> res;
    if ((pos = s.find(delimiter)) != std::string::npos)
    {
        token = s.substr(0, pos);
        res.push_back(token);
        s.erase(0, pos + delimiter.length());
    }
    res.push_back(s);
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
    std::counting_semaphore<1> *print_protector;
    std::counting_semaphore<MAX_PEERS> *barrier_semaphore;
    std::counting_semaphore<1> *main_barrier;

public:
    bool running;
    int conv;
    barrier(int max)
    {
        reached_count = 0;
        max_count = max;
        conv = 0;
        count_protector = new std::counting_semaphore<1>(1);
        print_protector = new std::counting_semaphore<1>(1);
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

        if (reached_count == max_count)
        {
            reached_count = 0;
            conv++;
            main_barrier->release(1);
        }
        count_protector->release(1);

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
    void get_print()
    {
        print_protector->acquire();
    }
    void release_print()
    {
        print_protector->release(1);
    }
};

void recv_routine(int new_socket, std::vector<std::string> *replies, barrier *b)
{
    int valread;
    std::string m;
    std::vector<std::string> r;

    char buffer[1025];

    b->hit();
    while (b->running)
    {
        while (true)
        {
            valread = -1;
            while (valread < 0)
            {
                valread = recv(new_socket, buffer, 1024, 0);
                sleep(1);
            }
            buffer[valread] = '\0';
            r = split(buffer, ";");

            m = std::to_string(b->conv) + ";" + std::to_string(new_socket) + ";";
            while (send(new_socket, m.c_str(), m.length(), 0) != m.length())
            {
                sleep(1);
            }
            if (std::stoi(r[0]) == b->conv)
            {
                b->get_print();
                replies->push_back(r[1]);
                b->release_print();
                break;
            }
            else if (std::stoi(r[0]) > b->conv)
            {
                break;
            }
            else
            {
                sleep(1);
            }
        }
        b->hit();
    }
}

void send_routine(int port, std::string *message, barrier *b)
{
    int sock = 0, valread;
    struct sockaddr_in peer_addr;
    char buffer[1024] = {0};

    std::string m;
    std::vector<std::string> r;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        printf("socket failure");
        exit(1);
    }

    fcntl(sock, F_SETFL, O_NONBLOCK);
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

    b->hit();
    while (b->running)
    {
        while (true)
        {
            m = std::to_string(b->conv) + ";" + *message + ";";

            while (send(sock, m.c_str(), m.length(), 0) != m.length())
            {
                sleep(1);
            }
            valread = -1;
            while (valread < 0)
            {
                sleep(1);
                valread = recv(sock, buffer, 1024, 0);
            }
            buffer[valread] = '\0';
            r = split(buffer, ";");

            if (std::stoi(r[0]) >= b->conv)
            {
                break;
            }
        }
        b->hit();
    }
}

int main(int argc, char *argv[])
{

    if (argc < 3)
    {
        std::cout << "Usage : executable argument1-config-file argument2-directory-path\n";
        exit(1);
    }

    std::string path_to_files = argv[2];

    std::map<std::string, std::string> dir;
    for (const auto &entry : std::filesystem::directory_iterator(path_to_files))
        if (path_to_name(entry.path()) != "Downloaded")
            dir[path_to_name(entry.path())] = "";
    for (std::pair<std::string, std::string> file : dir)
        std::cout << file.first << std::endl;

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

    std::map<std::string, std::string> files_info;
    if (connection_n != 0)
    {
        barrier *b = new barrier(connection_n * 2);

        std::map<int, std::string *> message_map;
        std::string message = std::to_string(id) + "," + std::to_string(clt_num) + ",";

        std::vector<std::string> *replies = new std::vector<std::string>();

        std::vector<std::thread *> send_threads(connection_n);
        for (size_t i = 0; i < connection_n; i++)
        {
            message_map[neighbours[i]] = new std::string(message);
            send_threads[i] = new std::thread(send_routine, port_map[neighbours[i]], message_map[neighbours[i]], b);
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

        fcntl(master_socket, F_SETFL, O_NONBLOCK);
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
            while ((new_socket = accept(master_socket, (struct sockaddr *)&address,
                                        (socklen_t *)&addrlen)) < 0)
            {
                sleep(1);
            }
            fcntl(new_socket, F_SETFL, O_NONBLOCK);
            recv_threads[i] = new std::thread(recv_routine, new_socket, replies, b);
        }

        b->hit_main();
        b->release();
        b->hit_main();

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
            std::cout << "Connected to " + std::to_string(port_entry.first) + " with unique-ID " + id_map[port_entry.first] + " on port " + std::to_string(port_entry.second) + "\n"
                      << std::flush;
        }

        b->running = false;
        b->release();

        for (std::thread *t : send_threads)
            t->join();
        for (std::thread *t : recv_threads)
            t->join();
    }
    return 0;
}
