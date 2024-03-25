/* INCLUDES HERE */
#include "talk.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <getopt.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/select.h>
#include <unistd.h>
#include <pwd.h>
#include <poll.h>
#include <signal.h>
#include <ctype.h>

#define SERVER_MODE 0
#define CLIENT_MODE 1
#define PORT_MIN 1024
#define PORT_MAX 65535
#define BASE_10 10
#define LOCAL 0
#define REMOTE (LOCAL+1)
#define MAX_LINE 1024
#define OK_MSG "ok"
#define NOT_OK_MSG "not ok"
#define CNCTN_CLOSED_MSG "Connection closed. ^C to terminate."
#define MSG_BYE "bye"
#define MAX_CONNECTIONS 100
#define DEFAULT_BACKLOG MAX_CONNECTIONS
#define LOGIN_NAME_MAX 256


void client_side(char* hostname, int portNum, bool verbose, bool dontAsk, 
                                                                bool Windowing);
void server_side(int portNum, bool verbose, bool dontAsk, bool Windowing);
void sigint_action(int signum);



int main(int argc, char *argv[]) {
    int mode = SERVER_MODE;
    int options;
    char* optptr;
    bool verbose = false; /* by default not in verbose mode*/
    bool dontask = false; /* by default ask for authorization */
    bool windowing = true; /* by default split screens */
    char *hostname;
    int port_num;
 
 
    /* parse command line and use result to pick server or client path *
    * usage: mytalk [ -v ] [ -a ] [ -N ] [ hostname ] port            *
    * -v increases verbosity. May be repeated.                         *
    * -a (server) accept a connection without asking                   *
    * -N do not start ncurses windowing                               */
    
    while((options = getopt(argc, argv, "vaN")) != -1) {
        switch(options) {
            case 'v':
                verbose = true;
                break;
            case 'a':
                dontask = true;
                break;
            case 'N':
                windowing = false;
                break;
            case '?':
                printf(
                    "usage: mytalk [ -v ] [ -a ] [ -N ] [ hostname ] port\n");
                exit(1);
                break;
        }
    }
 
    switch (argc-optind) {
        case 1:
            /* when port number only, it means server mode*/
            mode = SERVER_MODE;    
            hostname = "";
            port_num = strtol(argv[optind], &optptr, BASE_10);
            if(port_num < PORT_MIN || port_num > PORT_MAX){ /*outside range */
                printf("invalid port number.\n");
                printf(
                    "usage: mytalk [ -v ] [ -a ] [ -N ] [ hostname ] port\n");
                exit(1);
            }
            break;
        
        case 2:
            /* hostname and port number means client mode */
            mode = CLIENT_MODE;
            hostname = argv[optind];
            port_num = strtol(argv[optind +1], &optptr, BASE_10);
            if(port_num < PORT_MIN || port_num > PORT_MAX){ /* outside range */
                printf("invalid port number.\n");
                printf(
                    "usage: mytalk [ -v ] [ -a ] [ -N ] [ hostname ] port\n");
                exit(1);
            }
            break;

        default:
            /* either missing port number or too many arguments*/
            printf("usage: mytalk [ -v ] [ -a ] [ -N ] [ hostname ] port\n");
            exit(1);
            break;
    }
 
    if(verbose) {
        /* visualization only*/
        printf(
            "Mytalk Mode =  %s\n", mode == SERVER_MODE ? "Server" : "Client");
        printf("hostname: %s\n", hostname[0] == '\0' ? "N/A" : hostname);
        printf("Port number: %d\n", port_num);
        printf("Verbose = %s\n", verbose == true ? "On" : "Off");
        printf(
            "Authorization required = %s\n", dontask == false ? "On" : "Off");
        printf("Windowing = %s\n", windowing == true ? "On" : "Off");
    }
 
    switch (mode) {
        case CLIENT_MODE:
            /* Either activate server mode */
            client_side(hostname, port_num, verbose, dontask, windowing);
            break;
        
        case SERVER_MODE:
            /* Or activate client mode */
            server_side(port_num, verbose, dontask, windowing);
            break;
        
        default: /* should not happen*/
            return 0;
            break;
    }
 
    return 0;
}
 


