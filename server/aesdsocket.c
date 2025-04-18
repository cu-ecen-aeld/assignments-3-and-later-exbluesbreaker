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
#include <pthread.h>
#include <sys/queue.h>
#include <errno.h>

#define FILE_NAME "/var/tmp/aesdsocketdata"

int main_sockfd = -3;
int client_fd = -3;
int tmp_fd  = -3;

struct thread_data
{
    pthread_t id;
    bool complete;
    int fd;
    int tmp_fd;
    char ip_str[INET6_ADDRSTRLEN];
    SLIST_ENTRY(thread_data)
    entries;
};

SLIST_HEAD(thread_list_head, thread_data);
struct thread_list_head head;

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

void terminate_threads()
{
    struct thread_data *item = SLIST_FIRST(&head);
    while (item != NULL)
    {
        struct thread_data *next = SLIST_NEXT(item, entries);
        pthread_cancel(item->id);
        pthread_join(item->id, NULL);
        close(item->fd);
        close(item->tmp_fd);
        SLIST_REMOVE(&head, item, thread_data, entries);
        free(item);
        item = next;
    }
    pthread_mutex_destroy(&lock);
}

void handle_signals(int signum)
{
    if (signum == SIGALRM)
    {
        char outstr[200];
        time_t t;
        struct tm *tmp;

        t = time(NULL);
        tmp = localtime(&t);
        if (tmp == NULL)
        {
            perror("localtime");
            return;
        }
        strftime(outstr, sizeof(outstr), "timestamp: %a, %d %b %Y %T %z\n", tmp);
        pthread_mutex_lock(&lock);
        int status = write(tmp_fd, outstr, strlen(outstr));
        pthread_mutex_unlock(&lock);
        if (status == -1)
        {
            perror("Timestamp write failed");
        }
    }
    else
    {
        syslog(LOG_DEBUG, "Caught signal, exiting");
        terminate_threads(&head);
        shutdown(main_sockfd, SHUT_RDWR);
        close(main_sockfd);
        close(tmp_fd);
        unlink(FILE_NAME);
        exit(0);
    }
}

void *thread_function(void *arg)
{
    struct thread_data *item = (struct thread_data *)arg;
    int client_fd = item->fd;
    char buffer[100];
    /* Receive data */
    bool transfer_end = false;
    int status;
    while (!transfer_end)
    {
        int data_size = recv(client_fd, buffer, 100, 0);
        if (data_size == -1)
        {
            perror("Recv failed");
            return NULL;
        }
        buffer[data_size] = '\0';
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
        pthread_mutex_lock(&lock);
        status = write(item->tmp_fd, buffer, write_size);
        pthread_mutex_unlock(&lock);
        if (status == -1)
        {
            perror("File write failed");
            return NULL;
        }
    }
    /* Send data back */
    status = lseek(item->tmp_fd, 0, SEEK_SET);
    if (status == -1)
    {
        perror("Lseek failed");
        return NULL;
    }
    transfer_end = false;
    while (!transfer_end)
    {
        int data_size = read(item->tmp_fd, buffer, 100);
        if (data_size == -1)
        {
            perror("File read failed");
            return NULL;
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
                return NULL;
            }
        }
    }
    syslog(LOG_DEBUG, "Closed connection from %s", item->ip_str);
    item->complete = true;
    return NULL;
}

int main(int argc, char *argv[])
{
    SLIST_INIT(&head);
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
        else if (status != 0)
        {
            exit(0);
        }
    }
    unlink(FILE_NAME);
    tmp_fd = open(FILE_NAME, O_APPEND | O_CREAT | O_RDWR, 0755);
    if (tmp_fd == -1)
    {
        perror("File open failed");
        exit(-1);
    }
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

    if (sigaction(SIGALRM, &sa, NULL) == -1)
    {
        perror("Sigaction failed");
        exit(-1);
    }
    timer_t timer_id;
    struct itimerspec timer_spec;
    if (timer_create(CLOCK_REALTIME, NULL, &timer_id) == -1) 
    {
        perror("timer_create failed");
        exit(-1);
    }

    timer_spec.it_value.tv_sec = 10;           
    timer_spec.it_value.tv_nsec = 0;
    timer_spec.it_interval.tv_sec = 10; 
    timer_spec.it_interval.tv_nsec = 0;

    // Set the timer
    if (timer_settime(timer_id, 0, &timer_spec, NULL) == -1) {
        perror("timer_settime failed");
        exit(1);
    }
    while (1)
    {
        struct sockaddr_storage their_addr;
        socklen_t addr_size;
        status = listen(main_sockfd, 1);
        if (status == -1)
        {
            perror("Listen failed");
            terminate_threads(&head);
            exit(-1);
        }
        addr_size = sizeof(their_addr);
        while((client_fd = accept(main_sockfd, (struct sockaddr *)&their_addr, &addr_size)) == -1)
        {
            if (errno == EINTR)
            {
                continue; 
            }
            else
            {
                perror("Accept failed");
                terminate_threads(&head);
                exit(-1);
            }
        }
        struct thread_data *new_item = malloc(sizeof(struct thread_data));
        if (new_item == NULL)
        {
            perror("Malloc failed");
            terminate_threads(&head);
            exit(-1);
        }
        if (their_addr.ss_family == AF_INET)
        { // IPv4
            struct sockaddr_in *s = (struct sockaddr_in *)&their_addr;
            inet_ntop(AF_INET, &s->sin_addr, new_item->ip_str, sizeof(new_item->ip_str));
            syslog(LOG_DEBUG, "Accepted connection from %s", new_item->ip_str);
        }
        else if (their_addr.ss_family == AF_INET6)
        { // IPv6
            struct sockaddr_in6 *s = (struct sockaddr_in6 *)&their_addr;
            inet_ntop(AF_INET6, &s->sin6_addr, new_item->ip_str, sizeof(new_item->ip_str));
            syslog(LOG_DEBUG, "Accepted connection from %s", new_item->ip_str);
        }
        new_item->complete = false;
        new_item->fd = client_fd;
        new_item->tmp_fd = open(FILE_NAME, O_APPEND | O_RDWR, 0755);
        if (new_item->tmp_fd == -1)
        {
            perror("File open failed");
            free(new_item);
            terminate_threads(&head);
            exit(-1);
        }
        SLIST_INSERT_HEAD(&head, new_item, entries);

        status = pthread_create(&new_item->id, NULL, thread_function, new_item);
        if (status != 0)
        {
            perror("Thread creation failed");
            terminate_threads(&head);
            exit(-1);
        }
        struct thread_data *item = SLIST_FIRST(&head);
        while (item != NULL)
        {
            struct thread_data *next = SLIST_NEXT(item, entries);
            if (item->complete)
            {
                pthread_join(item->id, NULL);
                close(item->fd);
                close(item->tmp_fd);
                SLIST_REMOVE(&head, item, thread_data, entries);
                free(item);
            }
            item = next;
        }
    }
    close(tmp_fd);
    close(main_sockfd);
    closelog();
}
