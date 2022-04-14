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
#include <csignal>
#include <streambuf>
#include <sys/sendfile.h>

#define TRUE 1
#define FALSE 0
#define MAX_PEERS 15

std::string path_to_files;

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

std::string get_exec(const char *cmd)
{
    char buffer[128];
    std::string result = "";
    FILE *pipe = popen(cmd, "r");
    if (!pipe)
        throw std::runtime_error("popen() failed!");
    try
    {
        while (fgets(buffer, sizeof buffer, pipe) != NULL)
        {
            result += buffer;
        }
    }
    catch (...)
    {
        pclose(pipe);
        throw;
    }
    pclose(pipe);
    return result;
}

int client_fd(std::string dir, std::string path_to_send)
{ // probably need to make the client_routine return the socket to use it here

    // std::vector<const char*> dir_char;

    std::string file_path = path_to_send + dir;
    FILE *fp;
    fp = fopen(file_path.c_str(), "r");

    if (fp == NULL)
    {
        std::cout << "Error in reading the file" << std::endl;
    }

    int fileds = fileno(fp);

    // send_file(fp,sock);

    return fileds;
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
    barrier(int max)
    {
        reached_count = 0;
        max_count = max;
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
    void get_print()
    {
        print_protector->acquire();
    }
    void release_print()
    {
        print_protector->release();
    }
};

void recv_routine(int new_socket, std::vector<std::string> *replies, barrier *b)
{
    int valread;
    int conv = 0;
    std::string m;
    std::vector<std::string> r;

    char buffer[1025];

    b->hit();
    while (b->running)
    {
        while (true)
        {
            valread = recv(new_socket, buffer, 1024, 0);
            buffer[valread] = '\0';
            r = split(buffer, ";");
            // b->get_print();
            // std::cout << valread << "," << buffer << "\n";
            // b->release_print();
            if (valread == 0)
            {
                m = std::to_string(-1) + ";" + std::to_string(new_socket) + ";";
                send(new_socket, m.c_str(), m.length(), 0);
                break;
            }

            std::string src = split(r[1])[0];

            m = std::to_string(conv) + ";" + std::to_string(new_socket) + ";";
            send(new_socket, m.c_str(), m.length(), 0);

            if (std::stoi(r[0]) == conv)
            {
                replies->push_back(r[1]);
                // b->get_print();
                // std::cout << buffer << " -> kept\n"
                //           << std::flush;
                // b->release_print();
                break;
            }
            else
            {
                // b->get_print();
                // std::cout << buffer << " -> discarded\n"
                //           << std::flush;
                // b->release_print();
                sleep(2);
            }
        }
        b->hit();
        conv++;
    }

    sleep(1);
    valread = recv(new_socket, buffer, 1024, 0);
    buffer[valread] = '\0';
    int nfiles = std::stoi(std::string(buffer));
    // b->get_print();
    // std::cout << "nfiles : " << nfiles << "\n"
    //           << std::flush;
    // b->release_print();

    m = "ACK";
    send(new_socket, m.c_str(), m.length(), 0);
    sleep(1);

    for (size_t i = 0; i < nfiles; i++)
    {
        sleep(1);
        valread = recv(new_socket, buffer, 1024, 0);
        buffer[valread] = '\0';
        r = split(buffer);
        // b->get_print();
        // std::cout << valread << "," << r[0] << "," << r[1] << "," << r.size() << "\n"
        //           << std::flush;
        // b->release_print();
        int file_size = std::stoi(r[0]);

        std::string file_name = r[1];

        m = "ACK";
        send(new_socket, m.c_str(), m.length(), 0);

        int rec_bytes = 0;

        std::string path = path_to_files + "Downloaded/" + file_name;
        std::ofstream down_file(path);

        while (rec_bytes < file_size)
        {
            valread = recv(new_socket, buffer, 1024, 0);
            // std::cout << "reading" << valread << "\n";
            buffer[valread] = '\0';

            rec_bytes += valread;
            down_file.write(buffer, valread);

            // b->get_print();
            // std::cout << rec_bytes << "/" << file_size << "          \n" << std::flush;
            // b->release_print();
        }

        down_file.close();

        m = "ACK";
        send(new_socket, m.c_str(), m.length(), 0);

        // replies->push_back(file_name + "," + temp);
    }
    // close(new_socket);
    sleep(5);
    // std::cout << "recv-bye\n"
    //           << std::flush;
}

void send_routine(int port, std::string *message, barrier *b)
{
    int sock = 0, valread;
    struct sockaddr_in peer_addr;
    char buffer[1024] = {0};

    int conv = 0;
    std::string m;
    std::vector<std::string> r;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        // b->get_print();
        printf("socket failure");
        // b->release_print();
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
        // b->get_print();
        // std::cout << "connecting to " << port << "\r" << std::flush;
        // b->release_print();
        sleep(1);
    }

    b->hit();
    while (b->running)
    {
        while (true)
        {
            m = std::to_string(conv) + ";" + *message + ";";

            if (send(sock, m.c_str(), m.length(), 0) != m.length())
            {
                perror("send failure");
            }
            valread = recv(sock, buffer, 1024, 0);
            buffer[valread] = '\0';
            // std::cout << buffer << "\n";
            r = split(buffer, ";");

            if (std::stoi(r[0]) == conv)
            {
                // b->get_print();
                // std::cout << conv << " accpected at " << r[1] << "\n"
                //           << std::flush;
                // b->release_print();
                break;
            }
            // b->get_print();
            // std::cout << conv << " rejected at " << r[1] << "\n"
            //           << std::flush;
            // b->release_print();
            // sleep(5);
        }
        b->hit();
        conv++;
    }

    std::vector<std::string> file_desc = split(*message);
    int nfiles = file_desc.size() / 3;
    m = std::to_string(nfiles);
    send(sock, m.c_str(), m.length(), 0);
    sleep(1);
    valread = recv(sock, buffer, 1024, 0);
    buffer[valread] = '\0';

    // b->get_print();
    // std::cout << "\n"
    //           << *message << " + " << file_desc.size() << "\n"
    //           << std::flush;
    // b->release_print();
    for (size_t i = 0; i < file_desc.size(); i = i + 3)
    {
        m = file_desc[i + 1] + "," + file_desc[i + 2] + ",";
        send(sock, m.c_str(), m.length(), 0);
        sleep(1);
        valread = recv(sock, buffer, 1024, 0);
        buffer[valread] = '\0';
        // b->get_print();
        // std::cout << valread << "," << buffer << "\n"
        //           << std::flush;
        // b->release_print();

        int file_size = std::stoi(file_desc[i + 1]);
        int sen_bytes = 0;
        while (sen_bytes < file_size)
        {
            int sent = sendfile(sock, std::stoi(file_desc[i]), (off_t *)&sen_bytes, std::stoi(file_desc[i + 1]));
            sen_bytes += sent;
        }
        sleep(1);
        valread = recv(sock, buffer, 1024, 0);
        buffer[valread] = '\0';
    }

    // close(sock);
    sleep(5);
    // std::cout << "send-bye\n"
    //           << std::flush;
}