/* HELPER FUNCTIONS HERE */
void client_side(char* hostname, int portNum, bool verbose, bool dontAsk, 
                                                            bool windowing) {
    struct hostent *hostent;
    int sockfd, mlen, len;
    struct sockaddr_in sa;
    char* client_username;
    struct pollfd fds[REMOTE+1];
    char readBuffer[MAX_LINE], writeBuffer[MAX_LINE];
    bool dialogueDone = false;

    if(verbose){
        printf("Activating client's side...");
        set_verbosity(1); /* library function for verbosity */
    }
    /* based on Lecture 21 minichat poll version fig. 93 & 95*/
    if ((hostent = gethostbyname(hostname)) == NULL) { /* who we’re talking to*/
        perror("probelm getting hostname");
        exit(1);
    } 
    /* Create the socket */
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        /* client and server will use TCP (SOCK_STREAM) for communication */
        perror("unable to create client socket");
        exit(1);
    } 
    
    /* setting file descriptor contents */
    fds[LOCAL].fd = STDIN_FILENO;
    fds[LOCAL].events = POLLIN;
    fds[LOCAL].revents = 0;
    fds[REMOTE]=fds[LOCAL];
    fds[REMOTE].fd = sockfd;
    
    
    /* connect it */
    sa.sin_family = AF_INET;
    sa.sin_port = htons(portNum); /* use our port */
    sa.sin_addr.s_addr = *(uint32_t*)hostent->h_addr_list[0]; /* net order */
    if (connect(sockfd, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
        perror("error connecting to client socket");
        exit(1);
    }
    
    /* When a client starts, it attempts to open a connection to the server on *
    * the given host at the given port. If the connection is established, the  *
    * client sends a packet containing the user’s username and waits for a    *
    * response.*/
    if ((client_username = (getpwuid(getuid()))->pw_name) == NULL) {
        perror("problem getting username");
        exit(-1);
    }
    /* send and wait on poll */
    if (send(sockfd, client_username, strlen(client_username), 0) == -1) {
        perror("error sending username to socket");
        exit(1);
    }
    printf("Waiting for a reponse from %s\n", hostname);
    if (poll(fds, sizeof(fds)/sizeof(struct pollfd), -1) == -1) {
        perror("failed to poll remote");
        exit(1);
    }
    if ((mlen = recv(sockfd, writeBuffer, sizeof(writeBuffer),0)) == -1) {
        perror("initial recv failed");
        exit(1);
    }
    if (strncmp((char*)writeBuffer, OK_MSG, strlen(OK_MSG)) != 0) {
        printf("%s declined connection.\n", hostname);
        fflush(stdout);
        exit(0); /* this is a successful exit; simply rejected connection */
    } else {
        if(verbose) {
            printf("ok received from host.\n");
            fflush(stdout);
        }
    }
    readBuffer[0] = '\0';
    writeBuffer[0] = '\0';

    /* starting chat/dialogue */
    if (windowing) {
        start_windowing(); /* library function */
    }
    do { /* polling loop*/
        if (poll(fds, sizeof(fds)/sizeof(struct pollfd), -1) == -1) {
            perror("failed to poll remote");
            exit(1);
        }
        if (fds[LOCAL].revents & POLLIN ) { /* client side event */
            update_input_buffer(); /* lib provided; get pending data everytime*/
            if(has_whole_line()) { /* library provided */
                len = read_from_input(readBuffer, sizeof(readBuffer));
                /* combination above before reading avoids blocking */
                if (len >= 0) { /* something to send; 0 for ^D */
                    if (send(sockfd, readBuffer, len, 0) == -1) {
                        perror("problem sending message");
                        exit(1);
                    }
                }
                if (has_hit_eof()) { /* we are done */
                    stop_windowing();
                    close(sockfd);
                    exit(0); /* successful exit */
                }
            }
        }
        if (fds[REMOTE].revents & POLLIN) { /* server side event */
            /* receive and write to screen */
            if ((mlen = recv(sockfd, writeBuffer, 
                                            sizeof(writeBuffer), 0)) == -1) {
                perror("problem receiving");
                exit(1);
            }
            if (mlen == 0) { /* EOF; we are done; server gone */
                write_to_output(CNCTN_CLOSED_MSG, strlen(CNCTN_CLOSED_MSG));
                close(sockfd);
            }
            write_to_output(writeBuffer, mlen);
        }
        if (!strncmp(writeBuffer, MSG_BYE, strlen(MSG_BYE))) { /*check if bye */
            dialogueDone = true;
            write_to_output(CNCTN_CLOSED_MSG, strlen(CNCTN_CLOSED_MSG));
            close(sockfd);
        }
        if (!strncmp(readBuffer, MSG_BYE, strlen(MSG_BYE))) { /* check if bye*/
            dialogueDone = true;
            stop_windowing();
            close(sockfd);
            exit(0);
        }
    } while (!dialogueDone);
    /* done with dialogue; waiting for ^C */
    signal(SIGINT, sigint_action);
    while(1) {
        pause(); /* waiting for ^C*/
    }
    exit(0);
    return;
}


