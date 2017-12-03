/* lab1-server.c
 * Sam Messina
 * CS 407
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#define SECRET "cs407rembash\n"
#define PORT 4070
#define INPUT_SIZE 4096

int error_check(int return_val, char *error_msg, int sockfd);
void handle_client(int client_sockfd);
int setup_socket(void);
int get_client_sockfd(int server_sockfd);
int handle_protocol(int client_sockfd);

int main()
{
    /* Main Driver
     * 1. client connects
     * 2. secret word exchanged
     * 3. fork
     * 4. one fork continues accepting clients
     * 5. other fork redirects in, out, and err; then...
     * 6. execs bash
     * 7. when exec finished, child is terminated
     */

    int   server_sockfd;
    pid_t pid;
    int   client_sockfd = 0;

    server_sockfd = setup_socket();
    while(1) {
        // server set up and started
        printf("server waiting\n");
        client_sockfd = get_client_sockfd(server_sockfd);
        if (client_sockfd) {
            pid = fork();
            if (pid < 0) {
                perror("Oops. Error in fork call. Did you try a spoon?\n");
                close(client_sockfd);
            }
            else if (pid > 0) {
                // parent closes socket and returns back to main,
                // waiting for new connections
                close(client_sockfd);
            }
            else {
                handle_client(client_sockfd);
            }
        }
    }
    return EXIT_FAILURE;
}


int handle_protocol(int client_sockfd) {
    const char * const rembash_string = "<rembash>\n";
    const char * const ok_string      = "<ok>\n";
    const char * const error_string   = "<error>\n";
    int bytes_read;
    char  buffer[INPUT_SIZE];

    // send "<rembash>" to the client upon connection
    error_check(write(client_sockfd, rembash_string, strlen(rembash_string))< 0, 
            "Oops. Error writing to client.\n", 
            client_sockfd);

    // wait to read the secret word from client
    error_check((bytes_read = read(client_sockfd, &buffer, sizeof(buffer)))< 0, 
            "Oops. Error reading from client.\n", 
            client_sockfd);

    // if the secret is incorrect, close the connection
    if (strcmp(buffer, SECRET) != 0) {
        error_check(write(client_sockfd, error_string, strlen(error_string)) < 0, 
                "Oops. Error writing to socket\n", 
                client_sockfd);
        close(client_sockfd);
        return -1;
    }

    // if the secret was correct, send <ok>
    error_check(write(client_sockfd, ok_string, strlen(ok_string)) < 0, 
            "Oops. Error writing to client.\n", 
            client_sockfd);
    return 1;
}

void handle_client(int client_sockfd) {
    /* handle_client is called after a 
     * connection with the client has been created.
     *
     * client_sockfd is the file descriptor for the socket.
     */

    char  buffer[INPUT_SIZE];
    memset(buffer, 0, sizeof(buffer));
    int i;
    int sources_to_redirect[] = {STDERR_FILENO, STDIN_FILENO, STDOUT_FILENO};

    // child handles bash exec call

    if (handle_protocol(client_sockfd) == -1) {
        close(client_sockfd);
        exit(EXIT_FAILURE);
    }

    // create new bash session for each client
    error_check((setsid() < 0), 
            "Oops. Error setting session ID.\n", 
            client_sockfd);

    // redirect STDIN, STDERR, and STDOUT to socket
    for (i = 0; i < sizeof(sources_to_redirect); i++) {
        error_check(dup2(client_sockfd, sources_to_redirect[i]) < 0, 
                "Oops. Dup2 error.\n", 
                client_sockfd);
    }

    // exec bash
    error_check(execlp("bash","bash","--noediting","-i",NULL) < 0, 
            "Oops. Exec error.\n", 
            client_sockfd);

    exit(EXIT_FAILURE);
    return;
}

int get_client_sockfd(int server_sockfd)
{
    int       client_sockfd = 0;
    socklen_t client_len;
    struct    sockaddr_in client_address;

    client_len    = sizeof(client_address);

    // seperate fd for each client
    client_sockfd = accept(server_sockfd,
            (struct sockaddr *)&client_address, 
            &client_len);

    error_check(client_sockfd,
            "Oops. Error accepting connection",
            client_sockfd);
    return client_sockfd;
}

int setup_socket(void) 
{
    int       server_sockfd;
    int       client_sockfd = 0;
    socklen_t server_len;
    struct    sockaddr_in server_address;

    server_sockfd                  = socket(AF_INET, SOCK_STREAM, 0);
    server_address.sin_family      = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port        = htons(PORT);
    server_len                     = sizeof(server_address);

    // use address for socket address to avoid pass by value
    error_check(bind(server_sockfd, (struct sockaddr *)&server_address, server_len)< 0, 
            "Oops. Error binding socket.\n", 
            client_sockfd);

    // listen with a backlog of 5 connections
    error_check((listen(server_sockfd, 5) < 0), 
            "Oops. Error \n", 
            client_sockfd);

    error_check((signal(SIGCHLD, SIG_IGN) == SIG_ERR), 
            "Oops. Error \n", 
            client_sockfd);
    return server_sockfd;
}

int error_check(int return_val, char *error_msg, int sockfd) {
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
