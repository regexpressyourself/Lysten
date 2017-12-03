/* lab5-client.c
 * Sam Messina
 * CS 407
 */

#define _DEFAULT_SOURCE
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define INPUT_SIZE 4096
#define PORT       4070
#define SECRET     "<cs407rembash>\n"

char*  get_ip_addr(int argc, char *argv[]);
int    setup_signal_handler();
void   handle_data_transfer(int from_fd, int to_fd, pid_t pid);
int    set_non_canon_mode(int fd, struct termios *prev_tty);
void   sigchld_handler(int signal);
void   restore_tty_settings();
void   exit_gracefully(int exit_status);
int    connect_to_server(char *ip_addr);
int    handle_protocol(int sockfd);

struct termios tty;

int main(int argc, char *argv[])
{
    int   sockfd = 0;          // file descriptor for socket
    char* ip_addr;
    pid_t pid;

    // get argument from command line
    ip_addr = get_ip_addr(argc, argv);

    // setup signal handler
    setup_signal_handler();

    // connect to server
    sockfd = connect_to_server(ip_addr);

    // set non_canonical mode
    set_non_canon_mode(STDIN_FILENO, &tty);

    // handle protocol with server
    handle_protocol(sockfd);

    // fork off for input/output
    pid = fork();

    switch(pid) {
        case -1:
            perror("Oops. Fork failure.\nDid you try a spoon?"); 
            close(sockfd);
        case 0:
            // child process reads from stdin and writes to socket
            pid = getppid();
            while (1) {
                handle_data_transfer(STDIN_FILENO, sockfd, pid); 
            }
        default:
            // parent reads from socket and writes to stdout
            while (1) {
                handle_data_transfer(sockfd, STDOUT_FILENO, pid); 
            }
    }

    // We don't technically need to close socket since program 
    // termination will do it for us, but might as well to be safe.
    close(sockfd);
    // Success!
    exit(EXIT_SUCCESS);
}

char* get_ip_addr(int argc, char *argv[]) 
{
    char* ip_addr;
#ifndef DEBUG
    if (argc != 2) {
        fprintf(stderr, "Please specify an IP address\n");
        exit(EXIT_FAILURE);
    }
    ip_addr = argv[1];
#endif

#ifdef DEBUG
    ip_addr = "127.0.0.1";
#endif
    return ip_addr;
}

int connect_to_server(char *ip_addr) 
{
    int    sockfd = 0;          // file descriptor for socket
    struct sockaddr_in address; // address of server

    // set up socket
    sockfd             = socket(AF_INET, SOCK_STREAM, 0);
    address.sin_family = AF_INET;
    address.sin_port   = htons(PORT);

    // set up address from string
    if (inet_aton(ip_addr, &address.sin_addr) == 0) {
#ifdef DEBUG
        fprintf(stderr, "Error: Invalid address\n");
#endif
        exit(EXIT_FAILURE);
    }

    // connect socket to server
    if (connect(sockfd, (struct sockaddr *)&address, sizeof(address))){
        perror("Oops. Error calling connect.\n"); 
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    return sockfd;
}

int setup_signal_handler()
{
    struct sigaction act;
    act.sa_handler = sigchld_handler;
    act.sa_flags = 0;
    sigemptyset(&act.sa_mask);

    if (sigaction(SIGCHLD, &act, NULL) < 0) {
        perror("Oops. Could not make signal handler");
        exit(EXIT_FAILURE);
        return -1;
    }
    return 1;
}

void handle_data_transfer(int from_fd, int to_fd, pid_t pid)
{
    int    bytes_read;          // hold how many bytes were read from socket
    char   buffer[INPUT_SIZE];  // general-purpose buffer to hold strings

    memset(buffer, 0, sizeof(buffer));
    // read from stdin
    bytes_read = read(from_fd, &buffer, sizeof(buffer));

    if (bytes_read < 0){ 
#ifdef DEBUG
        fprintf(stderr, "Oops. Read failure from %d to %d.\n", from_fd, to_fd); 
#endif
        kill(pid, SIGTERM);
        exit(1);
    }
    else if (bytes_read == 0 ) {
#ifdef DEBUG
        fprintf(stderr, "Read 0 bytes from %d.\n", from_fd); 
#endif
        kill(pid, SIGTERM);
        exit(0);
    }
    else {
    }
    // write to socket
    if (write(to_fd, buffer, bytes_read) < 0){ 
        fprintf(stderr, "Oops. Write failure to %d.\n", to_fd); 
        kill(pid, SIGTERM);
        exit(1);
    }
    else {
    }
    return;
}

int handle_protocol(int sockfd) 
{
    int    bytes_read;          // hold how many bytes were read from socket
    char   buffer[INPUT_SIZE];  // general-purpose buffer to hold strings
    memset(buffer, 0, sizeof(buffer));
    // wait for "<rembash>" from server
    if ((bytes_read = read(sockfd, &buffer, sizeof(buffer))) < 0){ 
        perror("Oops. Read failure in handle_protocol <rembash>.\n"); 
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // send secret to server
    if (write(sockfd, SECRET, strlen(SECRET)) < 0){ 
        perror("Oops. Write failure sending secret.\n"); 
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // get either the "<ok>" or "<error>" response from the server
    if ((bytes_read = read(sockfd, &buffer, sizeof(buffer))) < 0){ 
        perror("Oops. Read failure in handle_protocol <ok>.\n"); 
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    if (write(STDOUT_FILENO, buffer, bytes_read) < 0){ 
        perror("Oops. Write failure.\n"); 
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    return 1;
}

int set_non_canon_mode(int fd, struct termios *prev_tty)
{
    struct termios temp_tty;

    if (tcgetattr(fd, &temp_tty) == -1){
        perror("Error getting terminal attributes\n");
        perror("Warning: Could not set non canonical mode\n");
        return -1;
    }

    if (prev_tty != NULL)
        *prev_tty = temp_tty;

    temp_tty.c_lflag    &= ~(ICANON | ECHO);
    temp_tty.c_lflag    |= ISIG;
    temp_tty.c_iflag    &= ~ICRNL;
    temp_tty.c_cc[VMIN]  = 1;
    temp_tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSAFLUSH, &temp_tty) == -1){
        perror("Error setting terminal attributes\n");
        perror("Warning: Could not set non canonical mode\n");
        return -1;
    }
    return 0;
}

void sigchld_handler(int signal)
{
    exit_gracefully(EXIT_SUCCESS);
}

void restore_tty_settings()
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &tty) == -1) {
        perror("Oops. Could not restoring TTY attributes ");
        exit(EXIT_FAILURE); 
    }

    return;
}

void exit_gracefully(int exit_status)
{
    restore_tty_settings();

    int childstatus;
    wait(&childstatus);

    if (exit_status==EXIT_FAILURE || !WIFEXITED(childstatus) || 
            WEXITSTATUS(childstatus) != EXIT_SUCCESS) {
        exit(EXIT_FAILURE);
    }

    exit(EXIT_SUCCESS);
}
