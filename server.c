#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <ncurses.h>
#include <ctype.h>
#include <time.h>

#define MAX_CLIENTS 30
#define BUFFER_SIZE 1024
#define PSEUDO_SIZE 32
#define FULL_MESSAGE_SIZE (PSEUDO_SIZE + BUFFER_SIZE + 2) // +2 pour ": " et '\0'


#define RECEIVED_MSG_COLOR_PAIR 1
#define SENT_MSG_COLOR_PAIR 2

int client_socks[MAX_CLIENTS] = {0};
char client_pseudos[MAX_CLIENTS][PSEUDO_SIZE] = {0};
int total_clients = 0;

void init_server(int *server_sock, struct sockaddr_in *server_addr, int port);
void handle_new_connection(int server_sock, fd_set *master_fds, int *fd_max);
void handle_client_message(int client_sock, fd_set *master_fds);
void broadcast_message(int sender_sock, char *message, char *sender_pseudo);
void close_socket(int sock, fd_set *master_fds);
void log_message(const char *message);

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

    // Initialisation de ncurses
    initscr();
    cbreak();
    noecho();
    start_color();
    init_pair(RECEIVED_MSG_COLOR_PAIR, COLOR_CYAN, COLOR_BLACK);
    init_pair(SENT_MSG_COLOR_PAIR, COLOR_GREEN, COLOR_BLACK);

    init_server(&server_sock, &server_addr, port);
    FD_ZERO(&master_fds);
    FD_ZERO(&read_fds);
    FD_SET(server_sock, &master_fds);
    fd_max = server_sock;

    while (1) {
        read_fds = master_fds;
        if (select(fd_max + 1, &read_fds, NULL, NULL, NULL) == -1) {
            perror("select");
            break;
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

    close(server_sock);
    endwin(); // Fermeture de ncurses
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

    wattron(stdscr, COLOR_PAIR(SENT_MSG_COLOR_PAIR));
    wprintw(stdscr, "Nouveau client connecté : socket %d\n", client_sock);
    wattroff(stdscr, COLOR_PAIR(SENT_MSG_COLOR_PAIR));
    wrefresh(stdscr);
}

void handle_client_message(int client_sock, fd_set *master_fds) {
    char buffer[BUFFER_SIZE] = {0};
    ssize_t len = recv(client_sock, buffer, BUFFER_SIZE - 1, 0);

    if (len <= 0) {
        wattron(stdscr, COLOR_PAIR(SENT_MSG_COLOR_PAIR));
        wprintw(stdscr, "Client déconnecté : socket %d\n", client_sock);
        wattroff(stdscr, COLOR_PAIR(SENT_MSG_COLOR_PAIR));
        wrefresh(stdscr);
        close_socket(client_sock, master_fds);
    } else {
        // Vérifier si le client a déjà un pseudo
        if (client_pseudos[client_sock][0] == '\0') {
            strncpy(client_pseudos[client_sock], buffer, PSEUDO_SIZE - 1);
            client_pseudos[client_sock][PSEUDO_SIZE - 1] = '\0'; // Assurer la fin de chaîne
            wattron(stdscr, COLOR_PAIR(SENT_MSG_COLOR_PAIR));
            wprintw(stdscr, "Pseudo '%s' attribué au client %d\n", buffer, client_sock);
            wattroff(stdscr, COLOR_PAIR(SENT_MSG_COLOR_PAIR));
            wrefresh(stdscr);
        } else {
            // Construire le message complet avec pseudo et message
            char full_message[FULL_MESSAGE_SIZE];
            snprintf(full_message, sizeof(full_message), "%s: %s", client_pseudos[client_sock], buffer);

            // Afficher le message avec le pseudo du client
            wattron(stdscr, COLOR_PAIR(RECEIVED_MSG_COLOR_PAIR));
            wprintw(stdscr, "%s\n", full_message);
            wattroff(stdscr, COLOR_PAIR(RECEIVED_MSG_COLOR_PAIR));
            wrefresh(stdscr);

            // Enregistrer le message dans le fichier de log avec l'horodatage
            log_message(full_message);

            // Diffuser le message avec le pseudo
            broadcast_message(client_sock, buffer, client_pseudos[client_sock]);
        }
    }
}

void broadcast_message(int sender_sock, char *message, char *sender_pseudo) {
    for (int i = 0; i < total_clients; i++) {
        if (client_socks[i] != sender_sock) {
            char full_message[BUFFER_SIZE + PSEUDO_SIZE];
            snprintf(full_message, sizeof(full_message), "%s: %s", sender_pseudo, message); // Concaténer le pseudo et le message
            send(client_socks[i], full_message, strlen(full_message), 0);
        }
    }
}


void close_socket(int sock, fd_set *master_fds) {
    close(sock);
    FD_CLR(sock, master_fds);
    // Suppression du socket de la liste des clients ici, si nécessaire
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

