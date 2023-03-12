
#include <signal.h>
#include <malloc.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "chatServer.h"

static int end_server = 0;
void intHandler(int SIG_INT) {
    if (SIG_INT == SIGINT){
        end_server = 1;
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) { //check if the correct number of command line arguments (port number) has been provided
        fprintf(stdout, "Usage: chatServer <port>\n");
        exit(EXIT_FAILURE);
    }
    int port = (int) strtol(argv[1], (char **) NULL, 10); //convert the port number from a string to an integer and assigns it to the variable 'port'
    if(port < 0){  //check if the port is valid
        fprintf(stdout, "Usage: chatServer <port>\n");
        exit(EXIT_FAILURE);
    }
    signal(SIGINT, intHandler); //set up signal handler to capture the interrupt signal (SIGINT) and assigns the intHandler function to handle it
    conn_pool_t *pool = malloc(sizeof(conn_pool_t) ); //allocate memory for the connection pool
    if (pool == NULL) {
        perror("error: malloc\n");
        exit(EXIT_FAILURE);
    }
    init_pool(pool); //initialize the connection pool
    int socketServer; //create a socket for the server
    if ((socketServer = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("error: socket\n");
        return -1;
    }
    int on = 1;//set the socket to non-blocking mode
    if (ioctl(socketServer, (int) FIONBIO, (char *) &on) == -1) {
        perror("error: ioctl\n");
        close(socketServer);
        return -1;
    }
    struct sockaddr_in serverAddress; //set up the server's address information (AF_INET, port, INADDR_ANY)
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(port);
    serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(socketServer, (struct sockaddr *) &serverAddress, sizeof(serverAddress)) < 0) {//bind the socket to that address
        perror("error: bind\n");
        close(socketServer);
        exit( -1);
    }
    if (listen(socketServer, 10) == -1) {//set the server to listen for incoming connections
        perror("error: listen\n");
        close(socketServer);
        return -1;
    }
    FD_SET(socketServer, &(pool->read_set)); //add the socket descriptor to the read set of the connection pool
    int conn;
    char *msg;
    do {//loop that repeatedly calls the select() function to monitor the read and write sets of the connection pool for available data
        int max = -1;
        conn_t *curr;
        for (curr = pool->conn_head; curr != NULL; curr = curr->next) { //find the maximum socket descriptor
            if (curr->fd > max) {
                max = curr->fd;
            }
        }
        if (socketServer > max) {
            max = socketServer;
        }
        pool->maxfd = max;
        pool->ready_read_set = pool->read_set;
        pool->ready_write_set = pool->write_set;
        printf("Waiting on select()...\nMaxFd %d\n", pool->maxfd);  //Waiting on select()
        if ((pool->nready = select(pool->maxfd + 1, &(pool->ready_read_set), &(pool->ready_write_set), NULL, NULL)) == -1) {break;}
        int i=0;
        for ( i = 0; (i < (pool->maxfd + 1)) && (pool->nready > 0); i++) {
            if (FD_ISSET(i, &(pool->ready_read_set))) {
                if (i == socketServer) {
                    conn = accept(socketServer, NULL, NULL);   //new connection is accepted
                    if (conn >= 0) {
                        if (add_conn(conn, pool) == 0) {
                            FD_SET(conn, &(pool->read_set));
                        }
                    }


                    FD_CLR(socketServer, &(pool->ready_read_set));
                    pool->nready--;
                } else {
                    int byte_received = 0;
                    char buffer[BUFFER_SIZE + 1] = "";
                    memset(buffer, '\0', BUFFER_SIZE + 1);
                    printf("Descriptor %d is readable\n", i); // Read data from the socket
                    byte_received = (int) read(i, buffer, BUFFER_SIZE);
                    if(byte_received>=2){
                    byte_received= byte_received-2;}
                    else{
                     byte_received=0;
                    }
                    printf("%d bytes received from sd %d\n", byte_received, i);
                    if (byte_received <= 0) { // Check if the connection has been closed
                        if (byte_received == 0) {// If the connection has been closed by the client, remove it
                            printf("Connection closed for sd %d\n",i);
                            printf("removing connection with sd %d \n", i);
                        } else {
                            printf("removing connection with sd %d \n", i);
                        }
                        remove_conn(i, pool);
                    }
                    msg= buffer;  // Allocate memory for the received message and copy it over
                    pool->nready--;
                    add_msg(i, msg, (int) strlen(msg), pool);
                }
            }
            if (FD_ISSET(i, &(pool->ready_write_set))) {
                write_to_client(i, pool);
                pool->nready--;
            }
        }
    } while (end_server == 0);
    conn_t *curr;
    for (curr = pool->conn_head; curr != NULL; curr = pool->conn_head) {
        printf("removing connection with sd %d \n", curr->fd);
        remove_conn(curr->fd, pool);
    }
    FD_ZERO(&(pool->read_set));
    FD_ZERO(&(pool->ready_read_set));
    FD_ZERO(&(pool->write_set));
    FD_ZERO(&(pool->ready_write_set));
    free(pool);
    close(socketServer);
    return 0;
}

