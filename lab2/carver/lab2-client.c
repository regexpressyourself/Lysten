// Client Solution to Lab #2 in CS 407/591, Fall 2017.
// Author: Norman Carver (copyright 2017), Computer Science Dept., Southern Illinois University Carbondale.
// This material is provided for personal use by students enrolled in CS407/591 Fall 2017 only!
// Any other use represents infringement of the author's exclusive rights under US copyright law.
// In particular, posting this file to a website or in any way sharing it is expressly forbidden.
// Such sharing is also a violation of SIUC's Student Conduct Code (section 2.1.3), so may result
// in academic sanctions such as grade reduction.
//
// Usage: client SERVER_IP_ADDRESS (using dotted quad format)
//
// Properties:
// -- structured into multiple functions to simplify main
// -- uses plain read() with initial protocol exchange
// -- uses two subprocesses for commands-shell exchange:
//      (parent) read from socket and write to stdout
//      (child)  read from stdin and write to socket
// -- uses read()/write() for all I/O
// -- assumes all write()'s are done in full (no partials), which is true since blocking mode
// -- setups up SIGCHLD handler to deal with premature child process termination
// -- parent always collects child before termination
// -- TTY changed to noncanonical mode
// -- TTY reset to canonical mode before termination

// Note: can use the following command in client to get remote shell process info:
//   ps -p $$ -o 'pid pgid sid command'


#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/wait.h>
#include <termios.h>

#include "DTRACE.h"

//Declared constants:
#define PORTNUM 4070
#define SECRET "cs407rembash"


//Function prototypes:
int connect_to_server (const char *server_ip);
void carry_out_initial_protocol_exchange(int server_fd);
char *input_matches_protocol(int infd, const char * const protocol_str);
void exchange_commands_with_server(int server_fd);
int transfer_data(int source_fd, int target_fd);
void set_tty_noncanon_noecho();
void restore_tty_settings();
void cleanup_and_exit(int exit_status);
void sigchld_handler(int signal);


// Global variables:
// Initial TTY settings, stored in global to make it easy to access throughout program:
struct termios saved_tty_settings;


int main(int argc, char *argv[])
{
  char *server_ip;
  int server_fd;

  //Check for proper number of arguments:
  if (argc != 2) {
    fprintf(stderr,"Usage: client SERVER_IP_ADDRESS\n");
    exit(EXIT_FAILURE);
  }

  DTRACE("Client starting: PID=%ld, PGID=%ld, SID=%ld\n",(long)getpid(),(long)getpgrp(),(long)getsid(0));

  //Get server's IP address from arguments:
  server_ip = argv[1];

  //Create socket and connect to server:
  server_fd = connect_to_server(server_ip);

  carry_out_initial_protocol_exchange(server_fd);

  DTRACE("%ld:Setting TTY to noncanonical mode\n",(long)getpid());
  set_tty_noncanon_noecho();

  exchange_commands_with_server(server_fd);

  //Normal termination:
  DTRACE("%ld:Normal client termination...\n",(long)getpid());
  if (errno) cleanup_and_exit(EXIT_FAILURE);

  cleanup_and_exit(EXIT_SUCCESS);
}



