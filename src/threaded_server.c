#ifdef _WIN32
    /* See http://stackoverflow.com/questions/12765743/getaddrinfo-on-win32 */
    #ifndef _WIN32_WINNT
        #define _WIN32_WINNT 0x0501  /* Windows XP. */
    #endif
    #include <winsock2.h>
    #include <Ws2tcpip.h>
    #include <tchar.h>
    #include <windows.h>
    #include <pthread.h>
#else
    /* Assume that any non-Windows platform uses POSIX-style sockets instead. */
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <netdb.h>  /* Needed for getaddrinfo() and freeaddrinfo() */
    #include <netinet/in.h>
    #include <pthread.h>
#endif


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>


int errnum;
int balance = 0;
socklen_t socklen;
int sockfd, new_sock;
// address structs
struct sockaddr_in6 addr, client_addr;

struct thread_info {    /* Used as argument to thread_start() */
    int                 thread_num;       /* Application-defined thread # */
    int                 sockfd;
    struct sockaddr_in6  client_addr;
};

pthread_t tid[10], dispatch;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;


 int sockInit(void)
 {
     #ifdef _WIN32
         WSADATA wsa_data;
         return WSAStartup(MAKEWORD(2,2), &wsa_data);
     #else
         return 0;
     #endif
 }

 int sockQuit(void)
 {
     #ifdef _WIN32
         return WSACleanup();
     #else
         return 0;
     #endif
 }


void
handle_error_en(int en, char *msg) {
    errno = en;
    perror(msg);
    exit(EXIT_FAILURE);
}

