#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <Server Address> <Port> <Pseudo>\n", argv[0]);
        return 1;
    }

    char *serverAddress = argv[1];
    int port = atoi(argv[2]);
    char *pseudo = argv[3];

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    if (inet_pton(AF_INET, serverAddress, &serverAddr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sock);
        return 1;
    }

    if (connect(sock, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("connect");
        close(sock);
        return 1;
    }

    if (send(sock, pseudo, strlen(pseudo), 0) < 0) {
        perror("send pseudo");
        close(sock);
        return 1;
    }

    printf("Connecté au serveur %s:%d en tant que %s\n", serverAddress, port, pseudo);

    fd_set read_fds;
    int fd_max = sock > STDIN_FILENO ? sock : STDIN_FILENO;

    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(sock, &read_fds);

        if (select(fd_max + 1, &read_fds, NULL, NULL, NULL) == -1) {
            perror("select");
            break;
        }

        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            char message[1024];
            if (fgets(message, 1024, stdin) == NULL) break; // Sortie si fgets échoue

            size_t len = strlen(message);
            if (message[len - 1] == '\n') message[len - 1] = '\0';

            if (strcmp(message, "exit") == 0) break;

            if (send(sock, message, strlen(message), 0) < 0) {
                perror("send");
                break;
            }
        }

        if (FD_ISSET(sock, &read_fds)) {
            char buffer[1024];
            memset(buffer, 0, sizeof(buffer));
            ssize_t len = recv(sock, buffer, sizeof(buffer), 0);
            if (len <= 0) {
                printf("Déconnexion du serveur.\n");
                break;
            }
            printf("==> %s\n", buffer); // Affiche déjà le pseudo et le message
        }
    }

    close(sock);
    return 0;
}
