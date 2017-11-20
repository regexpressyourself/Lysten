/* lab5-server.c
 * Sam Messina
 * CS 407
 * TODO
 * [ ] look into partial write problem/fix busy wait in transfer_data()
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
#include <sys/timerfd.h>
#include <unistd.h>
#include "tpool.h"

#define SECRET     "cs407rembash\n"
#define PORT       4070
#define INPUT_SIZE 4096
#define MAX_EVENTS 20

char* rembash_string = "<rembash>\n";
char* ok_string      = "<ok>\n";
char* error_string   = "<error>\n";

typedef enum state {
    new,
    established,
    unwritten,
    terminated
} client_state;

typedef struct client {
    int sock_fd;
    int pty_fd;
    client_state state;
    char buf[4096];
} client_struct;

typedef struct timer {
    int timer_fd;
    int client_fd;
} timer_struct;

int     setup_server(void);
void *  epoll_wait_loop();
void    process_task(int fd);
int     close_hung_fds(int hung_fd);
int     accept_new_client();
void *  handle_client(client_struct* client);
void    timer_handler(int sig, siginfo_t * si, void * uc);
int setup_timer(int client_sockfd);
int     exec_bash(char  * slave_pty_name);
int     add_fd_to_epoll(int client_sockfd);
int     setup_pty(void);
int     check_protocol_secret(int client_sockfd);
int     write_protocol_string(int client_sockfd);
void    create_client_struct(int fd);
int     kill_client(int fd);
int     transfer_data(int from_fd);
int     set_nonblocking(int fd);
int send_message(int fd , char* msg);
void print_client_info( client_struct* client);
int finish_writing_data(client_struct* client);
int mod_epoll_unit(int fd) ;
int handle_timer_epoll_loop(int sfd);

// TODO: check indexes for all injections on this
int timer_slab[MAX_EVENTS];
client_struct*  client_ptr_slab[MAX_EVENTS*2];
client_struct  client_slab[MAX_EVENTS];
int epfd;
int timer_epfd;
int listening_sock;

int main()
{
    // setup our server
    if (setup_server() == -1) {
        exit(EXIT_FAILURE);
    }

    if (tpool_init(process_task) < 0) {
        perror("Could not create initiate thread pool\n");
        exit(EXIT_FAILURE); }

    signal(SIGCHLD, SIG_IGN);

    // create our epoll fd
    if ((epfd = epoll_create1(EPOLL_CLOEXEC)) == -1) {
        perror("Could not create epoll unit\n");
        exit(EXIT_FAILURE); }

    if ((timer_epfd = epoll_create1(EPOLL_CLOEXEC)) == -1) {
        perror("Could not create epoll unit\n");
        exit(EXIT_FAILURE); }

    if (add_fd_to_epoll(timer_epfd) < 0) {
        perror("Could not add listenening socket to epoll unit\n");
        exit(EXIT_FAILURE); }

    if (add_fd_to_epoll(listening_sock) < 0) {
        perror("Could not add listenening socket to epoll unit\n");
        exit(EXIT_FAILURE); }

    printf("server starting...\n");
    // start the epoll wait loop
    epoll_wait_loop();

    // ignore bash's signals, as we don't care about exit status

    return EXIT_FAILURE;
}

int setup_server(void) 
{
    /* Performs the heavy lifting to get the 
     * server off the ground
     */
    socklen_t server_len;
    struct    sockaddr_in server_address;
    int       option = 1;

    listening_sock                 = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    server_address.sin_family      = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port        = htons(PORT);
    server_len                     = sizeof(server_address);

    setsockopt(listening_sock, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));

    // use address for socket address to avoid pass by value
    if (bind(listening_sock, (struct sockaddr *)&server_address, server_len)< 0) {
        perror("Error. Could not bind server\n");
        return -1; }

    // listen with a backlog of 512 connections
    if ((listen(listening_sock, 512) < 0)) { 
        perror("Oops. Error listening on server\n"); 
        return -1; }

    set_nonblocking(listening_sock);
    return 1;
}