void
handle_error(char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

int
resend(int fd, void *buf, size_t n) {
    long len;
    len = send(fd, buf, n, 0);
        handle_error("send");
    if(len < 64) {
        do {    // resend until all bytes have been sent
            len = send(fd, buf, n, 0);
            handle_error("send");
        } while(len < 64);
    }
    memset(buf, 0, sizeof(buf));

    return 0;
}

void
*workerThread(void *thread_arg) {

    printf("\nThread %lu started\n", pthread_self());

    long len;
    pthread_mutex_lock(&lock);
    struct thread_info *t_info = thread_arg;
    struct sockaddr_in6 client_addr_t = t_info->client_addr;
    int t_sock = t_info->sockfd;
    pthread_mutex_unlock(&lock);

    char msg[64];
    memset(&msg, 0, sizeof(msg));

    char host[NI_MAXHOST], service[NI_MAXSERV];
    socklen_t client_len = sizeof(client_addr_t);




    if(t_sock < 0) {
            handle_error_en(t_sock, "t_sock");
        } else {
            // recieve first message
            recv(sockfd, msg, sizeof(msg), 0);
            getnameinfo((struct sockaddr * ) &client_addr_t, client_len, host, sizeof(host), service, sizeof(service), NI_NUMERICSERV);
            printf("\nRecieved connection from %s on port %s.\n", host, service);

            strcpy(msg, "Welcome!");
            len = send(t_sock, msg, sizeof(msg), 0);
            if(len < 64) {
                do {    // resend until all bytes have been sent
                    len = send(t_sock, msg, sizeof(msg), 0);
                } while(len < 64);
            }
            memset(msg, 0, sizeof(msg));
        }

        do {
            // Kontostand an Client senden
            printf("Sending current balance of %i Euro to client %s\n", balance, host);
            // Schreibe aktuellen Kontostand in den Buffer
            sprintf(msg, "%d", balance);

            // Sende Kontostand an Client
            len = send(t_sock, msg, sizeof(msg), 0);

            // Prüfe ob alle Bytes versendet wurden
            if(len < 64)
                resend(t_sock, msg, sizeof(msg));
            else
                printf("Sent successfully\n");
            memset(msg, 0, sizeof(msg));


            // Recieve client operation
            len = recv(t_sock, msg, sizeof(msg), 0);
            printf("\nRecieved %li Bytes\n", len);


            // Adjust balance
            pthread_mutex_lock(&lock);
            balance = balance + (int) strtol(msg, NULL, 10);
            pthread_mutex_unlock(&lock);
        } while(len > 0);

        printf("\nClient %s disconnected\n", host);

    return NULL;
}


// the dispatcher handles incoming connections and creates worker threads for each one
void
*dispatcherThread() {

    printf("Dispatcher started\nWaiting for connections...\n");

    pthread_attr_t attr;
    errnum = pthread_attr_init(&attr);
    if(errnum != 0)
        handle_error_en(errnum, "pthread_attr_init");

    // set detach state to true, so worker threads clean up after themselves
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    // create thread info for worker threads
    struct thread_info t_info;
    memset(&t_info, 0, sizeof(struct thread_info));

    // main loop for handling incoming connections
    while((new_sock = accept(sockfd, (struct sockaddr *) &client_addr, &socklen)) > 0) {
        int i = 0;

        t_info.sockfd = new_sock;
        t_info.client_addr = client_addr;
        t_info.thread_num = i;

        if(pthread_create(&tid[i++], &attr, workerThread, &t_info) != 0) {
            printf("Work thread creation failed!\n");
        }

        //sleep(1);
    }


    // destroy unneeded thread attributes
    errnum = pthread_attr_destroy(&attr);
    if (errnum != 0)
        handle_error_en(errnum, "pthread_attr_destroy");

    close(new_sock);

    return NULL;
}


int
main(int argc, char *argv[]) {

    // check args
    if(argc == 2) { // only 2 args allowed (program name, service)
        if(strtol(argv[1], NULL, 0) > 65535 || strtol(argv[1], NULL, 0) < 1025) {
            fprintf(stderr, "%s: Invalid Arguments\nUsage: %s [1025 - 65535]\n", argv[0], argv[0]);
            exit(EXIT_FAILURE);
        }
    }
    else {
        fprintf(stderr, "%s: Invalid Arguments\nUsage: %s [1025 - 65535]\n", argv[0], argv[0]);
        exit(EXIT_FAILURE);
    }


    // initialize WSA for windows
    sockInit();


    // initialize address structs
    memset(&addr, 0, sizeof(struct sockaddr_in6));
    memset(&client_addr, 0, sizeof(struct sockaddr_in6));


    // create the socket for the server
    printf("Initializing socket...\n");
    sockfd = socket(AF_INET6, SOCK_STREAM, 0);
    if(sockfd < 0)
        handle_error("Couldn't create socket: ");
    // enable IPv4 addresses via IPv6
    setsockopt(sockfd, IPPROTO_IPV6, IPV6_V6ONLY, &(int){0}, sizeof(int) < 0);

    // set the IP address and get the service from args
    printf("Initializing address...\n");
    addr.sin6_family = AF_INET6;  // currently only IPV4 support
    addr.sin6_port = htons((unsigned short) strtol(argv[1], NULL, 0));
    addr.sin6_addr = in6addr_any;


    // bind the socket
    printf("Binding socket...\n");
    errnum = bind(sockfd, (struct sockaddr *) &addr, sizeof(addr));
    if(errnum < 0)
        handle_error_en(errnum, "Couldn't bind the address");


    // Holen uns die uns zugewiesene Adresse ab und geben den Port aus
    errnum = getsockname(sockfd, (struct sockaddr *) &addr, &socklen);
    if(errnum < 0)
        handle_error_en(errnum, "getsockname failed: ");
    printf("\nSuccess!\nThe server is running on port %hu\n", ntohs(addr.sin6_port));


    // set socket into passive listen state
    errnum = listen(sockfd, 5);
    if(errnum < 0)
        handle_error_en(errnum, "Error with listen: ");

    // create dispatch thread which handles incoming connections
    pthread_create(&dispatch, NULL, dispatcherThread, NULL);
    // main program is done, wait for dispatcher
    pthread_join(dispatch, NULL);

    // shutdown WSA for windows
    sockQuit();

    close(sockfd);

    return 0;
}