void server_side(int portNum, bool verbose, bool dontAsk, bool windowing) {
    int sockfd, newsock, mlen, len;
    bool dialogueDone = false;
    struct sockaddr_in sa;
    struct sockaddr_in peerinfo;
    socklen_t slen;
    char readBuffer[MAX_LINE], writeBuffer[MAX_LINE];
    char addr[INET_ADDRSTRLEN];
    char remoteUsername[LOGIN_NAME_MAX+1];
    struct pollfd fds[MAX_CONNECTIONS+1];


    if(verbose){
        printf("Activating server's side...\n");
        fflush(stdout);
        set_verbosity(1); /* library function for verbosity */
    }
    /* based on Lecture 21 minichat poll version fig. 97 & 96*/

    /* create the socket */
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("could not host socket");
        exit(1);
    }

    /* bind it to our address*/
    sa.sin_family = AF_INET;
    sa.sin_port = htons(portNum); /* use our port */
    sa.sin_addr.s_addr = htonl(INADDR_ANY); /* all local interfaces */
    if (bind(sockfd, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
        perror("unable to bind to host port");
        exit(1);
    }
    /* server listening for connection requests */
    if (listen(sockfd, DEFAULT_BACKLOG) == -1) {
        perror("problem listening");
        exit(1);
    }

    /* accept connection and chat */
    slen = sizeof(peerinfo);
    if ((newsock = accept(sockfd, (struct sockaddr *)&peerinfo, &slen)) == -1) {
        perror("problem accepting");
        exit(1);
    }
    /* converting addr to string*/
    if (inet_ntop(AF_INET, &peerinfo.sin_addr.s_addr, addr, 
                                                        sizeof(addr)) == NULL) {
        perror("inet_ntop problem");
        exit(1);
    }
    /* receiving first message */
    if ((mlen = recv(newsock, remoteUsername, 
                                            sizeof(remoteUsername), 0)) == -1) {
        perror("problem receiving");
        exit(1);
    }
    remoteUsername[mlen] = '\0'; /* null at the end to work with printf */
    if (verbose) {
        printf("New connection from: %s:%d\n", addr, htons(peerinfo.sin_port));
    }
    printf("Mytalk request from %s@%s. Accept (y/n)? ", (char*)remoteUsername, 
                                                                (char*)addr);
    fflush(stdout);
    if (dontAsk && verbose) {
        printf("\n-a flag. Accepted connection.\n");
        fflush(stdout);
    }
    if (dontAsk || tolower(getchar()) == 'y') {
        if (send(newsock, OK_MSG, strlen(OK_MSG), 0) == -1) {
            perror("failed to send acceptance");
            exit(1);
        }
    }
    else {
        if (send(newsock, NOT_OK_MSG, sizeof(NOT_OK_MSG), 0) == -1) {
            perror("failed to send rejection");
            exit(1);
        }
        close(newsock);
        close(sockfd);
        exit(0);
    }

    /* setting file descriptor */
    fds[LOCAL].fd = STDIN_FILENO;
    fds[LOCAL].events = POLLIN;
    fds[LOCAL].revents = 0;
    fds[REMOTE]=fds[LOCAL];
    fds[REMOTE].fd = newsock;
    writeBuffer[0] = '\0';
    readBuffer[0] = '\0';

    /* all set; start dialogue/chat */
    if (windowing) {
        start_windowing();
    }
    do {
        if (poll(fds, sizeof(fds)/sizeof(struct pollfd), -1) == -1) {
            perror("failed to poll remote");
            exit(1);
        }
        if (fds[LOCAL].revents & POLLIN) { /* server side event */
            /* read a line and send to other side */
            /* using library functions to avoid IO problems*/
            update_input_buffer(); /* lib provided; get pending data everytime*/
            if(has_whole_line()) { /* library provided */
                len = read_from_input(readBuffer, sizeof(readBuffer));
                /*combination above before reading avoids blocking*/
                if (len >= 0) { /* something to send; 0 for ^D*/
                    if (send(newsock, readBuffer, len, 0) == -1) {
                        perror("problem sending");
                        exit(1);
                    }
                }
                if (has_hit_eof()) { /* we are done */
                    stop_windowing();
                    close(sockfd);
                    close(newsock);
                    exit(0); /* successful exit */
                }
            }
        }
        if (fds[REMOTE].revents & POLLIN) { /* client side event */
            /* receive and write to screen */
            if ((mlen = recv(newsock, writeBuffer, 
                                            sizeof(writeBuffer), 0)) == -1) {
                perror("problem receiving");
                exit(1);
            }
            if (mlen == 0) { /* EOF; we are done; client gone */
                write_to_output(CNCTN_CLOSED_MSG, strlen(CNCTN_CLOSED_MSG));
                close(sockfd);
                close(newsock);
            }
            write_to_output(writeBuffer, mlen);
        }
        if (!strncmp(readBuffer, MSG_BYE, strlen(MSG_BYE))) { /* check if bye */
            dialogueDone = true;
            stop_windowing();
            close(sockfd);
            close(newsock);
            exit(0);
        }
        if (!strncmp(writeBuffer, MSG_BYE, strlen(MSG_BYE))) { /* check if bye*/
            dialogueDone = true;
            write_to_output(CNCTN_CLOSED_MSG, strlen(CNCTN_CLOSED_MSG));
            close(sockfd);
            close(newsock);
        }
    } while (!dialogueDone);

    /* done with dialogue; waiting for ^C */
    signal(SIGINT, sigint_action);
    while(1) {
        pause(); /* waiting for ^C*/
    }
    printf("\n");
    exit(0);

    return;
}

void sigint_action(int signum) {
    stop_windowing();
    exit(0);
}