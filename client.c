/* lab1-client.c
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

#define SECRET     "cs407rembash\n"
#define PORT       4070
#define INPUT_SIZE 4096

struct termios tty;

int  error_check(int return_val, char *error_msg, int sockfd);
int  setup_socket(char *ip_addr);
int  handle_protocol(int sockfd);
int handle_stdin_to_sock(int sockfd);
int handle_sock_to_stdout(int sockfd);
int set_non_canonical_mode(int sockfd, struct termios *tty);
int set_canonical_mode(int fd, struct termios *tty);
int ttySetCbreak(int fd, struct termios *prevTermios);

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
    int read_write_return_val;
    pid_t pid;
    // get argument from command line
    if (argc != 2) {
    // TODO remove
        sockfd = setup_socket("127.0.0.1");
        //fprintf(stderr, "Please specify an IP address\n");
        //exit(EXIT_FAILURE);
    }

    // TODO remove
    else
        sockfd = setup_socket(argv[1]);


    if (ttySetCbreak(STDIN_FILENO, &tty) < 0 ) {
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
                read_write_return_val = handle_stdin_to_sock(sockfd); 
                if (read_write_return_val < 0) {
                    pid = getppid();
                    kill(pid, SIGTERM);
                    exit(1);
                }
                else if (read_write_return_val == 0) {
                    pid = getppid();
                    kill(pid, SIGTERM);
                    exit(0);
                }
                else {continue;}
            }
        default:
            // parent reads from socket and writes to stdout
            while (1) {
                read_write_return_val = handle_sock_to_stdout(sockfd);
                if (read_write_return_val < 0) {
                    kill(pid, SIGTERM);
                    exit(1);
                }
                else if (read_write_return_val == 0) {
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
        perror("Oops. Read failure from STDIN to socket.\n"); 
        close(sockfd);
        return -1;
    }
    else if (bytes_read == 0 ) {
        printf("Read 0 bytes from STDIN.\n"); 
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
        printf("Read 0 bytes from socket.\n"); 
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
        fprintf(stderr, "Invalid address\n");
        exit(EXIT_FAILURE);
    }

    // connect socket to server
    if (connect(sockfd, (struct sockaddr *)&address, sizeof(address))){
        perror("Oops. Client could not connect.\n"); 
        close(sockfd);
    }
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

int set_non_canonical_mode(int fd, struct termios *tty)
{
    struct termios t;
    if (tcgetattr(fd, &t) == -1)
        return -1;
    if (tty != NULL)
        *tty = t;
    t.c_lflag &= ~(ICANON | ISIG | IEXTEN | ECHO);
    /* Noncanonical mode, disable signals, extended
       input processing, and echoing */
    t.c_iflag &= ~(BRKINT | ICRNL | IGNBRK | IGNCR | INLCR |
            INPCK | ISTRIP | IXON | PARMRK);
    /* Disable special handling of CR, NL, and BREAK.
       No 8th-bit stripping or parity error handling.
       Disable START/STOP output flow control. */
    t.c_oflag &= ~OPOST; /* Disable all output processing */
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0; /* Character-at-a-time input */
    /* with blocking */
    if (tcsetattr(fd, TCSAFLUSH, &t) == -1)
        return -1;
    return 0;
}
int set_canonical_mode(int fd, struct termios *tty)
{
    struct termios t;
    if (tcgetattr(fd, &t) == -1)
        return -1;
    if (tty != NULL)
        *tty = t;
    t.c_lflag &= ~(ICANON | ECHO);
    t.c_lflag |= ISIG;
    t.c_iflag &= ~ICRNL;
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    /* Character-at-a-time input */
    /* with blocking */
    if (tcsetattr(fd, TCSAFLUSH, &t) == -1)
        return -1;
    return 0;
}

int ttySetCbreak(int fd, struct termios *prevTermios)
{
    struct termios t;
    printf("%d",fd);
    if (tcgetattr(fd, &t) == -1){
        perror("problem in first getattr\n");
        return -1;
    }
    if (prevTermios != NULL)
        *prevTermios = t;
    t.c_lflag &= ~(ICANON | ECHO);
    t.c_lflag |= ISIG;
    t.c_iflag &= ~ICRNL;
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    /* Character-at-a-time input */
    /* with blocking */
    if (tcsetattr(fd, TCSAFLUSH, &t) == -1){
        perror("problem in second getattr\n");
        return -1;
    }
    return 0;
}