void * epoll_wait_loop()
{
    int    ready; // how many ready epoll events
    int    sfd;   // the fd of the ready epoll event
    int    i;     // counter for for loop
    struct epoll_event evlist[MAX_EVENTS];
    struct epoll_event current_event;

    while (1) {
        ready = epoll_wait(epfd, evlist, MAX_EVENTS, -1);
        if (ready < 0 ){
            printf("problem with epoll_wait\n");
            break;
        }

        for (i = 0; i < ready; i++) {
            current_event = evlist[i];
            sfd           = current_event.data.fd;
            printf("next i: %d\n", i);
            printf("sfd is %d\n", sfd);

            if ((current_event.events & EPOLLHUP) || 
                    (current_event.events & EPOLLERR) ) {
                perror("Error getting next epoll ready fd\n");
                kill_client(sfd); }

            else if (current_event.events & EPOLLIN ||
                    current_event.events & EPOLLOUT) {
                if (sfd == timer_epfd) {
                    if (handle_timer_epoll_loop(sfd) < 0) {
                        perror("Error handling timer epoll\n");
                    }
                }
                else if (tpool_add_task(sfd) < 0) {
                    printf("error adding task\n");
                }
            };
        }
    }
    printf("UH OH, EPOLL LOOP BROKE\n");
    return NULL;
}

int handle_timer_epoll_loop(int sfd)
{
    int i;
    int    time_ready; // how many ready epoll events
    int    time_sfd;   // the fd of the ready epoll event
    struct epoll_event time_evlist[MAX_EVENTS];
    struct epoll_event time_current_event;
    printf("TIMER FD MARKED IN EPOLL\n");

    time_ready = epoll_wait(timer_epfd, time_evlist, MAX_EVENTS, -1);

    if (time_ready < 0 ){
        printf("problem with epoll_wait\n");
        return -1;
    }
    for (i = 0; i < time_ready; i++) {
        time_current_event = time_evlist[i];
        time_sfd           = time_current_event.data.fd;

        int client_fd = timer_slab[time_sfd];
        if (client_ptr_slab[client_fd] && client_ptr_slab[client_fd]->state == new){

            if (epoll_ctl(epfd, EPOLL_CTL_DEL, client_fd, NULL) < 0) {
                perror("Error removing client fds from epoll unit\n");
                return -1;}
            close(client_fd);
        }

        if (epoll_ctl(timer_epfd, EPOLL_CTL_DEL, time_sfd, NULL) < 0) {
            perror("Error removing client fds from epoll unit\n");
            return -1;}
    }
    return 1;
}

void process_task(int fd)
{
    /* Transfer data from one fd to another */

    client_struct* client = client_ptr_slab[fd];
    client_state state;
    int client_fd;

    if (client) {
        state = client->state;}

    if (fd == listening_sock) {
        client_fd = accept_new_client();
        setup_timer(client_fd);
        client = client_ptr_slab[client_fd];
        send_message(client_fd, rembash_string);
        print_client_info(client);
        mod_epoll_unit(fd);
    }

    else {
        switch (state) {
            case new:
                printf("state: new\n");
                client_fd = client->sock_fd;

                check_protocol_secret(client_fd);

                handle_client(client);
                send_message(client_fd, ok_string);

                client->state = established;
                mod_epoll_unit(fd);
                break;
            case established :
                printf("state: established\n");

                if (transfer_data(fd) < 0 ) {
                    printf("ERROR TRANSFERRING DATA from %d\n", fd);
                }

                mod_epoll_unit(fd);
                break;
            case unwritten :
                printf("state: unwritten\n");
                send_message(client->sock_fd, client->buf);
                client->state = established;
                memset(client->buf, 0, 4096);
                mod_epoll_unit(fd);
                break;
            case terminated :
                print_client_info(client);
                printf("state: terminated\n");
                kill_client(client->sock_fd);
                break;
            default:
                printf("state: default\n");
                break;
        }

    }

}

int accept_new_client()
{
    /* Accepts new clients and returns 
     * their FD
     */
    int client_sockfd = 0; // socket for new client

#ifdef DEBUG
    printf("server socket in use from accept: %d\n",listening_sock);
#endif

    // seperate fd for each client
    client_sockfd = accept4(listening_sock, (struct sockaddr *) NULL, NULL, SOCK_CLOEXEC);
    printf("ACCEPTED NEW CLIENT: %d\n", client_sockfd);


    if (client_sockfd < 0) {
        perror("Oops. Error accepting connection");
        close(client_sockfd);
    }

    set_nonblocking(client_sockfd);
    create_client_struct(client_sockfd);
    add_fd_to_epoll(client_sockfd);

    return client_sockfd;
}

