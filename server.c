#include <stdio.h>
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

char client_pseudos[MAX_CLIENTS][PSEUDO_SIZE];

void broadcast_message(int sender_sock, int *client_socks, char client_pseudos[MAX_CLIENTS][PSEUDO_SIZE], int total_clients, char *message, char *sender_pseudo) {
    for (int i = 0; i < total_clients; i++) {
        if (client_socks[i] != sender_sock) { // Ne pas renvoyer le message au client qui l'a envoyé
            char full_message[BUFFER_SIZE + PSEUDO_SIZE];
            snprintf(full_message, sizeof(full_message), "%s : %s", sender_pseudo, message); // Utiliser le pseudo de l'expéditeur
            send(client_socks[i], full_message, strlen(full_message), 0);
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <Port>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    int server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_size;

    fd_set read_fds, master_fds;
    int fd_max, client_socks[MAX_CLIENTS], total_clients = 0;

    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("socket");
        return 1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_sock);
        return 1;
    }

    if (listen(server_sock, MAX_CLIENTS) < 0) {
        perror("listen");
        close(server_sock);
        return 1;
    }

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
                    client_addr_size = sizeof(client_addr);
                    client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_addr_size);
                    if (client_sock < 0) {
                        perror("accept");
                        continue;
                    }

                    FD_SET(client_sock, &master_fds);
                    client_socks[total_clients++] = client_sock;
                    if (client_sock > fd_max) {
                        fd_max = client_sock;
                    }

                    printf("Nouveau client connecté : socket %d\n", client_sock);
                } else {
                    char buffer[BUFFER_SIZE];
                    memset(buffer, 0, BUFFER_SIZE);
                    ssize_t len = recv(i, buffer, BUFFER_SIZE, 0);

                    if (len <= 0) {
                        printf("Client déconnecté : socket %d\n", i);
                        close(i);
                        FD_CLR(i, &master_fds);
                    } else {
                        buffer[len] = '\0';

                        if (client_pseudos[i][0] == '\0') {
                            // Attribuer le pseudo au client pour la première fois
                            strncpy(client_pseudos[i], buffer, PSEUDO_SIZE - 1);
                            client_pseudos[i][PSEUDO_SIZE - 1] = '\0'; // Assurer la terminaison de la chaîne
                            printf("Pseudo '%s' attribué au client %d\n", buffer, i);
                        } else {
                            // Diffuser le message aux autres clients
                            printf("Message reçu de %s: %s\n", client_pseudos[i], buffer);
                            broadcast_message(i, client_socks, client_pseudos, total_clients, buffer, client_pseudos[i]);
                        }
                    }
                }
            }
        }
    }


    return 0;
}
