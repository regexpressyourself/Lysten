/* lab1-client.c
 * Sam Messina
 * CS 407
 */

#define _POSIX_SOURCE
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#define SECRET     "cs407rembash\n"
#define PORT       4070
#define INPUT_SIZE 4096

int error_check(int return_val, char *error_msg, int sockfd);
int setup_socket(char *ip_addr);
int handle_protocol(int sockfd);
void handle_stdin_to_sock(int sockfd);
void handle_sock_to_stdout(int sockfd);

int main(int argc, char *argv[])
{
    /* Main Driver
     * 1. connect to and wait for server
     * 2. exchange secret word
     * 3. fork
     * 4. one fork reads from stdin and sends to socket
     * 5. other fork reads from socket and sends to stdout
     * 6. exit terminates client
     */

    int    sockfd = 0;          // file descriptor for socket

    // get argument from command line
    if (argc != 2) {
        fprintf(stderr, "Please specify an IP address\n");
        exit(EXIT_FAILURE);
    }

    sockfd = setup_socket(argv[1]);

    handle_protocol(sockfd);

    // fork off for input/output
    switch(fork()) {
        case -1:
            error_check(-1, 
                    "Oops. Fork failure.\nDid you try a spoon?", 
                    sockfd);
        case 0:
            // child process reads from stdin and writes to socket
            while (1) {
                handle_stdin_to_sock(sockfd);
            }
        default:
            // parent reads from socket and writes to stdout
            while (1) {
                handle_sock_to_stdout(sockfd);
            }
    }

    // We don't technically need to close socket since program 
    // termination will do it for us, but might as well to be safe.
    close(sockfd);
    // Success!
    exit(EXIT_SUCCESS);
}

void handle_stdin_to_sock(int sockfd)
{
    int    bytes_read;          // hold how many bytes were read from socket
    char   buffer[INPUT_SIZE];  // general-purpose buffer to hold strings
    // read from stdin
    error_check((bytes_read = read(STDIN_FILENO, &buffer, sizeof(buffer))) < 0, 
            "Oops. Read failure.\n", 
            sockfd);
    // write to socket
    error_check(write(sockfd, buffer, bytes_read) < 0, 
            "Oops. Write failure.\n", 
            sockfd);
}

void handle_sock_to_stdout(int sockfd)
{
    int    bytes_read;          // hold how many bytes were read from socket
    char   buffer[INPUT_SIZE];  // general-purpose buffer to hold strings
    // read from socket
    error_check((bytes_read = read(sockfd, &buffer, sizeof(buffer))), 
            "Oops. Read failure.\n", 
            sockfd);

    // if socket returns empty string, close client
    if (bytes_read == 0) {
        close(sockfd);
        exit(EXIT_SUCCESS);
    }

    else {
        // write socket data to stdout
        error_check(write(STDOUT_FILENO, buffer, bytes_read) < 0, 
                "Oops. Write failure.\n", 
                sockfd);
    }
}

int handle_protocol(int sockfd) 
{
    int    bytes_read;          // hold how many bytes were read from socket
    char   buffer[INPUT_SIZE];  // general-purpose buffer to hold strings
    memset(buffer, 0, sizeof(buffer));
    // wait for "<rembash>" from server
    error_check((bytes_read = read(sockfd, &buffer, sizeof(buffer))) < 0, 
            "Oops. Read failure.\n", 
            sockfd);

    // send secret to server
    error_check(write(sockfd, SECRET, strlen(SECRET)) < 0, 
            "Oops. Write failure.\n", 
            sockfd);

    // get either the "<ok>" or "<error>" response from the server
    error_check(bytes_read = read(sockfd, &buffer, sizeof(buffer)) < 0, 
            "Oops. Read failure.\n", 
            sockfd);
    error_check(write(STDOUT_FILENO, buffer, bytes_read) < 0, 
            "Oops. Write failure.\n", 
            sockfd);

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
        fprintf(stderr, "Invalid address\n");
        exit(EXIT_FAILURE);
    }

    // connect socket to server
    error_check(connect(sockfd, (struct sockaddr *)&address, sizeof(address)),
            "Oops. Client could not connect.\n", 
            sockfd);
    return sockfd;

}

int error_check(int return_val, char *error_msg, int sockfd) 
{
    /* error_check serves to make the code more readable. 
     * If the return value is negative:
     *   1. an error message is spit out
     *   2. the socket is closed, and
     *   3. the program exits with EXIT_FAILURE.
     */

    if ( return_val < 0 ) {
        perror(error_msg);
        if (sockfd) { 
            close(sockfd);
        };
        exit(EXIT_FAILURE);
    }
    return return_val; 
}

