// Server Solution to Lab #2 in CS 407/591, Fall 2017.
// Author: Norman Carver (copyright 2017), Computer Science Dept., Southern Illinois University Carbondale.
// This material is provided for personal use by students enrolled in CS407/591 Fall 2017 only!
// Any other use represents infringement of the author's exclusive rights under US copyright law.
// In particular, posting this file to a website or in any way sharing it is expressly forbidden.
// Such sharing is also a violation of SIUC's Student Conduct Code (section 2.1.3), so may result
// in academic sanctions such as grade reduction.
//
// Usage: server
//
// Properties:
// -- parallel/concurrent server (can handle multiple simultaneous clients)
// -- handle_client() runs in separate subprocess
// -- uses plain read() with initial protocol exchange
// -- assumes all socket write()'s are done in full (no partials), which is true since blocking mode
// -- creates three subprocesses for each client:
//      (1) handle initial protocol exchange, create PTY, start bash subprocess, relay data PTY-->socket, collect children
//      (2) exec bash with standard in/out/error redirected to PTY
//      (3) transfer data between PTY<--socket
// -- creates a PTY for bash-client interaction (stdin/stout/stderr redirected to PTY)
// -- puts each bash subprocess into separate session, to allow concurrent bash processes (PTY is bash's controlling terminal)
// -- sets SIGCHLD to be ignored in server to avoid having to collect client handling subprocesses
// -- sets SIGCHLD handler in client handler process to detect premature termination of PTY<--socket subprocess
// -- broken/malicious clients can cause resource problems due to subprocess creation
// -- three subprocesses/client will limit ability to handle large numbers of simultaneous clients


#define _XOPEN_SOURCE 600  //for posix_openpt(), etc.

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <wait.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "DTRACE.h"

//Declared constants:
#define PORTNUM 4070
#define SECRET "cs407rembash"


//Function prototypes:
int setup_listening_socket();
void handle_client(int client_fd);
int validate_client_secret(int client_fd);
int create_new_pty_pair(int *ptymasterfd_ptr, char **ptyslavename_ptr);
void run_shell_via_pty(char *ptyslave_name);
void relay_data_between_socket_and_pty(int client_fd, int ptymaster_fd);
int transfer_data(int source_fd, int target_fd);
//void sigchld_handler(int signal);
void sigchld_handler(int signal, siginfo_t *sip, void *ignore);



//Global variables:
//Store handle_client() subprocess's children PIDs, so can terminate from SIGCHLD handler: 
pid_t cpid[2];


int main()
{
  DTRACE("Server starting: PID=%ld, PGID=%ld, SID=%ld\n",(long)getpid(),(long)getpgrp(),(long)getsid(0));

  int listen_fd, connect_fd;

  //Create listening TCP socket:
  if ((listen_fd = setup_listening_socket()) == -1)
    exit(EXIT_FAILURE);

  //Set SIGCHLD signals to be ignored, which causes child processes (running handle_client())
  //to have results automatically discarded when they terminate, so no need to collect:
  signal(SIGCHLD,SIG_IGN);

  //Main server loop to wait for a new connection and then fork off
  //child process to run handle_client(), with server continuing forever:
  while(1) {
    //Accept a new connection and get socket to use for client:
    if ((connect_fd = accept(listen_fd,(struct sockaddr *) NULL,NULL)) != -1) {
      //Make child process to handle this client:
      switch (fork()) {
      case -1:  //fork error:
        perror("fork() call failed");
        exit(EXIT_FAILURE);

      case 0:  //CHILD process:
        DTRACE("%ld:New subprocess to handle client: PID=%ld, PGID=%ld, SID=%ld\n",(long)getppid(),(long)getpid(),(long)getpgrp(),(long)getsid(0));
        //Don't need/want listen socket in subprocesses:
        close(listen_fd);
        //Call function to handle new client connection:
        handle_client(connect_fd);
        //Should not need, but guarantee child terminates:
        exit(EXIT_FAILURE);

      default:  //PARENT process:
          //Main server loop will just continue to wait for connections,
          //but first, close un-needed client FD to conserve FD's:
          //(Client FD will still be open in running subprocess(es).)
          close(connect_fd); } }
  }

  //Should never end up here, but just in case:
  return EXIT_FAILURE;
}



