/* lab2-client.c
 * Sam Messina
 * CS 407
 */

#define _DEFAULT_SOURCE
#include <termios.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>

#define SECRET     "cs407rembash\n"
#define PORT       4070
#define INPUT_SIZE 4096

struct termios tty;

int  setup_socket(char *ip_addr);
int  handle_protocol(int sockfd);
int handle_stdin_to_sock(int sockfd);
int handle_sock_to_stdout(int sockfd);
int set_non_canon_mode(int fd, struct termios *prev_tty);
void sigchld_handler(int signal);
void restore_tty_settings();
void exit_gracefully(int exit_status);

int main(int argc, char *argv[])
{

    int   sockfd = 0;          // file descriptor for socket
    int   temp_fd;
    pid_t pid;
    struct sigaction act;
    act.sa_handler = sigchld_handler;
    act.sa_flags = 0;
    sigemptyset(&act.sa_mask);

    if (sigaction(SIGCHLD, &act, NULL) < 0) {
        perror("Oops. Could not make signal handler");
        exit(EXIT_FAILURE); 
    }

    // get argument from command line
    if (argc != 2) {
        fprintf(stderr, "Please specify an IP address\n");
        exit(EXIT_FAILURE);
    }
    sockfd = setup_socket(argv[1]);


    if (set_non_canon_mode(STDIN_FILENO, &tty) < 0 ) {
        perror("Could not set canonical mode");
    }

    if (handle_protocol(sockfd) < 0) {
        close(sockfd);
        exit(1);
    }

    // fork off for input/output
    pid = fork();
    switch(pid) {
        case -1:
                perror("Oops. Fork failure.\nDid you try a spoon?"); 
                close(sockfd);
        case 0:
            // child process reads from stdin and writes to socket
            while (1) {
                temp_fd = handle_stdin_to_sock(sockfd); 
                if (temp_fd < 0) {
                    pid = getppid();
                    kill(pid, SIGTERM);
                    exit(1);
                }
                else if (temp_fd == 0) {
                    pid = getppid();
                    kill(pid, SIGTERM);
                    exit(0);
                }
                else {continue;}
            }
        default:
            // parent reads from socket and writes to stdout
            while (1) {
                temp_fd = handle_sock_to_stdout(sockfd);
                if (temp_fd < 0) {
                    kill(pid, SIGTERM);
                    exit(1);
                }
                else if (temp_fd == 0) {
                    kill(pid, SIGTERM);
                    exit(0);
                }
                else {continue;}
            }
    }

    // We don't technically need to close socket since program 
    // termination will do it for us, but might as well to be safe.
    close(sockfd);
    // Success!
    exit(EXIT_SUCCESS);
}

int handle_stdin_to_sock(int sockfd)
{
    int    bytes_read;          // hold how many bytes were read from socket
    char   buffer[INPUT_SIZE];  // general-purpose buffer to hold strings
    // read from stdin
    bytes_read = read(STDIN_FILENO, &buffer, sizeof(buffer));
    if (bytes_read < 0){ 
        #ifdef DEBUG
        perror("Oops. Read failure from STDIN to socket.\n"); 
        #endif
        close(sockfd);
        return -1;
    }
    else if (bytes_read == 0 ) {
        #ifdef DEBUG
        printf("Read 0 bytes from STDIN.\n"); 
        #endif
        close(sockfd);
        return 0;
    }
    // write to socket
    if (write(sockfd, buffer, bytes_read) < 0){ 
        perror("Oops. Write failure.\n"); 
        close(sockfd);
        return -1;
    }
    return 1;
}

int handle_sock_to_stdout(int sockfd)
{
    int    bytes_read;          // hold how many bytes were read from socket
    char   buffer[INPUT_SIZE];  // general-purpose buffer to hold strings
    // read from socket
    bytes_read = read(sockfd, &buffer, sizeof(buffer));
    if (bytes_read < 0){ 
        perror("Oops. Read failure from socket to STDOUT.\n"); 
        close(sockfd);
        return -1;
    }

    // if socket returns empty string, return 0
    else if (bytes_read == 0 ) {
        #ifdef DEBUG
        printf("Read 0 bytes from socket.\n"); 
        #endif
        close(sockfd);
        return 0;
    }

    else {
        // write socket data to stdout
        if (write(STDOUT_FILENO, buffer, bytes_read) < 0){ 
            perror("Oops. Write failure.\n"); 
            close(sockfd);
            return -1;
        }
    }
    return 1;
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
        return -1;
    }

    // send secret to server
    if (write(sockfd, SECRET, strlen(SECRET)) < 0){ 
        perror("Oops. Write failure.\n"); 
        close(sockfd);
        return -1;
    }

    // get either the "<ok>" or "<error>" response from the server
    if ((bytes_read = read(sockfd, &buffer, sizeof(buffer))) < 0){ 
        perror("Oops. Read failure in handle_protocol <ok>.\n"); 
        close(sockfd);
        return -1;
    }
    if (write(STDOUT_FILENO, buffer, bytes_read) < 0){ 
        perror("Oops. Write failure.\n"); 
        close(sockfd);
        return -1;
    }

    return 1;
}

int setup_socket(char *ip_addr) 
{
    int    sockfd = 0;          // file descriptor for socket
    struct sockaddr_in address; // address of server

    // set up socket
    sockfd             = socket(AF_INET, SOCK_STREAM, 0);
    address.sin_family = AF_INET;
    address.sin_port   = htons(PORT);

    if (inet_aton(ip_addr, &address.sin_addr) == 0) {
        #ifdef DEBUG
        fprintf(stderr, "Invalid address\n");
        #endif
        exit(EXIT_FAILURE);
    }

    // connect socket to server
    if (connect(sockfd, (struct sockaddr *)&address, sizeof(address))){
        perror("Oops. Client could not connect.\n"); 
        close(sockfd);
    }
    return sockfd;

}

int set_non_canon_mode(int fd, struct termios *prev_tty)
{
    struct termios temp_tty;

    if (tcgetattr(fd, &temp_tty) == -1){
        perror("problem in tcgeattr\n");
        return -1;
    }

    if (prev_tty != NULL)
        *prev_tty = temp_tty;

    temp_tty.c_lflag &= ~(ICANON | ECHO);
    temp_tty.c_lflag |= ISIG;
    temp_tty.c_iflag &= ~ICRNL;
    temp_tty.c_cc[VMIN] = 1;
    temp_tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSAFLUSH, &temp_tty) == -1){
        perror("problem in tcsetattr\n");
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

  if (exit_status==EXIT_FAILURE || 
          !WIFEXITED(childstatus) || 
          WEXITSTATUS(childstatus) != EXIT_SUCCESS)
    exit(EXIT_FAILURE);

  exit(EXIT_SUCCESS);
}