void * handle_client(client_struct* client) 
{
    /* handle_client is called after a 
     * connection with the client has been created.
     *
     * client_sockfd is the file descriptor for the socket.
     */

    int client_fd = client->sock_fd;
    int master_pty_fd; // the master pty
    char *  slave_pty_name;
    int pid;

    // run all the setup functions for the master pty
    if ((master_pty_fd = setup_pty()) < 0) {
#ifdef DEBUG
        perror("Error setting up PTY\n");
#endif
    }
    client->pty_fd = master_pty_fd;
    client_ptr_slab[master_pty_fd] = client;
    //printf("Master PTY FD:\t%d\n", master_pty_fd);
    //printf("Client FD (in handle_client):\t%d\n", client_fd);

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
            close(client_fd);
            exit(1);
            break;
        case 0: // child
            // set up pty slave and bash process
            close(master_pty_fd);
            close(client_fd);
            exec_bash(slave_pty_name);
        default: // parent
            // set up the epoll units and kill the temp thread
            set_nonblocking(master_pty_fd);
            if(add_fd_to_epoll(master_pty_fd) < 0) {
                perror("add_fd_to_epoll returned error\n");
                kill(pid, SIGTERM);
                pthread_exit(NULL);
            }
    }
    return NULL;
}

int setup_timer(int client_sockfd)
{
    /* Set up our timer. The timer will count 3 seconds before 
     * disconnecting an unresponsive client. Once the protocol is 
     * complete, the timer is deleted
     */

    struct itimerspec timer; // the timer itself
    int     timer_flag = 0;  // flag to be switched when the timer completes
    int timer_fd;

    timer.it_value.tv_sec     = 3;
    timer.it_value.tv_nsec    = 0;
    timer.it_interval.tv_sec  = 0;
    timer.it_interval.tv_nsec = 0;

    if ((timer_fd = timerfd_create(CLOCK_REALTIME, 0)) == -1) {
        perror("Error creating timer\n");
        return -1;
    }
    printf("timer fd is: %d\n", timer_fd);

    if (timerfd_settime(timer_fd, timer_flag, &timer, NULL) == -1) {
        perror("Error setting time on timer\n");
        return -1;
    }
    printf("Adding client fd (%d) to slab at index: %d\n", client_sockfd,timer_fd);

    timer_slab[timer_fd] = client_sockfd;

    struct epoll_event ev;

    ev.events  = EPOLLIN | EPOLLET | EPOLLONESHOT;
    //ev.events  = EPOLLIN | EPOLLET ;
    ev.data.fd = timer_fd;
    if (epoll_ctl(timer_epfd, EPOLL_CTL_ADD, timer_fd, &ev) == -1){
        fprintf(stderr, "adding client epoll_ctl failed. fd: %d\n", timer_fd);
        return -1;
    }

    return timer_fd;

}

int check_protocol_secret(int client_sockfd) 
{
    int  bytes_read;
    char buffer[INPUT_SIZE];
    memset(buffer, 0, INPUT_SIZE);

    // wait to read the secret word from client.
    // our timer will interrupt this read in the event of 
    // a malicious client.
    if ((bytes_read = read(client_sockfd, buffer, sizeof(buffer)))< 0){
        perror("Oops. Error reading from client.\n"); 
        return -1;
    }

    // if the secret is incorrect, close the connection
    if (strcmp(buffer, SECRET) != 0) {
        send_message(client_sockfd, error_string);
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
        exit(EXIT_FAILURE); }

    // create a new session
    if (setsid() < 0) {
        perror("Could not set SID\n");
        exit(EXIT_FAILURE); }

    // redirect in, out, and error
    for (int i = 0; i < 3; i++) {
        if (dup2(slave_pty_fd, i) < 0) {
            perror("Oops. Dup2 error.\n");
            exit(EXIT_FAILURE); }
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

int mod_epoll_unit(int fd) 
{
    struct epoll_event ev;
    ev.events  = EPOLLIN | EPOLLET | EPOLLONESHOT;
    //ev.events  = EPOLLIN| EPOLLET;
    ev.data.fd = fd;
    if (epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev) == -1){
        fprintf(stderr, "Modding client epoll_ctl failed. fd: %d\n", fd);
        return -1;
    }
    printf("modded epoll unit: %d\n", fd);
    return 0;
}

int add_fd_to_epoll(int fd) 
{
    /* Take care of the read/write loops between 
     * the client socket and the master PTY,
     * and set up the epoll events for read/write 
     * from the client.
     */

    struct epoll_event ev;

    ev.events  = EPOLLIN | EPOLLET | EPOLLONESHOT;
    //ev.events  = EPOLLIN | EPOLLET ;
    ev.data.fd = fd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) == -1){
        fprintf(stderr, "adding client epoll_ctl failed. fd: %d\n", fd);
        return -1;
    }

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

