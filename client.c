#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <Server Address> <Port>\n", argv[0]);
        return 1;
    }

    char *serverAddress = argv[1];
    int port = atoi(argv[2]);

    // Création de la socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    // Construction de l'adresse du serveur
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    if (inet_pton(AF_INET, serverAddress, &serverAddr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sock);
        return 1;
    }

    // Connexion au serveur
    if (connect(sock, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("connect");
        close(sock);
        return 1;
    }

    printf("Connecté au serveur %s:%d\n", serverAddress, port);
    printf("Entrez vos messages (tapez 'exit' pour terminer) :\n");

    char message[1024];

    // Boucle pour envoyer des messages
    // Boucle pour envoyer des messages
    while (1) {
        printf("> ");
        if (fgets(message, 1024, stdin) == NULL) {
            break; // Sortie si fgets échoue
        }

        // Suppression du caractère de nouvelle ligne à la fin du message, s'il existe
        size_t len = strlen(message);
        if (len > 0 && message[len - 1] == '\n') {
            message[len - 1] = '\0';
        }

        // Vérifie si l'utilisateur veut quitter
        if (strncmp(message, "exit", 4) == 0) {
            break;
        }

        // Envoi du message
        if (send(sock, message, strlen(message), 0) < 0) {
            perror("send");
            break;
        }
    }


    printf("Déconnexion...\n");

    // Fermeture de la socket
    close(sock);

    return 0;
}