int main(int argc, char *argv[])
{
    // reading config file and given folder

    if (argc < 3)
    {
        std::cout << "Usage : get_executable argument1-config-file argument2-directory-path\n";
        exit(1);
    }

    // signal(SIGPIPE, SIG_IGN);
    path_to_files = argv[2];
    system(("ls -p " + path_to_files + " | grep -v '/$' | sed \'s/ /\\n/g\'").c_str());

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

    std::string clt_num_str = std::to_string(clt_num);

    // std::string path_to_down = "../sample-data/files/client" + clt_num_str + "/Downloaded/";
    // std::string path_to_send = "../sample-data/files/client" + clt_num_str;

    barrier *b = new barrier(connection_n * 2);

    std::map<int, std::string *> message_map;
    std::string *message = new std::string();
    *message = std::to_string(id) + "," + std::to_string(clt_num) + ",";

    std::vector<std::string> *replies = new std::vector<std::string>();

    std::vector<std::thread *> send_threads(connection_n);
    for (size_t i = 0; i < connection_n; i++)
    {
        message_map[neighbours[i]] = new std::string(*message);
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
        recv_threads[i] = new std::thread(recv_routine, new_socket, replies, b);
    }

    b->hit_main();
    // std::cout << "==============================\n";
    sleep(2);
    b->release();
    b->hit_main();
    // std::cout << "==============================\n";
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
    // std::cout << "==============================\n";
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
    // std::cout << "==============================\n";
    sleep(2);

    for (std::string reply : *replies)
    {
        std::vector<std::string> reply_segments = split(reply);
        int peer_clt_num = clt_num_map[reply_segments[0]];
        *message_map[peer_clt_num] = std::string(std::to_string(id) + ",");

        for (size_t i = 1; i < reply_segments.size(); i++)
            if (reply_segments[i] == "y")
                if (files_info.find(files[i - 1]) == files_info.end())
                    files_info[files[i - 1]] = reply_segments[0];
                else if (std::stoi(files_info[files[i - 1]]) < std::stoi(reply_segments[0]))
                    files_info[files[i - 1]] = reply_segments[0];
    }

    replies->clear();

    for (std::pair<std::string, std::string> files : files_info)
    {
        int peer_clt_num = clt_num_map[files.second];
        *message_map[peer_clt_num] += files.first + ",";
    }

    b->release();
    b->hit_main();
    // std::cout << "==============================\n";
    sleep(2);

    for (std::string reply : *replies)
    {
        std::vector<std::string> reply_segments = split(reply);
        int peer_clt_num = clt_num_map[reply_segments[0]];

        *message_map[peer_clt_num] = "";
        for (size_t i = 1; i < reply_segments.size(); i++)
        {
            std::string file_path = path_to_files + reply_segments[i];
            std::filesystem::path p{file_path};
            // std::cout << client_fd(reply_segments[i], path_to_files) << "\n"
            //           << std::flush;
            *message_map[peer_clt_num] += std::to_string(client_fd(reply_segments[i], path_to_files)) + "," + std::to_string((int)std::filesystem::file_size(p)) + "," + reply_segments[i] + ",";
        }
    }

    replies->clear();

    b->running = false;
    b->release();

    //*message = client_fsend(file,,path_to_send);

    for (std::thread *t : send_threads)
        t->join();
    for (std::thread *t : recv_threads)
        t->join();

    // for (std::string reply : *replies)
    // {
    //     std::vector<std::string> reply_fields = split_once(reply);
    // }

    for (std::string file : files)
    {
        if (files_info.find(file) != files_info.end())
        {
            // int file_descript;
            // unsigned long file_size;
            // char *file_buffer;

            // file_descript = client_fd(file, path_to_files + "Downloaded/");

            std::string file_path = path_to_files + "Downloaded/" + file;
            // std::filesystem::path p{file_path};
            // file_size = std::filesystem::file_size(p);
            // std::string hash;
            std::string hash = split_once(get_exec(("md5sum " + file_path + " | grep -o \"^[0-9a-f]*\"").c_str()), "\n")[0];
            // file_buffer = (char *)mmap(0, file_size, PROT_READ, MAP_SHARED, file_descript, 0);
            // MD5((unsigned char *)file_buffer, file_size, (unsigned char *)hash.c_str());
            // munmap(file_buffer, file_size);

            std::cout << "Found " << file << " at " << files_info[file] << " with MD5 " << hash << " at depth 1\n";
        }
        else
            std::cout << "Found " << file << " at 0 with MD5 0 at depth 0\n";
    }
    return 0;
}