int init_pool(conn_pool_t *pool) {
    pool->maxfd = -1;//set the maximum file descriptor in the pool to -1
    pool->nready = 0;//initialize the number of ready file descriptors to 0
    FD_ZERO(&(pool->read_set));  //zero out the read, ready_read, write, and ready_write sets
    FD_ZERO(&(pool->ready_read_set));
    FD_ZERO(&(pool->write_set));
    FD_ZERO(&(pool->ready_write_set));
    pool->conn_head = NULL; //set the head of the connection list in the pool to NULL
    pool->nr_conns = 0; //set the number of connections in the pool to 0
    return 0;
}

int add_conn(int sd, conn_pool_t *pool) { //allocate memory for new connection
    conn_t *conn = (conn_t *) malloc(sizeof(conn_t) * 1);
    conn->fd = sd;
    conn->write_msg_head = NULL; //initialize pointers for the connection's write message queue to NULL
    conn->write_msg_tail = NULL;
    conn->prev = NULL;//initialize pointers for the connection's next and previous connections in the linked list to NULL
    conn->next = NULL;
    if (pool->conn_head != NULL) {  //check if the pool already has connections
        pool->conn_head->prev = conn;//set the previous connection of the head of the connection list to the new connection
    }
    conn->next = pool->conn_head;//set the next pointer of the new connection to the current head of the connection list
    pool->conn_head = conn; //set the new connection as the head of the connection list
    pool->nr_conns++; //increment the number of connections in the pool
    return 0;
}

int remove_conn(int sd, conn_pool_t *pool) {
    conn_t *current = pool->conn_head; // pointer to current connection
    while (current != NULL) {//iterate through the connections in the pool
        if (current->fd == sd) {//check if connection's file descriptor matches the given socket descriptor
            if (current->prev == NULL) {// remove connection from linked list
                pool->conn_head = current->next;
                if (pool->conn_head != NULL) {
                    pool->conn_head->prev = NULL;
                }
            } else {
                current->prev->next = current->next;
                if (current->next != NULL) {
                    current->next->prev = current->prev;
                }
            }
            msg_t* msg = current->write_msg_head;
            while (msg) {  //free memory allocated for messages in the connection's write queue
                msg_t* next_msg = msg->next;
                free(msg->message);
                free(msg);
                msg = next_msg;
            }
            current->write_msg_head = NULL;// reset the pointers for the linked list of message objects
            current->write_msg_tail = NULL;
            free(current);//free memory allocated for the connection
            break;
        }
        current = current->next;
    }
    FD_CLR(sd, &(pool->read_set));// clear the file descriptor from the ready and read sets in the connection pool
    FD_CLR(sd, &(pool->write_set));
    FD_CLR(sd, &(pool->ready_read_set));
    FD_CLR(sd, &(pool->ready_write_set));
    pool->nr_conns--;//decrement the number of connections in the pool
    close(sd);//close the descriptor
    return 0;
}

int add_msg(int sd, char *buffer, int len, conn_pool_t *pool) {
    msg_t *msg;//pointer to new message
    conn_t *curr;//pointer to current connection
    for (curr = pool->conn_head; curr != NULL; curr = curr->next) { //iterate through the connections in the pool
        if (curr->fd != sd) { //check if connection's file descriptor does not match the given socket descriptor
            msg = (msg_t *) malloc(sizeof(msg_t) * 1); //allocate memory for new message
            if (msg == NULL) { //if memory allocation fails, return -1
                return -1;
            }
            msg->prev = NULL;
            msg->next = NULL;
            msg->size = len;//allocate memory for message
            msg->message = (char *) malloc(sizeof(char) * (len + 1));
            if (msg->message == NULL) { //if memory allocation fails, free memory for msg and return -1
                free(msg);
                return -1;
            }
            memset(msg->message, '\0', (len + 1));
            strcpy(msg->message, buffer);  //copy buffer to message
            if (curr->write_msg_head == NULL) { //add message to write queue
                curr->write_msg_head = msg;
                curr->write_msg_tail = msg;
            } else {
                msg->next = curr->write_msg_head;
                curr->write_msg_head = msg;
            }
            FD_SET(curr->fd, &(pool->write_set)); // add file descriptor to write_set
        }
    }
    return 0;
}

int write_to_client(int sd, conn_pool_t *pool) {
    int check = 1;// variable to check for errors in write()
       msg_t *msg; // pointer to current message
       conn_t *curr; // pointer to current connection
    for (curr = pool->conn_head; curr != NULL; curr = curr->next) {// iterate through connections in pool
        if (curr->fd == sd) { // check if connection's file descriptor matches given socket descriptor
            for (msg = curr->write_msg_tail; msg != NULL; msg = curr->write_msg_tail) {// iterate through messages in connection's write queue
                int bytes_written = write(sd, msg->message, msg->size);  // write message to client
                if (bytes_written == -1) { // check for errors
                    check = -1;
                    break;
                }
                curr->write_msg_tail = curr->write_msg_tail->prev;// remove message from write queue
                if (curr->write_msg_tail == NULL) {
                    curr->write_msg_head = NULL;
                } else {
                    curr->write_msg_tail->next =NULL;
                }
                free(msg->message);// free memory allocated for message
                free(msg);
            }
            if (check == 1) {
                FD_CLR(curr->fd, &(pool->ready_write_set)); // clear file descriptor from ready and write sets in pool
                FD_CLR(curr->fd, &(pool->write_set));
            }
            break;
        }
    }
    return 0;
}
