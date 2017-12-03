/* lab2-server.c
 * Sam Messina
 * CS 407
 */
#define _XOPEN_SOURCE 600
#include <sys/types.h>
#include <termios.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <stdio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <libgen.h>
#include <fcntl.h>

#define SECRET "cs407rembash\n"
#define PORT 4070
#define INPUT_SIZE 4096

int  error_check(int return_val, char *error_msg, int sockfd);
void handle_client(int client_sockfd);
int  setup_socket(void);
int  setup_pty(void);
int  get_client_sockfd(int server_sockfd);
int  handle_protocol(int client_sockfd);
int  setup_slave_pty(char *slave_pty_name);
int  from_client_to_pty(int client_sockfd, int master_pty_fd);
int  from_pty_to_client(int client_sockfd, int master_pty_fd);
int  handle_pty_master(int client_sockfd, int master_pty_fd);

int main()
{
    int   server_sockfd;
    pid_t pid;
    int   client_sockfd = 0;

    // setup our server
    server_sockfd = setup_socket();
    while(1) {
        // server set up and started
        #ifdef DEBUG
        printf("server waiting\n");
        #endif

        // accept a new client
        client_sockfd = get_client_sockfd(server_sockfd);

        if (client_sockfd) {
            // as soon as a client comes in, fork off 
            // a new process for it
            pid = fork();

            switch (pid) {
                case -1:
                    perror("Oops. Fork call failure. Did you try a spoon?\n");
                    close(client_sockfd);
                    break;

                case 0: // child
                    // the server socket is closed in the child, 
                    // as it is no longer needed
                    close(server_sockfd);

                    // child handles the new client
                    handle_client(client_sockfd);
                    break;

                default: // parent
                    // parent closes socket and returns back to main,
                    // waiting for new connections
                    close(client_sockfd);
                    break;
            }
        }
    }
    return EXIT_FAILURE;
}


int handle_protocol(int client_sockfd) {
    /* Handles the rembash protocol step 
     * in the server 
     */

    const char * const rembash_string = "<rembash>\n";
    const char * const ok_string      = "<ok>\n";
    const char * const error_string   = "<error>\n";
    int  bytes_read;
    char buffer[INPUT_SIZE];

    // send "<rembash>" to the client upon connection
    if (write(client_sockfd, rembash_string, strlen(rembash_string))< 0){
        perror("Oops. Error writing to client.\n"); 
        close(client_sockfd);
    }

    // wait to read the secret word from client
    if ((bytes_read = read(client_sockfd, buffer, sizeof(buffer)))< 0){
        perror("Oops. Error reading from client.\n"); 
        close(client_sockfd);
    }

    // if the secret is incorrect, close the connection
    if (strcmp(buffer, SECRET) != 0) {
        if (write(client_sockfd, error_string, strlen(error_string)) < 0){
            perror("Oops. Error writing to socket\n"); 
        }
        close(client_sockfd);
        return -1;
    }

    // if the secret was correct, send <ok>
    if (write(client_sockfd, ok_string, strlen(ok_string)) < 0){
        perror("Oops. Error writing to client.\n"); 
        close(client_sockfd);
    }
    return 1;
}

