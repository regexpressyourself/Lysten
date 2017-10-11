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
#include <pthread.h>
#include <sys/wait.h>
#include <sys/epoll.h>

#define SECRET "cs407rembash\n"
#define PORT 4070
#define INPUT_SIZE 4096
#define MAX_BUF 1000
#define MAX_EVENTS 1000

int  error_check(int return_val, char *error_msg, int sockfd);
void * handle_client(void*arg);
void * epoll_wait_loop(void*arg);
int  setup_socket(void);
int  setup_pty(void);
int  get_client_sockfd(int server_sockfd);
int  handle_protocol(int client_sockfd);
int  setup_slave_pty(char *slave_pty_name);
int  from_client_to_pty(int client_sockfd, int master_pty_fd);
int  from_pty_to_client(int client_sockfd, int master_pty_fd);
int handle_pty_master(int client_sockfd, int master_pty_fd);

int epoll_fds[MAX_EVENTS*2 + 5];
int epfd;
int main()
{

    epfd = epoll_create1(EPOLL_CLOEXEC);

    int   server_sockfd;
    int   client_sockfd = 0;
    int *client_sockfd_ptr;
    pthread_t threadid;
    // setup our server
    server_sockfd = setup_socket();

    pthread_create(&threadid, NULL, epoll_wait_loop, NULL);
    signal(SIGCHLD,SIG_IGN);
    while(1) {
        // server set up and started
        #ifdef DEBUG
        printf("server waiting\n");
        #endif
        client_sockfd_ptr = malloc(sizeof(int));
        // accept a new client
        client_sockfd = get_client_sockfd(server_sockfd);
        if (client_sockfd < 0) {continue;};
        *client_sockfd_ptr = client_sockfd;
        pthread_create(&threadid, NULL, handle_client, client_sockfd_ptr);

    }
    return EXIT_FAILURE;
}

void * epoll_wait_loop(void*arg){
    int ready;
    int sfd;
    int bytes_read;
    int i = 0;
    char buf[MAX_BUF];
    struct epoll_event evlist[MAX_EVENTS];
    while (1) {
        memset(buf,0, sizeof(buf));
        ready = epoll_wait(epfd, evlist, MAX_EVENTS, -1);

        for (i = 0; i<ready; i++) {

            printf(" fd=%d; events: %s%s%s\n", evlist[i].data.fd,
                    (evlist[i].events & EPOLLIN) ? "EPOLLIN " : "",
                    (evlist[i].events & EPOLLHUP) ? "EPOLLHUP " : "",
                    (evlist[i].events & EPOLLERR) ? "EPOLLERR " : "");

            if ((evlist[i].events & EPOLLHUP) || 
                    (evlist[i].events & EPOLLERR) ) {
                // in the event of error...

                // get the error'ed file descriptor (sfd)
                sfd = evlist[i].data.fd;

                // close the hung thread (sfd)
                printf("closing %d\n", sfd);
                if (close(sfd) == -1 ) {
                    printf("error closing fd: %d\n", sfd);
                }
                else {
                    printf("closed fd: %d\n", sfd);
                }

                // close hung thread's compatriot 
                // (the value at epoll_fsd[sfd])
                printf("closing %d\n", epoll_fds[sfd]);
                if (close(epoll_fds[sfd]) == -1 ) {
                    printf("error closing fd: %d\n", epoll_fds[sfd]);
                }
                else {
                    printf("closed fd: %d\n", epoll_fds[sfd]);
                }
            }
            if (evlist[i].events & EPOLLIN) {
                sfd = evlist[i].data.fd;
                printf("epoll fd: %d\n", sfd);
                bytes_read = read(sfd, buf, sizeof(buf));
                if (bytes_read == -1) {
                    perror("error on read");
                }
                else if (bytes_read == 0) {
                if (close(epoll_fds[sfd]) == -1 ) {
                    printf("error closing fd: %d\n", epoll_fds[sfd]);
                }
                else {
                    printf("closed fd: %d\n", epoll_fds[sfd]);
                }
                if (close(sfd) == -1 ) {
                    printf("error closing fd: %d\n", sfd);
                }
                else {
                    printf("closed fd: %d\n", sfd);
                }
                }
                bytes_read = write(epoll_fds[sfd], buf, sizeof(buf));
                if (bytes_read == -1) {
                    perror("error on write");
                }

            };
        }
    }
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
    printf(" handle_protocol'sclient fd: %d\n",client_sockfd);

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

void * handle_client(void*arg) {
    /* handle_client is called after a 
     * connection with the client has been created.
     *
     * client_sockfd is the file descriptor for the socket.
     */

    int client_sockfd = *(int*)arg;
    char  buffer[INPUT_SIZE];
    int   master_pty_fd;
    int   return_val;
    char  *slave_pty_name;
    pid_t pid;
    memset(buffer, 0, sizeof(buffer));

    // handle rembash protocol
    if (handle_protocol(client_sockfd) == -1) {
        printf("Error in handle_protocol()");
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

            int slave_pty_fd;

            // create a new session
            if (setsid() < 0) {
                perror("Could not set SID\n");
                exit(EXIT_FAILURE);
            }

            // get the slave PTY FD
            if ((slave_pty_fd = open(slave_pty_name, O_RDWR)) < 0) {
                perror("Oops. Could not open slave PTY FD");
                exit(EXIT_FAILURE);
            }


            // redirect in, out, and error
            for (int i = 0; i < 3; i++) {
                if (dup2(slave_pty_fd, i) < 0) {
                    perror("Oops. Dup2 error.\n");
                    exit(EXIT_FAILURE);
                }
            }

            // run bash
            if (execlp("bash","bash",NULL) < 0) {
                perror("Oops. Exec error.\n");
                exit(EXIT_FAILURE);
            }

            // should not get here if all goes well.
            // kill off errant processes if we do get here.
            exit(EXIT_FAILURE);

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
    return NULL;
}

int handle_pty_master(int client_sockfd, int master_pty_fd) 
{
    /* Take care of the read/write loops between 
     * the client socket and the master PTY
     */

    struct epoll_event ev;


    ev.events = EPOLLIN;
    ev.data.fd = client_sockfd;

    printf("adding epoll fd: %d\n", client_sockfd);
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, client_sockfd, &ev) == -1){
        perror("epoll_ctl");
        return -1;
    }

    printf("adding epoll fd: %d\n", master_pty_fd);
    ev.events = EPOLLIN;
    ev.data.fd = master_pty_fd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, master_pty_fd, &ev) == -1) {
        perror("epoll_ctl");
        return -1;
    }

    epoll_fds[master_pty_fd] = client_sockfd;
    epoll_fds[client_sockfd] = master_pty_fd;
    return 0;
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
    fcntl(master_pty_fd,F_SETFD,FD_CLOEXEC);

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


    printf("server socket in use from accept: %d\n",server_sockfd);
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

    return server_sockfd;
}

