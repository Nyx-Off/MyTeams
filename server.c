#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define MAX_CLIENTS 30
#define BUFFER_SIZE 1024
#define PSEUDO_SIZE 32

// Variables globales pour simplifier
int client_socks[MAX_CLIENTS] = {0};
char client_pseudos[MAX_CLIENTS][PSEUDO_SIZE] = {0};
int total_clients = 0;

// Prototypes de fonctions
void init_server(int *server_sock, struct sockaddr_in *server_addr, int port);
void handle_new_connection(int server_sock, fd_set *master_fds, int *fd_max);
void handle_client_message(int client_sock, fd_set *master_fds);
void broadcast_message(int sender_sock, char *message, char *sender_pseudo);
void close_socket(int sock, fd_set *master_fds);

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <Port>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    int server_sock;
    struct sockaddr_in server_addr;

    fd_set read_fds, master_fds;
    int fd_max;

    init_server(&server_sock, &server_addr, port);
    FD_ZERO(&master_fds);
    FD_ZERO(&read_fds);
    FD_SET(server_sock, &master_fds);
    fd_max = server_sock;

    printf("Le serveur démarre sur le port %d.\n", port);
    printf("En attente de messages des clients...\n");

    while (1) {
        read_fds = master_fds;
        if (select(fd_max + 1, &read_fds, NULL, NULL, NULL) == -1) {
            perror("select");
            exit(4);
        }

        for (int i = 0; i <= fd_max; i++) {
            if (FD_ISSET(i, &read_fds)) {
                if (i == server_sock) {
                    handle_new_connection(server_sock, &master_fds, &fd_max);
                } else {
                    handle_client_message(i, &master_fds);
                }
            }
        }
    }

    // Fermeture du socket serveur
    close(server_sock);
    return 0;
}

void init_server(int *server_sock, struct sockaddr_in *server_addr, int port) {
    *server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (*server_sock < 0) {
        perror("socket");
        exit(1);
    }

    memset(server_addr, 0, sizeof(*server_addr));
    server_addr->sin_family = AF_INET;
    server_addr->sin_addr.s_addr = INADDR_ANY;
    server_addr->sin_port = htons(port);

    if (bind(*server_sock, (struct sockaddr *)server_addr, sizeof(*server_addr)) < 0) {
        perror("bind");
        close(*server_sock);
        exit(1);
    }

    if (listen(*server_sock, MAX_CLIENTS) < 0) {
        perror("listen");
        close(*server_sock);
        exit(1);
    }
}

void handle_new_connection(int server_sock, fd_set *master_fds, int *fd_max) {
    int client_sock;
    struct sockaddr_in client_addr;
    socklen_t client_addr_size = sizeof(client_addr);

    client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_addr_size);
    if (client_sock < 0) {
        perror("accept");
        return;
    }

    FD_SET(client_sock, master_fds);
    client_socks[total_clients++] = client_sock;
    if (client_sock > *fd_max) {
        *fd_max = client_sock;
    }

    printf("Nouveau client connecté : socket %d\n", client_sock);
}

void log_message(const char *message) {
    FILE *file = fopen("messages.log", "a");
    if (file == NULL) {
        perror("Erreur lors de l'ouverture du fichier de log");
        return;
    }
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char formatted_time[20]; 
    strftime(formatted_time, sizeof(formatted_time), "%Y-%m-%d %H:%M:%S", t);
    fprintf(file, "[%s] %s\n", formatted_time, message);
    fclose(file);
}

void handle_client_message(int client_sock, fd_set *master_fds) {
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    ssize_t len = recv(client_sock, buffer, BUFFER_SIZE, 0);

    if (len <= 0) {
        printf("Client déconnecté : socket %d\n", client_sock);
        close_socket(client_sock, master_fds);
    } else {
        buffer[len] = '\0';
        char *sender_pseudo = client_pseudos[client_sock];

        if (sender_pseudo[0] == '\0') {
            strncpy(sender_pseudo, buffer, PSEUDO_SIZE - 1);
            sender_pseudo[PSEUDO_SIZE - 1] = '\0';
            printf("Pseudo '%s' attribué au client %d\n", buffer, client_sock);
        } else {
            char full_message[BUFFER_SIZE + PSEUDO_SIZE];
            snprintf(full_message, sizeof(full_message), "%s : %s", sender_pseudo, buffer);
            log_message(full_message); // Log le message complet
            broadcast_message(client_sock, buffer, sender_pseudo);
        }
    }
}

void broadcast_message(int sender_sock, char *message, char *sender_pseudo) {
    for (int i = 0; i < total_clients; i++) {
        if (client_socks[i] != sender_sock) {
            char full_message[BUFFER_SIZE + PSEUDO_SIZE];
            snprintf(full_message, sizeof(full_message), "%s : %s", sender_pseudo, message);
            send(client_socks[i], full_message, strlen(full_message), 0);
        }
    }
}

void close_socket(int sock, fd_set *master_fds) {
    close(sock);
    FD_CLR(sock, master_fds);
    // Ici, on pourrait également gérer la suppression du socket de la liste des clients
}
