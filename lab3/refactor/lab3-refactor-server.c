/* lab3-server.c
 * Sam Messina
 * CS 407
 */

#define _XOPEN_SOURCE 600
#define _POSIX_C_SOURCE 199309
#define _GNU_SOURCE
#define TIMER_SIG SIGRTMAX

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define SECRET     "cs407rembash\n"
#define PORT       4070
#define INPUT_SIZE 4096
#define MAX_EVENTS 20

int     setup_server(void);
void *  epoll_wait_loop(void * arg);
int     transfer_data(int in_fd, int out_fd);
int     close_hung_fds(int hung_fd);
int     accept_new_client(int server_sockfd);
void *  handle_client(void * client_sockfd_ptr);
void    timer_handler(int sig, siginfo_t * si, void * uc);
timer_t setup_timer(int client_sockfd);
int     handle_protocol(int client_sockfd);
int     exec_bash(char  * slave_pty_name);
int     setup_client_pty_epoll_units(int client_sockfd, int master_pty_fd);
int     setup_pty(void);
 
int epoll_fds[MAX_EVENTS*2 + 5];
int epfd;

int main()
{
    int        server_sockfd;     // socket fd for the server
    int        client_sockfd = 0; // client socket fd returned from accept()
    int       *client_sockfd_ptr; // memory to hold the client socket
    pthread_t temp_threadid;      // handle new client protocol thread
    pthread_t epoll_threadid;     // epoll wait loop thread

    // setup our server
    if ((server_sockfd = setup_server()) == -1) {
        exit(EXIT_FAILURE);
    }

    // create our epoll fd
    if ((epfd = epoll_create1(EPOLL_CLOEXEC)) == -1) {
        perror("Could not create epoll unit\n");
        exit(EXIT_FAILURE);
    }

    // start the epoll wait loop
    if (pthread_create(&epoll_threadid, NULL, epoll_wait_loop, NULL) == 
            (EAGAIN || EINVAL || EPERM)) {
        perror("Could not start epoll_wait thread\n");
        exit(EXIT_FAILURE);
    }

    // ignore bash's signals, as we don't care about exit status
    signal(SIGCHLD,SIG_IGN);

    while(1) {
        // server set up and started
        #ifdef DEBUG
        printf("server waiting\n");
        #endif

        /*
         * Create a little memory for our client socket FDs.
         * This avoids race conditions between pthread creation, 
         * handle_client's call, and another client connecting, 
         * thus hijacking client_sockfd.
         */
        client_sockfd_ptr = malloc(sizeof(int));

        // accept a new client
        client_sockfd = accept_new_client(server_sockfd);

        // if accept fails, just continue accepting new clients
        if (client_sockfd < 0) {continue;};

        // get that file descriptor and put it in the 
        // memory we malloc'ed earlier
        *client_sockfd_ptr = client_sockfd;

        // create a temporary thread to start handling the protocol
        if (pthread_create(&temp_threadid, NULL, handle_client, client_sockfd_ptr) == 
                (EAGAIN || EINVAL || EPERM)) {
            perror("Could not create temp thread for  client\n");
            close(*client_sockfd_ptr);
        }

        // ... and start it all again
    }
    close(server_sockfd);
    return EXIT_FAILURE;
}

int setup_server(void) 
{
    /* Performs the heavy lifting to get the 
     * server off the ground
     */
    int       server_sockfd;
    socklen_t server_len;
    struct    sockaddr_in server_address;
    int       option = 1;

    server_sockfd                  = socket(AF_INET, SOCK_STREAM, 0);
    server_address.sin_family      = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port        = htons(PORT);
    server_len                     = sizeof(server_address);
    setsockopt(server_sockfd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));

    // use address for socket address to avoid pass by value
    if (bind(server_sockfd, (struct sockaddr *)&server_address, server_len)< 0) {
        perror("Error. Could not bind server\n");
        return -1;
    }

    // listen with a backlog of 5 connections
    if ((listen(server_sockfd, 5) < 0)) { 
        perror("Oops. Error listening on server\n"); 
        return -1;
    }
    return server_sockfd;
}