void create_client_struct(int fd) 
{
    client_struct client = client_slab[fd];
    client.sock_fd  = fd;
    client.state    = new;
    client.pty_fd   = -1;
    client_slab[fd] = client;
    client_ptr_slab[fd] = &client;
}

int kill_client(int fd) 
{
    client_struct* client = client_ptr_slab[fd];
    if (!client) {return -1;}

    if (epoll_ctl(epfd, EPOLL_CTL_DEL, client->sock_fd, NULL) < 0 ||
            epoll_ctl(epfd, EPOLL_CTL_DEL, client->pty_fd, NULL) < 0 ) {
        perror("Error removing client fds from epoll unit\n");
        return -1;}

    client_ptr_slab[client->pty_fd]  = NULL;
    client_ptr_slab[client->sock_fd] = NULL;
    client_slab[client->sock_fd].sock_fd = -1;
    client_slab[client->sock_fd].pty_fd = -1;

    if (close(client->pty_fd) < 0 || 
            close(client->sock_fd) < 0 ) {
        perror("Error closing client fds\n");
        return -1;}

    return 1;
}

int send_message(int fd , char* msg)
{
    ssize_t msg_size, nwritten;
    msg_size = strlen(msg);
    nwritten = write(fd, msg, msg_size );

    if (nwritten < 0) {
        perror("Error writing message\n");
        return -1; }

    errno = 0;  //in case errno got set by write()
    return 1;

}

int transfer_data(int from_fd)
{
    client_struct* client = client_ptr_slab[from_fd];

    printf("pty_fd: %d\n", client->pty_fd);
    printf("sock_fd: %d\n", client->sock_fd);

    int to_fd = from_fd == client->pty_fd ? client->sock_fd : client->pty_fd;
    printf("IN transfer_data() (from: %d, to: %d)\n", from_fd, to_fd);

    char buff[4096];
    ssize_t nread, nwritten;
    errno = 0;
    nread = read(from_fd, buff, 4096);
    if (nread < 0 || errno) {
        fprintf(stderr, "Error reading from: %d\n", from_fd);
        return -1;
    }
    else if (nread == 0) {
        client->state = terminated;
        return -1;
    }
    nwritten = write(to_fd, buff, nread);
    if (nwritten < 0 || (errno && errno != EAGAIN)) {
        fprintf(stderr, "Error writinf to: %d\n", to_fd);
        nwritten = write(to_fd, client->buf, nread);
        client->state = unwritten;
        return -1;
    }
    client->state = established;
    return 1;
}

int set_nonblocking(int fd)
{
    int flags;

    if ((flags = fcntl(fd, F_GETFL, 0)) == -1)
    {
        perror("fcntl get flags");
        return -1;
    }
    flags |= O_NONBLOCK;
    if (fcntl(fd, F_SETFL, flags) == -1)
    {
        perror("fcntl set flags");
        return -1;
    }
    return 0;

}

void print_client_info( client_struct* client) 
{
    switch (client->state) {
        case new:
            printf("\n\nNEW CLIENT\n");
            break;
        case established:
            printf("\n\nESTABLISHED CLIENT \n");
            break;
        case unwritten:
            printf("\n\nUNWRITTEN CLIENT \n");
            break;
        case terminated:
            printf("\n\nTERMINATED CLIENT \n");
            break;
        default:
            printf("\n\nUNKNOWN STATE CLIENT -  %d\n", client->state);
            break;
    }
    printf("Sock FD:\t%d\n", client->sock_fd);
    printf("PTY FD: \t%d\n", client->pty_fd);
    printf("Buf:    \t[");
    int i = 3;
    for (; i < 15; i++){
        printf("%d, ", client_ptr_slab[i] ? client_ptr_slab[i]->sock_fd : 0);
    }
    printf("%d]\n\n", client_ptr_slab[i] ? client_ptr_slab[i]->sock_fd : 0);

    printf("Client Buf:\t");
    printf("%s\n", client->buf);
}
