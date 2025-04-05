#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <syslog.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

#define FILE_NAME "/var/tmp/aesdsocketdata"

int main_sockfd = -3;
int client_fd = -3;
int tmp_fd = -3;

void handle_signals(int)
{
    syslog(LOG_DEBUG, "Caught signal, exiting");
    shutdown(main_sockfd, SHUT_RDWR);
    close(tmp_fd);
    close(client_fd);
    close(main_sockfd);
    unlink(FILE_NAME);
    exit(0);
}

int main(int argc, char *argv[])
{
    bool daemon_mode = false;
    if (argc > 1)
    {
        if (strcmp(argv[1], "-d") == 0)
        {
            daemon_mode = true;
        }
    }
    openlog(argv[0], LOG_PID | LOG_CONS, LOG_USER);
    int status;
    struct addrinfo hints;
    struct addrinfo *servinfo; // will point to the results

    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));

    sa.sa_handler = handle_signals;

    // Block all signals while in the handler
    sigfillset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL) == -1)
    {
        perror("Sigaction failed");
        exit(-1);
    }

    if (sigaction(SIGTERM, &sa, NULL) == -1)
    {
        perror("Sigaction failed");
        exit(-1);
    }

    memset(&hints, 0, sizeof hints); // make sure the struct is empty
    hints.ai_family = AF_UNSPEC;     // don't care IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // fill in my IP for me

    if ((status = getaddrinfo(NULL, "9000", &hints, &servinfo)) != 0)
    {
        printf("getaddrinfo error: %s\n", gai_strerror(status));
        exit(-1);
    }
    main_sockfd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if (main_sockfd == -1)
    {
        perror("Socket error");
        exit(-1);
    }
    int opt = 1;
    if (setsockopt(main_sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("setsockopt");
        close(main_sockfd);
        exit(-1);
    }
    status = bind(main_sockfd, servinfo->ai_addr, servinfo->ai_addrlen);
    if (status == -1)
    {
        perror("Bind failed");
        exit(-1);
    }
    if (daemon_mode)
    {
        status = fork();
        if (status == -1)
        {
            perror("Fork in daemon mode failed");
            exit(-1);
        }
        else if (status !=0 )
        {
            exit(0);
        }
    }
    while (1)
    {
        struct sockaddr_storage their_addr;
        socklen_t addr_size;
        status = listen(main_sockfd, 1);
        if (status == -1)
        {
            perror("Listen failed");
            exit(-1);
        }
        addr_size = sizeof(their_addr);
        client_fd = accept(main_sockfd, (struct sockaddr *)&their_addr, &addr_size);
        if (client_fd == -1)
        {
            perror("Accept failed");
            exit(-1);
        }
        char ip_str[INET6_ADDRSTRLEN];
        if (their_addr.ss_family == AF_INET)
        { // IPv4
            struct sockaddr_in *s = (struct sockaddr_in *)&their_addr;
            inet_ntop(AF_INET, &s->sin_addr, ip_str, sizeof(ip_str));
            syslog(LOG_DEBUG, "Accepted connection from %s", ip_str);
        }
        else if (their_addr.ss_family == AF_INET6)
        { // IPv6
            struct sockaddr_in6 *s = (struct sockaddr_in6 *)&their_addr;
            inet_ntop(AF_INET6, &s->sin6_addr, ip_str, sizeof(ip_str));
            syslog(LOG_DEBUG, "Accepted connection from %s", ip_str);
        }
        tmp_fd = open(FILE_NAME, O_APPEND | O_CREAT | O_RDWR, 0755);
        if (tmp_fd == -1)
        {
            perror("File open failed");
            exit(-1);
        }
        char buffer[100];
        /* Receive data */
        bool transfer_end = false;
        while (!transfer_end)
        {
            int data_size = recv(client_fd, buffer, 100, 0);
            if (data_size == -1)
            {
                perror("Recv failed");
                exit(-1);
            }
            int write_size = data_size;
            for (int i = 0; i < data_size; i++)
            {
                if (buffer[i] == '\n')
                {
                    write_size = i + 1;
                    transfer_end = true;
                    break;
                }
            }
            status = write(tmp_fd, buffer, write_size);
            if (status == -1)
            {
                perror("File write failed");
                exit(-1);
            }
        }
        /* Send data back */
        status = lseek(tmp_fd, 0, SEEK_SET);
        if (status == -1)
        {
            perror("Lseek failed");
            exit(-1);
        }
        transfer_end = false;
        while (!transfer_end)
        {
            int data_size = read(tmp_fd, buffer, 100);
            if (data_size == -1)
            {
                perror("File read failed");
                exit(-1);
            }
            if (data_size == 0)
            {
                /* End of file */
                transfer_end = true;
            }
            else
            {
                status = send(client_fd, buffer, data_size, 0);
                if (status == -1)
                {
                    perror("Send failed");
                    exit(-1);
                }
            }
        }
        close(client_fd);
        syslog(LOG_DEBUG, "Closed connection from %s", ip_str);
        close(tmp_fd);
    }
    closelog();
}