void * epoll_wait_loop(void*arg)
{
    int    ready; // how many ready epoll events
    int    sfd;   // the fd of the ready epoll event
    int    i;     // counter for for loop
    struct epoll_event evlist[MAX_EVENTS];
    struct epoll_event current_event;

    while (1) {
        ready = epoll_wait(epfd, evlist, MAX_EVENTS, -1);

        // go through all the ready events
        for (i = 0; i < ready; i++) {
            current_event = evlist[i];
            // get the fd of the ready event
            sfd = current_event.data.fd;

            if ((current_event.events & EPOLLHUP) || 
                    (current_event.events & EPOLLERR) ) {
                // in the event of error,
                // close both the hungup fd and its compatriot
                close_hung_fds(sfd);
                close_hung_fds(epoll_fds[sfd]);
            }
            if (current_event.events & EPOLLIN) {
                #ifdef DEBUG
                printf("Ready epoll fd: %d\n", sfd);
                #endif

                // transfer data from the ready epoll event to its compatriot
                if (transfer_data(sfd, epoll_fds[sfd]) <= 0) {
                    close_hung_fds(sfd);
                    close_hung_fds(epoll_fds[sfd]);
                }
            };
        }
    }
}

int transfer_data(int in_fd, int out_fd)
{
    /* Transfer data from one fd to another */

    char buf[INPUT_SIZE]; // holds the read data
    int  bytes_read;      // holds return value of read()
    memset(buf, 0, sizeof(buf));

    // read from the in fd
    bytes_read = read(in_fd, buf, sizeof(buf));
    if (bytes_read == -1) {
        fprintf(stderr, "Error reading from: %d\n", in_fd);
        return -1;
    }
    else if (bytes_read == 0) {
        return 0;
    }

    // write to the out fd
    bytes_read = write(out_fd, buf, strlen(buf));
    if (bytes_read == -1) {
        fprintf(stderr, "Error writing to : %d\n", out_fd);
        return -1;
    }
    return 1;
}

int close_hung_fds(int hung_fd)
{

    // close the hung thread 
    #ifdef DEBUG
    printf("closing %d\n", hung_fd);
    #endif
    if (close(hung_fd) == -1 ) {
        #ifdef DEBUG
        printf("error closing fd: %d\n", hung_fd);
        #endif
        return -1;
    }
    #ifdef DEBUG
    printf("closed fd: %d\n", hung_fd);
    #endif
    return 1;
}

int accept_new_client(int server_sockfd)
{
    /* Accepts new clients and returns 
     * their FD
     */
    int       client_sockfd = 0; // socket for new client
    socklen_t client_len;
    struct    sockaddr_in client_address;

    #ifdef DEBUG
    printf("server socket in use from accept: %d\n",server_sockfd);
    #endif
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

void * handle_client(void*client_sockfd_ptr) 
{
    /* handle_client is called after a 
     * connection with the client has been created.
     *
     * client_sockfd is the file descriptor for the socket.
     */
    int     client_sockfd; // socket for the client
    int     master_pty_fd; // the master pty
    char *  slave_pty_name;
    pid_t   pid;
    timer_t timerid;

    // get the client's fd from the malloc'ed memory
    client_sockfd = *(int*)client_sockfd_ptr;
    free(client_sockfd_ptr);

    // start the timer
    if ((timerid = setup_timer(client_sockfd)) == NULL) {
        close(client_sockfd);
        pthread_exit(NULL);
    }

    // handle rembash protocol
    if (handle_protocol(client_sockfd) == -1) {
        #ifdef DEBUG
        printf("Error in handle_protocol()");
        #endif
        close(client_sockfd);
        pthread_exit(NULL);
    }

    timer_delete(timerid);

    // run all the setup functions for the master pty
    if ((master_pty_fd = setup_pty()) < 0) {
        #ifdef DEBUG
        perror("Error setting up PTY\n");
        #endif
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
            // set up pty slave and bash process
            close(master_pty_fd);
            close(client_sockfd);
            exec_bash(slave_pty_name);
        default: // parent
            // set up the epoll units and kill the temp thread
            if(setup_client_pty_epoll_units(client_sockfd, master_pty_fd) < 0) {
                perror("setup_client_pty_epoll_units returned error\n");
                kill(pid, SIGTERM);
                pthread_exit(NULL);
            }
            pthread_exit(NULL);
    }
    return NULL;
}

void timer_handler (int sig, siginfo_t *si, void *uc)
{
    * (int *) si->si_ptr = 1;
}

timer_t setup_timer(int client_sockfd)
{
    /* Set up our timer. The timer will count 3 seconds before 
     * disconnecting an unresponsive client. Once the protocol is 
     * complete, the timer is deleted
     */

    struct itimerspec timer; // the timer itself
    struct sigaction  sa;    // action to be taken on timer expiration
    struct sigevent   sev;   // the even to the triggered
    timer_t timerid;         // id of our timer
    int     timer_flag = 0;  // flag to be switched when the timer completes

    timer.it_value.tv_sec     = 3;
    timer.it_value.tv_nsec    = 0;
    timer.it_interval.tv_sec  = 0;
    timer.it_interval.tv_nsec = 0;

    sa.sa_flags     = SA_SIGINFO;
    sa.sa_sigaction = timer_handler;

    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGALRM, &sa, NULL) == -1) {
        perror("Error setting sigaction\n");
        return NULL;
    }

    sev.sigev_signo           = SIGALRM;
    sev.sigev_notify          = SIGEV_THREAD_ID;
    sev.sigev_value.sival_ptr = &timer_flag;
    sev._sigev_un._tid        = syscall(SYS_gettid);

    if (timer_create(CLOCK_REALTIME, &sev, &timerid) == -1) {
        perror("Error creating timer\n");
        return NULL;
    }

    if (timer_settime(timerid, 0, &timer, NULL) == -1) {
        perror("Error setting time on timer\n");
        return NULL;
    }

    return timerid;
}