void handle_client(int client_sockfd) {
    /* handle_client is called after a 
     * connection with the client has been created.
     *
     * client_sockfd is the file descriptor for the socket.
     */

    char  buffer[INPUT_SIZE];
    int   master_pty_fd;
    int   return_val;
    char  *slave_pty_name;
    pid_t pid;
    memset(buffer, 0, sizeof(buffer));

    // handle rembash protocol
    if (handle_protocol(client_sockfd) == -1) {
        close(client_sockfd);
        exit(EXIT_FAILURE);
    }

    // run all the setup functions for the master pty
    if ((master_pty_fd = setup_pty()) < 0) {
        perror("Error setting up PTY\n");
    }

    // get the slave pty name from the master pty
    slave_pty_name = ptsname(master_pty_fd);
    if (slave_pty_name == NULL ){
        #ifdef DEBUG
        printf("getting ptsname failed\n");
        #endif
    }

    // create a new subprocess for bash
    pid = fork();
    switch(pid) {
        case -1:
            perror("Oops. Fork failure.\nDid you try a spoon?"); 
            close(client_sockfd);
            exit(1);
            break;
        case 0: // child
            // PTY Slave begins here

            // close child's connection to master pty and client socket
            close(master_pty_fd);
            close(client_sockfd);

            // setup the slave pty
            setup_slave_pty(slave_pty_name);

            // should not get here if all goes well.
            // kill off errant processes if we do get here.
            pid = getppid();
            kill(pid, SIGTERM);
            exit(1);
            break;

        default: // parent
            // PTY Master begins here

            // the parent handles all the master pty fun
            return_val = handle_pty_master(client_sockfd, master_pty_fd);
            if (return_val < 0) {
                kill(pid, SIGTERM);
                exit(1);
            }
            break;

    }
    exit(EXIT_FAILURE);
    return;
}

int handle_pty_master(int client_sockfd, int master_pty_fd) 
{
    /* Take care of the read/write loops between 
     * the client socket and the master PTY
     */
    int return_val;
    pid_t  pid;

    // fork off for read/write logic
    pid = fork();
    switch (pid) {
        case -1:
            perror("Oops. Fork failure.\nDid you try a spoon?"); 
            return -1;
            break;
        case 0: // child
            while (1){
                // read from client socket, write to master PTY
                return_val = from_client_to_pty(client_sockfd, master_pty_fd);
                if (return_val < 0) {
                    // if from_client_to_pty returns -1, an error occurred

                    // kill the parent
                    pid = getppid();
                    kill(SIGTERM, pid);

                    // close the file descriptors
                    close(client_sockfd);
                    close(master_pty_fd);

                    // exit
                    exit(1);
                    return -1;
                }
                else if (return_val == 0) {
                    // if from_client_to_pty returns 0, 0 bytes 
                    // were read from client, indicating the 
                    // client has closed

                    // kill parent
                    pid = getppid();
                    kill(SIGTERM, pid);

                    // close FDs
                    close(client_sockfd);
                    close(master_pty_fd);

                    // exit
                    exit(0);
                    return 0;
                }
                // if all is good, keep reading/writing
                else {continue;}
            }
            break;
        default: // parent
            while (1){
                // read from PTY, write to the client socket
                return_val = from_pty_to_client(client_sockfd, master_pty_fd);
                if (return_val < 0) {
                    // if from_pty_to_client returns -1, an error occurred

                    close(client_sockfd);
                    close(master_pty_fd);

                    kill(pid, SIGTERM);

                    exit(1);
                    return -1;
                }
                else if (return_val == 0) {
                    // if from_pty_to_client returns 0, 0 bytes 
                    // were read from PTY, indicating the 
                    // connection has closed

                    close(client_sockfd);
                    close(master_pty_fd);

                    kill(pid, SIGTERM);

                    exit(0);
                    return 0;
                }
                else {continue;}
            }
            break;
    }
}

int from_client_to_pty(int client_sockfd, int master_pty_fd) {
    /* Read data from the client socket, and write 
     * it to a master PTY FD.
     *
     * returns: 
     *      0 when 0 bytes read, 
     *      1 on successful read, and 
     *      -1 on error
     */
    char  buffer[INPUT_SIZE];

    // read from our client
    int bytes_read = read(client_sockfd, buffer, sizeof(buffer));
    if (bytes_read < 0) {
        perror("cannot read from client\n");
        return -1;
    }
    if (bytes_read == 0) {
        #ifdef DEBUG
        printf("Read 0 bytes from client\n");
        #endif
        return 0;
    }
    else {
        // write to socket
        if (write(master_pty_fd, buffer, bytes_read) < 0) {
            perror("Oops. Write failure.\n");
            return -1;
        }
    }
    return 1;
}