// Function to setup listening TCP server socket.
// Returns listening socket FD, else -1 on error.
int setup_listening_socket()
{
  int listen_fd;
  struct sockaddr_in servaddr;

  //Create socket for server to listen on:
  if ((listen_fd = socket(AF_INET,SOCK_STREAM,0)) == -1) {
    perror("socket() call failed");
    return -1; }

  //Set up socket so port can be immediately reused:
  int i=1;
  setsockopt(listen_fd,SOL_SOCKET,SO_REUSEADDR,&i,sizeof(i));

  //Set up server address struct:
  memset(&servaddr,0,sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons(PORTNUM);
  servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  //As an alternative for setting up address, could have had for declaration:
  //struct sockaddr_in servaddr = {AF_INET,htons(PORTNUM),htonl(INADDR_ANY)};

  //Give socket a name/address by binding to a port:
  if (bind(listen_fd,(struct sockaddr *)&servaddr,sizeof(servaddr)) == -1) {
    perror("bind() call failed");
    return -1; }

  //Start socket listening:
  if (listen(listen_fd,128) == -1) {
    perror("listen() call failed");
    return -1; }

  return listen_fd;
}



// Function to handle a new client connection:
//  (1) carries out the initial rembash protocol exhange;
//  (2) creates PTY;
//  (3) creates a subprocess in which to run bash, redirecting standard in/out/error to PTY;
//  (4) creates a second subprocess and relays data PTY<-->socket
//  (5) collects two subprocesses.
// Run in a separate subprocess for each client, terminates when done with client.
// Note that because a client connection could close at any time, socket read/write errors
// simply stop further handling of the client--they do not cause server termination.
void handle_client(int client_fd)
{
  const char * const server1 = "<rembash>\n";
  const char * const server2ok = "<ok>\n";
  const char * const server2err = "<error>\n";

  char *ptyslave_name;
  int ptymaster_fd;

  DTRACE("%ld:Starting protocol exchange for new client\n",(long)getpid());

  //Write initial protocol ID to client:
  if (write(client_fd,server1,strlen(server1)) == -1) {
    DTRACE("%ld:Error writing protocol ID to client\n",(long)getpid());
    exit(EXIT_FAILURE); }

  if (!validate_client_secret(client_fd)) {
    DTRACE("%ld:Client sent invalid secret\n",(long)getpid());
    write(client_fd,server2err,strlen(server2err));
    exit(EXIT_FAILURE); }

  if (!create_new_pty_pair(&ptymaster_fd,&ptyslave_name)) {
    DTRACE("%ld:Failed to create PTY pair for client\n",(long)getpid());
    exit(EXIT_FAILURE); }

  //Make child process to run bash in:
  switch (cpid[0] = fork()) {
  case -1:  //fork error:
    perror("fork() call failed");
    exit(EXIT_FAILURE);

  case 0:  //CHILD process:
    DTRACE("%ld:New subprocess for bash (pre setsid()): PID=%ld, PGID=%ld, SID=%ld\n",(long)getppid(),(long)getpid(),(long)getpgrp(),(long)getsid(0));
    close(client_fd);  //Client socket FD not required in bash subprocess
    close(ptymaster_fd);  //PTY master FD not required in bash subprocess
    run_shell_via_pty(ptyslave_name);
    //Should not get here since bash process should terminate, but just in case:
    //(Terminating child will close PTY slave leading to PTY master HUP.)
    exit(EXIT_FAILURE);
  }

  //PARENT process:

  //Ready to handle client commands, so send OK response to client:
  if (write(client_fd,server2ok,strlen(server2ok)) == -1) {
    DTRACE("%ld:Error writing OK to client\n",(long)getpid());
    exit(EXIT_FAILURE); }

  relay_data_between_socket_and_pty(client_fd,ptymaster_fd);

  //Done transferring data, make certain children are terminated:
  //(First, remove SIGCHLD handler and set to be ignored, so don't have to collect child.
  //Of course handler could be invoked before we can ignore it, but that is OK.)
  DTRACE("%ld:Client done, so remove SIGCHLD handler and terminate children\n",(long)getpid());
  signal(SIGCHLD,SIG_IGN);
  //Should already be terminated, so don't error check:
  kill(cpid[0],SIGTERM);
  kill(cpid[1],SIGTERM);

  //Collect any remaining child processes for this client (bash and data relay):
  while (waitpid(-1,NULL,WNOHANG) > 0);

  DTRACE("%ld:Client completed, so terminating client handler process...\n",(long)getpid());

  //Normal termination:
  exit(EXIT_SUCCESS);
}



// Function to perform initial rembash protocol exchange.
// Return indicates success/failure: 1 for true/success, 0 for false/fails
int validate_client_secret(int client_fd)
{
  const char * const secret = "<" SECRET ">\n";

  char buff[513];  //513 for valid string
  ssize_t nread;

  //Get and check shared secret:
  //(Note: read()'ing only secret message length bytes in case client
  //starts bash prematurely resulting in additional text in socket buffer;
  //ensures valid secret message by checking for '\n' at end of message.)
  if ((nread = read(client_fd,buff,strlen(secret))) <= 0) {
    DTRACE("%ld:Error/EOF reading secret from client\n",(long)getpid());
    return 0; }

  //Validate secret:
  buff[nread] = '\0';
  if (strcmp(buff,secret) != 0)
    return 0;

  //Success:
  return 1;
}



// Function to create new PTY master-slave pair.
// Passes master FD and slave name back via parameters.
// Return indicates success/failure: 1 for true/success, 0 for false/fails
int create_new_pty_pair(int *ptymasterfd_ptr, char **ptyslavename_ptr)
{
  int ptymaster_fd;
  char * ptyslave_name;

  if ((ptymaster_fd = posix_openpt(O_RDWR)) == -1) {
    perror("posix_openpt() call failed");
    return 0; }

  //Need to set PTY master FD so gets closed when bash exec'd:
  fcntl(ptymaster_fd,F_SETFD,FD_CLOEXEC);

  //grantpt(ptymaster_fd);  //Not required on Linux
  unlockpt(ptymaster_fd);

  char *ptyslave_nametmp = ptsname(ptymaster_fd);
  DTRACE("%ld:Created new PTY pair (%s)\n",(long)getpid(),ptyslave_nametmp);
  if ((ptyslave_name = malloc(strlen(ptyslave_nametmp)+1)) == NULL) {
    perror("malloc() call failed");
    return 0; }
  strcpy(ptyslave_name,ptyslave_nametmp);

  //Normal return:
  *ptymasterfd_ptr = ptymaster_fd;
  *ptyslavename_ptr = ptyslave_name;
  return 1;
}



// Function run in a new subprocess to setup bash to be
// run with I/O via PTY, and then exec bash.
// Always terminates process.
void run_shell_via_pty(char *ptyslave_name)
{
  //Create a new session for the new process:
  if (setsid() == -1) {
    perror("setsid() call failed");
    exit(EXIT_FAILURE); }
  
  //Setup PTY for bash subprocess:
  int ptyslave_fd;
  if ((ptyslave_fd = open(ptyslave_name,O_RDWR)) == -1) {
    perror("open() of PTY slave failed");
    exit(EXIT_FAILURE); }

  DTRACE("%ld:Execing bash (post setsid(): PGID=%ld, SID=%ld)\n",(long)getpid(),(long)getpgrp(),(long)getsid(0));

  //Setup stdin, stdout, and stderr redirection:
  if ((dup2(ptyslave_fd,0) == -1) || (dup2(ptyslave_fd,1) == -1) || (dup2(ptyslave_fd,2) == -1)) {
    perror("dup2() call for FD 0, 1, or 2 failed");
    exit(EXIT_FAILURE); }

  //Close PTY slave FD after duping since no longer needed:
  //(Don't want extra FD open in bash.)
  close(ptyslave_fd);

  //Start bash running:
  execlp("bash","bash",NULL);

  //Catch exec failure here:
  //(Terminating this child process will close PTY slave leading to PTY master HUP,
  //and so eventually removal of client.)
  DTRACE("%ld:Exec of bash failed!\n",(long)getpid());
  exit(EXIT_FAILURE);
}



// Function to relay data PTY<-->socket.
// Does this using by creating a subprocess and having:
//   -- parent do PTY-->socket
//   -- child do PTY<--socket
// Always returns (to handle_client()) for possible cleanup.
//
// Normal termination sequence will have parent process (PTY-master-->socket)
// get read() error from PTY master after bash terminates.
// Sets up SIGCHLD handler to catch premature/unexpected termination of child
// process (socket-->PTY-master).
// Note however that this handler will also catch bash subprocess termination,
// so handler has to be able to determine PID that caused signal!
//#pragma GCC diagnostic ignored "-Wunused-variable"
void relay_data_between_socket_and_pty(int client_fd, int ptymaster_fd)
{
  struct sigaction act;

  //Setup SIGCHLD handler to deal with child process terminating unexpectedly:
  //(Must be done before fork() in case child immediately terminates.)
  act.sa_sigaction = sigchld_handler;  //Note use of .sa_sigaction instead of .sa_handler
  act.sa_flags = SA_SIGINFO|SA_RESETHAND;  //SA_SIGINFO required to use .sa_sigaction instead of .sa_handler
  sigemptyset(&act.sa_mask);
  if (sigaction(SIGCHLD,&act,NULL) == -1) {
    perror("Error registering handler for SIGCHLD");
    return; }

  //Set SIGPIPE signals to be ignored, so that write to closed connection
  //produces error return, rather than signal-based termination:
  signal(SIGPIPE,SIG_IGN);

  //Make child process to read from client socket and write to PTY (bash):
  switch (cpid[1] = fork()) {
  case -1:  //fork error:
    perror("fork() call failed");
    return;

  case 0:  //CHILD process:
    DTRACE("%ld:New subprocess for data transfer socket-->PTY: PID=%ld, PGID=%ld, SID=%ld\n",(long)getppid(),(long)getpid(),(long)getpgrp(),(long)getsid(0));
    DTRACE("%ld:Starting data transfer socket-->PTY (FD %d-->%d)\n",(long)getpid(),client_fd,ptymaster_fd);
    //int transfer_status = transfer_data(client_fd,ptymaster_fd);
    transfer_data(client_fd,ptymaster_fd);
    DTRACE("%ld:Completed data transfer socket-->PTY, so terminating...\n",(long)getpid());

    //Child done transferring socket-->PTY-master, so terminate child process:
    exit(EXIT_SUCCESS);
  }  //End of child

  //PARENT process:

  //Loop, reading from PTY master (i.e., bash) and writing to client socket:
  DTRACE("%ld:Starting data transfer PTY-->socket (FD %d-->%d)\n",(long)getpid(),ptymaster_fd,client_fd);
  //int transfer_status = transfer_data(ptymaster_fd,client_fd);
  transfer_data(ptymaster_fd,client_fd);
  DTRACE("%ld:Completed data transfer PTY-->socket\n",(long)getpid());

  //Normal completion (return to handle_client()):
  return;
}
//#pragma GCC diagnostic pop


// Function to continually transfer data from source_fd to target_fd
// by looping read()'ing from source_fd and write()'ing to target_fd.
// Terminates on EOF/error read()'ing or error write()'ing.
// Handles partial write()'s by repeatedly write()'ing until complete.
// Returns true/success if terminates due to read() EOF, else false/failure.
int transfer_data(int source_fd, int target_fd)
{
  char buff[4096];
  ssize_t nread, nwrite;

  while ((nread = read(source_fd,buff,4096)) > 0) {
    if ((nwrite = write(target_fd,buff,nread)) == -1) break;
  }

  #ifdef DEBUG
  if (nread == -1) DTRACE("%ld:Error read()'ing from FD %d\n",(long)getpid(),source_fd);
  if (nwrite == -1) DTRACE("%ld:Error write()'ing to FD %d\n",(long)getpid(),target_fd);
  #endif

  //In case of an error, return false/fail:
  if (errno) return 0;

  //Normal true/success return:
  return 1;
}



// Handler for SIGCHLD during relay_data_between_socket_and_pty().
// Required to deal with premature/unexpected termination of child
// process that is handling data transfer PTY<--socket.
// Can get invoked by PTY<--socket child or bash child.
// Makes sure both children of client handler process are terminated,
// then terminates the PTY-->socket (handle_client) process too.
// (No point in continuing, as would just immediately get read/write
// error and then terminate anyway.)
void sigchld_handler(int signal, siginfo_t *sip, void *ignore)
{

  //Terminate other child process:
  if (sip->si_pid == cpid[0]) {
    DTRACE("%ld:SIGCHLD handler invoked due to PID:%d (bash), killing %d\n",(long)getpid(),sip->si_pid,cpid[1]);
    kill(cpid[1],SIGTERM); }
  else {
    DTRACE("%ld:SIGCHLD handler invoked due to PID:%d (socket-->PTY), killing %d\n",(long)getpid(),sip->si_pid,cpid[0]);
    kill(cpid[0],SIGTERM); }

  //Collect any/all killed child processes for this client:
  while (waitpid(-1,NULL,WNOHANG) > 0);

  //Terminate the parent without returning since no point:
  DTRACE("%ld:Terminating (client)...\n",(long)getpid());
  _exit(EXIT_SUCCESS);
}


// EOF
