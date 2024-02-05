#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define BUFFER_SIZE 1024

// Prototypes de fonctions
void init_connection(int *sock, struct sockaddr_in *serverAddr, char *serverAddress, int port);
void send_pseudo(int sock, char *pseudo);
void handle_user_input(int sock);
void receive_message(int sock);
void close_connection(int sock);

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <Server Address> <Port> <Pseudo>\n", argv[0]);
        return 1;
    }

    int sock;
    struct sockaddr_in serverAddr;
    char *serverAddress = argv[1];
    int port = atoi(argv[2]);
    char *pseudo = argv[3];

    init_connection(&sock, &serverAddr, serverAddress, port);
    send_pseudo(sock, pseudo);

    printf("Connecté au serveur %s:%d en tant que %s\n", serverAddress, port, pseudo);

    fd_set read_fds;
    int fd_max = sock;

    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(sock, &read_fds);

        if (select(fd_max + 1, &read_fds, NULL, NULL, NULL) == -1) {
            perror("select");
            break;
        }

        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            handle_user_input(sock);
        }

        if (FD_ISSET(sock, &read_fds)) {
            receive_message(sock);
        }
    }

    close_connection(sock);
    return 0;
}

void init_connection(int *sock, struct sockaddr_in *serverAddr, char *serverAddress, int port) {
    *sock = socket(AF_INET, SOCK_STREAM, 0);
    if (*sock < 0) {
        perror("socket");
        exit(1);
    }

    memset(serverAddr, 0, sizeof(*serverAddr));
    serverAddr->sin_family = AF_INET;
    serverAddr->sin_port = htons(port);
    if (inet_pton(AF_INET, serverAddress, &serverAddr->sin_addr) <= 0) {
        perror("inet_pton");
        close(*sock);
        exit(1);
    }

    if (connect(*sock, (struct sockaddr *)serverAddr, sizeof(*serverAddr)) < 0) {
        perror("connect");
        close(*sock);
        exit(1);
    }
}

void send_pseudo(int sock, char *pseudo) {
    if (send(sock, pseudo, strlen(pseudo), 0) < 0) {
        perror("send pseudo");
        close(sock);
        exit(1);
    }
}

void handle_user_input(int sock) {
    char message[BUFFER_SIZE];
    if (fgets(message, BUFFER_SIZE, stdin) == NULL) return; // Sortie si fgets échoue

    size_t len = strlen(message);
    if (message[len - 1] == '\n') message[len - 1] = '\0';

    if (strcmp(message, "/exit") == 0) exit(0); // Sortie si l'utilisateur tape "/exit" 1er commande

    if (send(sock, message, strlen(message), 0) < 0) {
        perror("send");
        exit(1);
    }
}

void receive_message(int sock) {
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    ssize_t len = recv(sock, buffer, BUFFER_SIZE, 0);
    if (len <= 0) {
        printf("Déconnexion du serveur.\n");
        exit(0);
    }
    printf("==> %s\n", buffer);
}

void close_connection(int sock) {
    close(sock);
}