int from_pty_to_client(int client_sockfd, int master_pty_fd) {
    /* Read data from the master PTY, and write 
     * it to the client socket.
     *
     * returns: 
     *      0 when 0 bytes read, 
     *      1 on successful read, and 
     *      -1 on error
     */
    char  buffer[INPUT_SIZE];
    int bytes_read = read(master_pty_fd, buffer, sizeof(buffer));

    if (bytes_read < 0) {
        return -1;
    }

    // if PTY returns empty string, return 0
    if (bytes_read == 0) {
        return 0;
    }
    else {
        // write PTY data to client
        if (write(client_sockfd, buffer, bytes_read) < 0) {
            perror("Oops. Write failure.\n");
            return -1;
        }
    }
    return 1;
}

int setup_slave_pty(char *slave_pty_name) {
    /* Sets up the slave PTY to redirect STDIN, 
     * STDOUT, and STDERR, and run bash.
     */

    int slave_pty_fd;

    // create a new session
    if (setsid() < 0) {
        perror("Could not set SID\n");
        return -1;
    }

    // get the slave PTY FD
    if ((slave_pty_fd = open(slave_pty_name, O_RDWR)) < 0) {
        perror("Oops. Could not open slave PTY FD");
        return -1;
    }

    // redirect in, out, and error
    for (int i = 0; i < 3; i++) {
        if (dup2(slave_pty_fd, i) < 0) {
            perror("Oops. Dup2 error.\n");
            return -1;
        }
    }

    // run bash
    if (execlp("bash","bash",NULL) < 0) {
        perror("Oops. Exec error.\n");
        return -1;
    }

    // shouldn't get here
    return -1;
}

int setup_pty(void) {
    /* Sets up the master PTY by calling 
     * the functions listed in the book
     */
    // open PTY
    int master_pty_fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (master_pty_fd < 0) {
        perror("master pty failed\n");
        return -1;
    }

    // grant PTY
    if ((grantpt(master_pty_fd)) < 0 ) {
        perror("granpty failed\n");
        return -1;
    }

    // unlock PTY
    if ((unlockpt(master_pty_fd)) < 0) {
        perror("unlockpt failed\n");
        return -1;
    }

    // send back the FD
    return master_pty_fd;
}

int get_client_sockfd(int server_sockfd)
{
    /* Accepts new clients and returns 
     * their FD
     */
    int       client_sockfd = 0;
    socklen_t client_len;
    struct    sockaddr_in client_address;

    client_len    = sizeof(client_address);

    // seperate fd for each client
    client_sockfd = accept(server_sockfd,
            (struct sockaddr *)&client_address, 
            &client_len);

    if (client_sockfd < 0) {
        perror("Oops. Error accepting connection");
        close(client_sockfd);
    }
    return client_sockfd;
}

int setup_socket(void) 
{
    /* Performs the heavy lifting to get the 
     * server off the ground
     */
    int       server_sockfd;
    int       client_sockfd = 0;
    socklen_t server_len;
    struct    sockaddr_in server_address;
    int option = 1;

    server_sockfd                  = socket(AF_INET, SOCK_STREAM, 0);
    server_address.sin_family      = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port        = htons(PORT);
    server_len                     = sizeof(server_address);
    setsockopt(server_sockfd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));

    // use address for socket address to avoid pass by value
    if (bind(server_sockfd, (struct sockaddr *)&server_address, server_len)< 0) {
        perror("could not bind server");
        exit(1);
    }

    // listen with a backlog of 5 connections
    if ((listen(server_sockfd, 5) < 0)) { 
        perror("Oops. Error \n"); 
        close(client_sockfd);
    }

    if ((signal(SIGCHLD, SIG_IGN) == SIG_ERR)) { 
        perror("Oops. Error \n"); 
        close(client_sockfd);
    }
    return server_sockfd;
}

