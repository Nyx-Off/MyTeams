#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/wait.h>

#define MAX_CLIENTS 30
#define BUFFER_SIZE 1024

void handle_client(int client_sock, struct sockaddr_in client_addr) {
    char buffer[BUFFER_SIZE];
    char client_ip[INET_ADDRSTRLEN];

    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    int client_port = ntohs(client_addr.sin_port);

    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        ssize_t len = recv(client_sock, buffer, BUFFER_SIZE, 0);

        if (len <= 0) {
            // Si recv retourne 0, le client a fermé la connexion
            // Si recv retourne -1, une erreur s'est produite
            break;
        }

        printf("Received from %s:%d => %s\n", client_ip, client_port, buffer);
    }

    close(client_sock);
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
    int fd_max;

    // Création de la socket
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("socket");
        return 1;
    }

    // Préparation de l'adresse du serveur
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    // Association de la socket à l'adresse du serveur
    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_sock);
        return 1;
    }

    // Passage de la socket en mode écoute
    if (listen(server_sock, MAX_CLIENTS) < 0) {
        perror("listen");
        close(server_sock);
        return 1;
    }

    // Initialisation des ensembles de descripteurs de fichier
    FD_ZERO(&master_fds);
    FD_ZERO(&read_fds);
    FD_SET(server_sock, &master_fds);
    fd_max = server_sock;

    printf("Starting on port %d.\n", port);
    printf("Binding OK\n");
    printf("Waiting for messages from clients...\n");

    // Boucle principale
    while (1) {
        read_fds = master_fds;
        if (select(fd_max + 1, &read_fds, NULL, NULL, NULL) == -1) {
            perror("select");
            exit(4);
        }

        // Parcourir les descripteurs de fichiers existants
        for (int i = 0; i <= fd_max; i++) {
            if (FD_ISSET(i, &read_fds)) {
                if (i == server_sock) {
                    // Nouvelle connexion
                    client_addr_size = sizeof(client_addr);
                    client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_addr_size);
                    if (client_sock < 0) {
                        perror("accept");
                    } else {
                        // Ajouter le nouveau socket au set
                        FD_SET(client_sock, &master_fds);
                        if (client_sock > fd_max) {
                            fd_max = client_sock;
                        }
                    }
                } else {
                    // Données d'un client
                    pid_t pid = fork();
                    if (pid < 0) {
                        perror("fork");
                    } else if (pid == 0) {
                        // Dans le processus enfant
                        close(server_sock); // L'enfant n'a pas besoin du listener
                        handle_client(i, client_addr);
                    } else {
                        // Dans le processus parent
                        close(i); // Le parent n'a plus besoin de ce socket
                        FD_CLR(i, &master_fds); // Retirer de l'ensemble principal
                        waitpid(-1, NULL, WNOHANG); // Nettoyer les processus enfants terminés
                    }
                }
            }
        }
    }

    return 0;
}