int handle_protocol(int client_sockfd) 
{
    /* Handles the rembash protocol step 
     * in the server 
     */

    const char * const rembash_string = "<rembash>\n";
    const char * const ok_string      = "<ok>\n";
    const char * const error_string   = "<error>\n";
    int  bytes_read;
    char buffer[INPUT_SIZE];
    #ifdef DEBUG
    printf(" handle_protocol'sclient fd: %d\n",client_sockfd);
    #endif

    // send "<rembash>" to the client upon connection
    if (write(client_sockfd, rembash_string, strlen(rembash_string))< 0){
        perror("Oops. Error writing to client.\n"); 
        return -1;
    }

    // wait to read the secret word from client.
    // our timer will interrupt this read in the event of 
    // a malicious client.
    if ((bytes_read = read(client_sockfd, buffer, sizeof(buffer)))< 0){
        perror("Oops. Error reading from client.\n"); 
        return -1;
    }

    // if the secret is incorrect, close the connection
    if (strcmp(buffer, SECRET) != 0) {
        if (write(client_sockfd, error_string, strlen(error_string)) < 0){
            perror("Oops. Error writing to socket\n"); 
            return -1;
        }
        return -1;
    }

    // if the secret was correct, send <ok>
    if (write(client_sockfd, ok_string, strlen(ok_string)) < 0){
        perror("Oops. Error writing to client.\n"); 
        return -1;
    }
    return 1;
}

int exec_bash(char  *slave_pty_name)
{
    int slave_pty_fd;
    // get the slave PTY FD
    if ((slave_pty_fd = open(slave_pty_name, O_RDWR)) < 0) {
        perror("Oops. Could not open slave PTY FD");
        exit(EXIT_FAILURE);
    }

    // create a new session
    if (setsid() < 0) {
        perror("Could not set SID\n");
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
}

int setup_client_pty_epoll_units(int client_sockfd, int master_pty_fd) 
{
    /* Take care of the read/write loops between 
     * the client socket and the master PTY,
     * and set up the epoll events for read/write 
     * from the client.
     */

    struct epoll_event ev;

    #ifdef DEBUG
    printf("adding epoll fd: %d\n", client_sockfd);
    #endif
    ev.events  = EPOLLIN;
    ev.data.fd = client_sockfd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, client_sockfd, &ev) == -1){
        fprintf(stderr, "adding client epoll_ctl failed. fd: %d\n", client_sockfd);
        return -1;
    }

    #ifdef DEBUG
    printf("adding epoll fd: %d\n", master_pty_fd);
    #endif
    ev.events  = EPOLLIN;
    ev.data.fd = master_pty_fd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, master_pty_fd, &ev) == -1) {
        fprintf(stderr, "adding client epoll_ctl failed. fd: %d\n", master_pty_fd);
        return -1;
    }

    // add the two new FDs to the hacky hash map
    epoll_fds[master_pty_fd] = client_sockfd;
    epoll_fds[client_sockfd] = master_pty_fd;
    return 0;
}

int setup_pty(void) 
{
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