// Function to create socket and connect to server:
// Returns socket FD if successful, else terminates process.
int connect_to_server (const char *server_ip)
{
  int sock_fd;
  struct sockaddr_in servaddr;

 //Create socket:
  if ((sock_fd = socket(AF_INET,SOCK_STREAM,0)) == -1) {
    perror("socket() call failed");
     exit(EXIT_FAILURE); }

  //Set up server address struct:
  memset(&servaddr,0,sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons(PORTNUM);
  inet_aton(server_ip,&servaddr.sin_addr);
  //older method: servaddr.sin_addr.s_addr = inet_addr(server_ip);

  //Connect to server:
  if (connect(sock_fd,(struct sockaddr *)&servaddr,sizeof(servaddr)) == -1) {
    perror("connect() call failed");
    exit(EXIT_FAILURE); }

  return sock_fd;
}



// Function to carry out rembash protocol exchange with connected server:
// Returns if successful, else terminates process.
void carry_out_initial_protocol_exchange(int server_fd)
{
  const char * const server1 = "<rembash>\n";
  const char * const server2 = "<ok>\n";
  const char * const secret = "<" SECRET ">\n";

  char *sock_input;

  //Get and check server protocol ID:
  if ((sock_input = input_matches_protocol(server_fd,server1)) != NULL) {
    fprintf(stderr,"Invalid protocol ID from server: %s\n",sock_input);
    exit(EXIT_FAILURE); }

  //Write shared secret message to server:
  if (write(server_fd,secret,strlen(secret)) == -1) {
    perror("Error writing shared secret to socket");
    exit(EXIT_FAILURE); }

  //Get and check server shared secret response:
  if ((sock_input = input_matches_protocol(server_fd,server2)) != NULL) {
    fprintf(stderr,"Invalid shared secret aknowledgement from server: %s\n",sock_input);
    exit(EXIT_FAILURE); }
}



// Function used by carry_out_initial_protocol_exchange to test if
// next input on socket matches what is supposed to be sent per protocol:
// Returns NULL if matches, else returns (incorrect) string that was read
// from socket (to support printing error message).
char *input_matches_protocol(int infd, const char * const protocol_str)
{
  static char buff[129];  //129 to handle extra '\0'', static so can return.
  ssize_t nread;

  if ((nread = read(infd,buff,strlen(protocol_str))) == -1) {
      perror("Error reading from server socket");
      exit(EXIT_FAILURE); }
  buff[nread] = '\0';

  if (strcmp(buff,protocol_str) == 0)
    return NULL;
  else
    return buff;
}



// Function to exchange commands and shell output with remote bash process:
// use two concurrent processes to simultaneously read commands from stdin and
// write to remote shell, and read output from shell and write to stdout.
// Normally returns when exchange completed (does not leave child running).
// terminates process if serious error (sigaction() or fork() error).
void exchange_commands_with_server(int server_fd)
{
  pid_t cpid;
  struct sigaction act;

  //Setup SIGCHLD handler to deal with child process terminating unexpectedly:
  //(Must be done before fork() in case child immediately terminates.)
  act.sa_handler = sigchld_handler;
  act.sa_flags = 0;
  sigemptyset(&act.sa_mask);
  if (sigaction(SIGCHLD,&act,NULL) == -1) {
    perror("Error registering handler for SIGCHLD");
    exit(EXIT_FAILURE); }

  //Set SIGPIPE signals to be ignored, so that write to closed connection
  //produces error return, rather than signal-based (SIGPIPE) termination:
  signal(SIGPIPE,SIG_IGN);

  //Make child process to read and send commands:
  switch (cpid = fork()) {
  case -1:  //fork error:
    perror("fork() failed");
    exit(EXIT_FAILURE);

  case 0:  //CHILD process:
    DTRACE("%ld:Subprocess created for data transfer stdin-->socket: PID=%ld, PGID=%ld, SID=%ld\n",(long)getppid(),(long)getpid(),(long)getpgrp(),(long)getsid(0));

    //Loop, reading commands from stdin and sending to remote shell/socket:
    //(Should run until terminated by parent process, unless problems occur.)
    DTRACE("%ld:Starting data transfer stdin-->socket (FD 0-->%d)\n",(long)getpid(),server_fd);
    int transfer_status = transfer_data(0,server_fd);
    DTRACE("%ld:Completed data transfer stdin-->socket\n",(long)getpid());

    //Check whether read-write loop terminated due to error:
    if (!transfer_status) {
      perror("Error reading commands from stdin and/or writing to shell/socket");
      exit(EXIT_FAILURE); }

    //Get here only if stdin returned EOF, so not an error:
    exit(EXIT_SUCCESS);
  }  //End of child

  //PARENT process:

  //Loop, reading output from remote shell/socket and printing to stdout:
  DTRACE("%ld:Starting data transfer socket-->stdout (FD %d-->1)\n",(long)getpid(),server_fd);
  int transfer_status = transfer_data(server_fd,1);
  DTRACE("%ld:Completed data transfer socket-->stdout\n",(long)getpid());

  //Check whether read-write loop terminated due to error:
  if (!transfer_status)
    perror("Error reading from shell socket and/or writing to stdout");

  //Eliminate SIGCHLD handler and kill child:
  DTRACE("%ld:Normal transfer completion, terminating child (%ld)\n",(long)getpid(),(long)cpid);
  signal(SIGCHLD,SIG_IGN);
  kill(cpid,SIGTERM);

  //Done communicating with remote shell process:
  return;
}



// Function to continually transfer data from source_fd to target_fd
// by looping read()'ing from source_fd and write()'ing to target_fd.
// Terminates on EOF/error read()'ing or error write()'ing.
// Handles partial write()'s by repeatedly write()'ing until complete.
// Returns true/success if terminates due to read() EOF, else false/failure.
int transfer_data(int source_fd, int target_fd)
{
  char buff[4096];
  ssize_t nread;

  while ((nread = read(source_fd,buff,4096)) > 0) {
      if (write(target_fd,buff,nread) == -1) break;
  }

  if (nread == 0)
    DTRACE("%ld:EOF on FD %d!\n",(long)getpid(),source_fd);

  //In case of an error, return false/fail:
  if (errno) return 0;

  //Normal true/success return:
  return 1;
}



// Various functions to manipulate TTY settings:

void set_tty_noncanon_noecho()
{
  struct termios tty_settings;

  if (tcgetattr(STDIN_FILENO, &tty_settings) == -1) {
    perror("Getting TTY attributes failed");
    exit(EXIT_FAILURE); }

  //Save current settings so can restore:
  saved_tty_settings = tty_settings;

  tty_settings.c_lflag &= ~(ICANON | ECHO);
  tty_settings.c_cc[VMIN] = 1;
  tty_settings.c_cc[VTIME] = 0;
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &tty_settings) == -1) {
    perror("Setting TTY attributes failed");
    exit(EXIT_FAILURE); }

  return;
}

void restore_tty_settings()
{
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_tty_settings) == -1) {
    perror("Restoring TTY attributes failed");
    exit(EXIT_FAILURE); }

  return;
}



void cleanup_and_exit(int exit_status)
{
  restore_tty_settings();

  //Collect child and get its exit status:
  int childstatus;
  wait(&childstatus);

  //Determine if exit status should be failure:
  if (exit_status==EXIT_FAILURE || !WIFEXITED(childstatus) || WEXITSTATUS(childstatus)!=EXIT_SUCCESS)
    exit(EXIT_FAILURE);

  exit(EXIT_SUCCESS);
}



// Handler for SIGCHLD:
// Collects child and terminates parent.
// Will get called only if child process (tty-->socket) terminates first.
// This could be due to an I/O error or simply to getting EOF from ^-d.
// Returns appropriate exit status based on local errors and child status.
void sigchld_handler(int signal)
{
  DTRACE("%ld:Caught signal from subprocess termination...terminating!\n",(long)getpid());

  cleanup_and_exit(EXIT_SUCCESS);
}

//EOF
