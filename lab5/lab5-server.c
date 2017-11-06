/* lab5-server.c
 * Sam Messina
 * CS 407
 *
 *
 *
 *
 * TODO
 * [ ] reimplement timers
 * [ ] look into partial write problem/fix busy wait in transfer_data()
 * [ ] make sure edge triggered is being used properly
 * [ ] 
 * [ ] 
 * [ ] 
 * [ ] 
 * [ ] 
 * [ ] 
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
#include "tpool.h"

#define SECRET     "cs407rembash\n"
#define PORT       4070
#define INPUT_SIZE 4096
#define MAX_EVENTS 20

const char * const rembash_string = "<rembash>\n";
const char * const ok_string      = "<ok>\n";
const char * const error_string   = "<error>\n";

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

int     setup_server(void);
void *  epoll_wait_loop();
void    process_task(int fd);
int     close_hung_fds(int hung_fd);
int     accept_new_client();
void *  handle_client(client_struct* client);
void    timer_handler(int sig, siginfo_t * si, void * uc);
timer_t setup_timer(int client_sockfd);
int     exec_bash(char  * slave_pty_name);
int     add_fd_to_epoll(int client_sockfd);
int     setup_pty(void);
int     check_protocol_secret(int client_sockfd);
int     write_protocol_string(int client_sockfd);
void    create_client_struct(int fd);
int     kill_client(int fd);
int     transfer_data(int from_fd);
int     set_nonblocking(int fd);
int send_message(int fd , const char * const msg);

client_struct*  client_slab[MAX_EVENTS*2];
int epfd;
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

    if (add_fd_to_epoll(listening_sock) < 0) {
        perror("Could not add listenening socket to epoll unit\n");
        exit(EXIT_FAILURE); }

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

    listening_sock                  = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    server_address.sin_family      = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port        = htons(PORT);
    server_len                     = sizeof(server_address);

    setsockopt(listening_sock, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));

    // use address for socket address to avoid pass by value
    if (bind(listening_sock, (struct sockaddr *)&server_address, server_len)< 0) {
        perror("Error. Could not bind server\n");
        return -1; }

    // listen with a backlog of 5 connections
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

        // go through all the ready events
        for (i = 0; i < ready; i++) {
            current_event = evlist[i];
            sfd           = current_event.data.fd;

            if ((current_event.events & EPOLLHUP) || 
                    (current_event.events & EPOLLERR) ) {
                perror("Error getting next epoll ready fd\n");
                kill_client(sfd); }

            else if (current_event.events & EPOLLIN) {
                tpool_add_task(sfd);
            };
        }
    }
}

void process_task(int fd)
{
    /* Transfer data from one fd to another */


    client_struct* client = client_slab[fd];
    client_state state;

    if (client) {
        state = client->state;}

    if (fd == listening_sock) {
        int client_fd = accept_new_client();
        client = client_slab[client_fd];
        send_message(client_fd, rembash_string);
    }

    else {
        switch (state) {
            case new:
                printf("state: new\n");
                check_protocol_secret(client->sock_fd);
                handle_client(client);
                send_message(client->sock_fd, ok_string);
                break;
            case established :
                printf("state: established\n");
                transfer_data(fd);
                break;
            case unwritten :
                printf("state: unwritten\n");
                send_message(client->sock_fd, client->buf);
                break;
            case terminated :
                printf("state: terminated\n");
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

    if (client_sockfd < 0) {
        perror("Oops. Error accepting connection");
        close(client_sockfd);
    }

    create_client_struct(client_sockfd);
    add_fd_to_epoll(client_sockfd);

    printf("HEY there! Got a client set up\n");
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
    client_slab[master_pty_fd] = client;
    set_nonblocking(master_pty_fd);
    set_nonblocking(client_fd);

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

            if(add_fd_to_epoll(master_pty_fd) < 0) {
                perror("add_fd_to_epoll returned error\n");
                kill(pid, SIGTERM);
                pthread_exit(NULL);
            }
    }
    client->state = established;
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
     */
    return NULL;
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

int add_fd_to_epoll(int fd) 
{
    /* Take care of the read/write loops between 
     * the client socket and the master PTY,
     * and set up the epoll events for read/write 
     * from the client.
     */

    struct epoll_event ev;

    ev.events  = EPOLLIN | EPOLLET;
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
    client_struct client;
    client.sock_fd  = fd;
    client.state    = new;
    client.pty_fd   = -1;
    client_slab[fd] = &client;
}

int kill_client(int fd) 
{
    client_struct* client = client_slab[fd];
    if (!client) {return -1;}

    if (epoll_ctl(epfd, EPOLL_CTL_DEL, client->sock_fd, NULL) < 0 ||
            epoll_ctl(epfd, EPOLL_CTL_DEL, client->pty_fd, NULL) < 0 ) {
        perror("Error removing client fds from epoll unit\n");
        return -1;}

    client_slab[client->pty_fd]  = NULL;
    client_slab[client->sock_fd] = NULL;

    if (close(client->pty_fd) < 0 || 
            close(client->sock_fd) < 0 ) {
        perror("Error closing client fds\n");
        return -1;}

    return 1;
}

int send_message(int fd , const char * const msg)
{
    ssize_t msg_size, nwritten, total;
    msg_size = strlen(msg);
    total = 0;

    do {
        if ((nwritten = write(fd, msg+total, msg_size - total)) <0) {
            perror("Error writing message\n");
            return -1; }
        total += nwritten;
    } while(total < msg_size);
    errno = 0;  //in case errno got set by write()
    return 1;

}

int transfer_data(int from_fd)
{
    client_struct* client = client_slab[from_fd];
    int to_fd = (client->pty_fd == from_fd) ?  client->sock_fd : client->pty_fd;

    char buff[4096];
    memset(buff, 0, 4096);

    ssize_t nread, nwritten;

    errno = 0;  
    if ((nread = read(from_fd,buff,4096)) < 0) {
        perror("Error reading\n");
        return -1; }

    nwritten = write(to_fd,buff,nread);
    if (nwritten == -1 && errno != EAGAIN) {
        perror("Error writing data\n");
        return -1; }

    else if (errno == EAGAIN) {
        printf("EAGAIN HIT");
        client->state = unwritten;
        *client->buf = *buff;
        tpool_add_task(client->sock_fd);
    }
    else {
        client->state = established;
    }


    return 1;
}

int set_nonblocking(int fd)
{
    int flags;

    if ((flags = fcntl(fd, F_GETFL, 0)) < 0) {
        perror("Could not get fd flags\n"); 
        return -1; }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("Could not set nonblocking fd flags\n"); 
        return -1; }

    return 0;
}
