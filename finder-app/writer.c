#include <fcntl.h>
#include <stdio.h>
#include <syslog.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    int fd;    
    openlog(argv[0], LOG_PID | LOG_CONS, LOG_USER);
    if (argc != 3) {
        printf("Only %d arguments were provided! Usage: %s <filename> <text>\n", argc,argv[0]);
        syslog(LOG_ERR, "Invalid number of arguments");
        closelog();
        return 1;
    }
    fd = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        syslog(LOG_ERR, "Failed to open file");
        closelog();
        return 1;
    }
    int status = write(fd, argv[2], strlen(argv[2]));
    if (status == -1) {
        syslog(LOG_ERR, "Failed to write to file");
        closelog();
        return 1;
    }
    syslog(LOG_DEBUG, "Writing %s to %s", argv[2], argv[1]);
    close(fd);
    closelog();
}