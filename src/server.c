#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "lexer.h"
#include "parser.h"
#include "executor.h"
#include "storage.h"

#include "distributed.h"

// Link with Ws2_32.lib
#pragma comment(lib, "Ws2_32.lib")

#define PORT_DEFAULT 8888
int g_port = PORT_DEFAULT;

#define BUFFER_SIZE 4096

KVStore *global_store;

void *client_handler(void *socket_desc) {
    int sock = *(int*)socket_desc;
    free(socket_desc);
    
    char buffer[BUFFER_SIZE];
    int read_size;
    
    while(1) {
        memset(buffer, 0, BUFFER_SIZE);
        read_size = recv(sock, buffer, BUFFER_SIZE, 0);
        
        if (read_size > 0) {
            printf("[%s] Received: %s\n", dist_is_master() ? "Master" : "Worker", buffer);
            
            // Remove Enter
            buffer[strcspn(buffer, "\r\n")] = 0;
            if (strcmp(buffer, "exit") == 0) break;

            TokenList *tokens = tokenize(buffer);
            if (tokens->count > 1) {
                ASTNode *ast = parse(tokens);
                if (ast) {
                    if (dist_is_master()) {
                        // Distributed Routing
                        char *resp = dist_route_query(ast, buffer);
                        if (resp) {
                            send(sock, resp, strlen(resp), 0);
                            free(resp);
                        } else {
                            send(sock, "Error: Routing failed\n", 22, 0);
                        }
                    } else {
                        // Local Execution (Worker or Standalone)
                        FILE *fp = tmpfile(); 
                        if (!fp) fp = fopen("temp_output.txt", "w+");
                        
                        execute_query(ast, global_store, fp);
                        
                        fseek(fp, 0, SEEK_SET);
                        char out_buf[BUFFER_SIZE];
                        memset(out_buf, 0, BUFFER_SIZE);
                        int sent_something = 0;
                        while(fgets(out_buf, BUFFER_SIZE, fp)) {
                            send(sock, out_buf, strlen(out_buf), 0);
                            sent_something = 1;
                        }
                        if (!sent_something) send(sock, "OK\n", 3, 0);
                        fclose(fp);
                    }
                } else {
                    char *msg = "Error: Failed to parse.\n";
                    send(sock, msg, strlen(msg), 0);
                }
            }
            free_token_list(tokens);
            
            // Only send prompt if interactive or master? 
            // Workers shouldn't send prompt if driven by Master? 
            // Assume Master handles clients, Workers handle Master.
            // Master sends prompt to client.
            if (dist_is_master()) {
                char *prompt = "\ninventix> ";
                send(sock, prompt, strlen(prompt), 0);
            }
        } else {
            break;
        }
    }
    
    closesocket(sock);
    // printf("Client disconnected.\n");
    return NULL;
}

int main(int argc, char *argv[]) {
    WSADATA wsa;
    SOCKET server_fd, new_socket;
    struct sockaddr_in server, client;
    int c;

    // Arg parsing
    dist_init();
    if (argc > 1) {
        for(int i=1; i<argc; i++) {
            if (strcmp(argv[i], "--master") == 0) {
                dist_set_master(1);
            } else if (strcmp(argv[i], "--worker") == 0) {
                dist_set_master(0);
            } else if (strcmp(argv[i], "--port") == 0 && i+1 < argc) {
                g_port = atoi(argv[i+1]);
            }
        }
    }

    printf("Initializing Winsock...\n");
    if (WSAStartup(MAKEWORD(2,2),&wsa) != 0) {
        printf("Failed. Error Code : %d", WSAGetLastError());
        return 1;
    }
    
    global_store = kv_create();

    if((server_fd = socket(AF_INET , SOCK_STREAM , 0 )) == INVALID_SOCKET) {
        printf("Could not create socket : %d", WSAGetLastError());
    }

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(g_port);

    if(bind(server_fd,(struct sockaddr *)&server , sizeof(server)) == SOCKET_ERROR) {
        printf("Bind failed with error code : %d", WSAGetLastError());
        return 1;
    }
    
    listen(server_fd , 3);

    printf("InventixDB Server (%s) started on port %d\n", dist_is_master() ? "MASTER" : "WORKER", g_port);
    
    c = sizeof(struct sockaddr_in);
    
    while( (new_socket = accept(server_fd, (struct sockaddr *)&client, &c)) != INVALID_SOCKET ) {
        printf("Connection accepted\n");

        pthread_t sniffer_thread;
        int *new_sock = malloc(sizeof(int));
        *new_sock = new_socket;
        
        if(pthread_create(&sniffer_thread, NULL,  client_handler, (void*) new_sock) < 0) {
            perror("could not create thread");
            return 1;
        }
        
        // Detach
        pthread_detach(sniffer_thread);
    }

    if (new_socket == INVALID_SOCKET) {
        printf("accept failed with error code : %d", WSAGetLastError());
        return 1;
    }

    closesocket(server_fd);
    WSACleanup();
    
    return 0;
}
