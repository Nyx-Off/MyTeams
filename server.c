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
#include <poll.h>
#include <sqlite3.h>


#define MAX_CLIENTS 30
#define BUFFER_SIZE 1024
#define PSEUDO_SIZE 32
#define FULL_MESSAGE_SIZE (PSEUDO_SIZE + BUFFER_SIZE + 2) // +2 pour ": " et '\0'

#define RECEIVED_MSG_COLOR_PAIR 1
#define SENT_MSG_COLOR_PAIR 2

int client_socks[MAX_CLIENTS] = {0};
char client_pseudos[MAX_CLIENTS][PSEUDO_SIZE] = {0};
int total_clients = 0;

char *motd = "Bienvenue sur le serveur!";
time_t server_start_time;
int max_clients = MAX_CLIENTS;


void init_server(int *server_sock, struct sockaddr_in *server_addr, int port);
void handle_new_connection(int server_sock, fd_set *master_fds, int *fd_max);
void handle_client_message(int client_sock, fd_set *master_fds);
void broadcast_message(int sender_sock, char *message, char *sender_pseudo);
void close_socket(int sock, fd_set *master_fds);
void log_message(const char *message);
int verify_credentials(char *identifiant, char *mdp, char *pseudo);
int find_client_index(int sock);
int check_pseudo_availability(char *pseudo);
void update_pseudo_in_db(int userId, char *newPseudo);
int get_user_id_by_pseudo(const char *pseudo);

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <Port>\n", argv[0]);
        return 1;
    }

    server_start_time = time(NULL);

    int port = atoi(argv[1]);
    int server_sock;
    struct sockaddr_in server_addr;

    // Initialisation de ncurses
    initscr();
    cbreak();
    noecho();
    start_color();
    init_pair(RECEIVED_MSG_COLOR_PAIR, COLOR_CYAN, COLOR_BLACK);
    init_pair(SENT_MSG_COLOR_PAIR, COLOR_GREEN, COLOR_BLACK);

    scrollok(stdscr, TRUE);

    wattron(stdscr, COLOR_PAIR(SENT_MSG_COLOR_PAIR));
    wprintw(stdscr, "Serveur démarré sur le port %d\n", port);
    wattroff(stdscr, COLOR_PAIR(SENT_MSG_COLOR_PAIR));
    wrefresh(stdscr);

    fd_set read_fds, master_fds;
    int fd_max;

    init_server(&server_sock, &server_addr, port);
    FD_ZERO(&master_fds);
    FD_SET(server_sock, &master_fds);
    fd_max = server_sock;

    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(server_sock, &read_fds);
        for (int i = 0; i < total_clients; i++) {
            FD_SET(client_socks[i], &read_fds);
        }
        fd_max = (fd_max > server_sock) ? fd_max : server_sock;

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

int verify_credentials(char *identifiant, char *mdp, char *pseudo) {
    sqlite3 *db;
    char *sql;
    int result = 0;
    sqlite3_stmt *stmt;

    if (sqlite3_open("MyTeams.db", &db) == SQLITE_OK) {
        sql = "SELECT pseudo FROM utilisateurs WHERE identifiant = ? AND mdp = ?";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, identifiant, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, mdp, -1, SQLITE_STATIC);

            if (sqlite3_step(stmt) == SQLITE_ROW) {
                const char *retrieved_pseudo = (const char *)sqlite3_column_text(stmt, 0);
                if (retrieved_pseudo) {
                    strncpy(pseudo, retrieved_pseudo, PSEUDO_SIZE - 1);
                    pseudo[PSEUDO_SIZE - 1] = '\0';
                    result = 1;
                }
            }
            sqlite3_finalize(stmt);
        }
        sqlite3_close(db);
    }
    return result;
}


void init_server(int *server_sock, struct sockaddr_in *server_addr, int port) {
    *server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (*server_sock < 0) {
        perror("socket");
        endwin();
        exit(1);
    }

    int optval = 1;
    if (setsockopt(*server_sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        perror("setsockopt");
        close(*server_sock);
        endwin();
        exit(1);
    }

    memset(server_addr, 0, sizeof(*server_addr));
    server_addr->sin_family = AF_INET;
    server_addr->sin_addr.s_addr = INADDR_ANY;
    server_addr->sin_port = htons(port);

    if (bind(*server_sock, (struct sockaddr *)server_addr, sizeof(*server_addr)) < 0) {
        perror("bind");
        close(*server_sock);
        endwin(); // Fermeture de ncurses
        exit(1);
    }

    if (listen(*server_sock, MAX_CLIENTS) < 0) {
        perror("listen");
        close(*server_sock);
        endwin();
        exit(1);
    }
}

void handle_new_connection(int server_sock, fd_set *master_fds, int *fd_max) {
    int client_sock;
    struct sockaddr_in client_addr;
    socklen_t client_addr_size = sizeof(client_addr);
    char buffer[1024]; // Assez grand pour identifiant + mdp

    client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_addr_size);
    if (client_sock < 0) {
        perror("accept");
        endwin();
        return;
    }

    // Lire les identifiants du client
    ssize_t len = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
    if (len <= 0) {
        close_socket(client_sock, master_fds);
        return;
    }
    buffer[len] = '\0';

    char identifiant[BUFFER_SIZE] = {0}, mdp[BUFFER_SIZE] = {0}, pseudo[PSEUDO_SIZE] = {0};

    // Extraire identifiant et mdp depuis le buffer
    sscanf(buffer, "%s %s", identifiant, mdp);

    if (!verify_credentials(identifiant, mdp, pseudo)) {
        wattron(stdscr, COLOR_PAIR(SENT_MSG_COLOR_PAIR));
        wprintw(stdscr, "Échec de la vérification des identifiants pour la socket %d\n", client_sock);
        wattroff(stdscr, COLOR_PAIR(SENT_MSG_COLOR_PAIR));
        close_socket(client_sock, master_fds);
        return;
    }

    // Ajout du nouveau client
    FD_SET(client_sock, master_fds);
    if (client_sock > *fd_max) {
        *fd_max = client_sock;
    }
    client_socks[total_clients] = client_sock;
    strncpy(client_pseudos[total_clients], pseudo, PSEUDO_SIZE);
    total_clients++;

    wattron(stdscr, COLOR_PAIR(SENT_MSG_COLOR_PAIR));
    wprintw(stdscr, "Client connecté : %s (socket %d)\n", pseudo, client_sock);
    wattroff(stdscr, COLOR_PAIR(SENT_MSG_COLOR_PAIR));
    wrefresh(stdscr);
}


int find_client_index_by_sock(int sock) {
    for (int i = 0; i < total_clients; i++) {
        if (client_socks[i] == sock) {
            return i;
        }
    }
    return -1;
}

void handle_client_message(int client_sock, fd_set *master_fds) {
   
    int port = 4242;
    char buffer[BUFFER_SIZE] = {0};

    char newPseudo[PSEUDO_SIZE] = {0};
    char confirmationPseudo[PSEUDO_SIZE] = {0};

    // Réception du message du client
    ssize_t len = recv(client_sock, buffer, BUFFER_SIZE - 1, 0);

    if (strcmp(buffer, "/who") == 0) {
        // Construire la réponse avec la liste des utilisateurs connectés
        char response[BUFFER_SIZE] = "Utilisateurs connectés : \n";
        for (int i = 0; i < total_clients; i++) {
            strcat(response, client_pseudos[i]);
            strcat(response, "\n");
        }
        // Envoyer la réponse uniquement au client qui a demandé
        send(client_sock, response, strlen(response), 0);
    }

    if (strcmp(buffer, "/info") == 0) {
        char response[BUFFER_SIZE];
        int uptime_seconds = difftime(time(NULL), server_start_time);
        int hours = uptime_seconds / 3600;
        int minutes = (uptime_seconds % 3600) / 60;
        int seconds = uptime_seconds % 60;

        snprintf(response, BUFFER_SIZE, "IP: %s\nPort: %d\nMOTD: %s\nUptime: %02d:%02d:%02d\nClients: %d/%d\n", 
                 "127.0.0.1", port, motd, hours, minutes, seconds, total_clients, max_clients);

        send(client_sock, response, strlen(response), 0);
    }

    if (strcmp(buffer, "/help") == 0 ) {
        char response[BUFFER_SIZE] = "Commandes disponibles :\n";
        strcat(response, "/who : Liste des utilisateurs connectés\n");
        strcat(response, "/info : Informations sur le serveur\n");
        strcat(response, "/help : Liste des commandes disponibles\n");
        send(client_sock, response, strlen(response), 0);
    }

    if (sscanf(buffer, "/nickname %s %s", newPseudo, confirmationPseudo) == 2) {
        if (strcmp(newPseudo, confirmationPseudo) == 0 && check_pseudo_availability(newPseudo)) {
            // Récupérer l'ID de l'utilisateur à partir de la base de données
            int userId = get_user_id_by_pseudo(client_pseudos[find_client_index_by_sock(client_sock)]);
            if (userId != -1) {
                // Mise à jour du pseudo dans la BDD
                update_pseudo_in_db(userId, newPseudo);
                // Mise à jour du pseudo dans la liste des clients connectés
                strncpy(client_pseudos[find_client_index_by_sock(client_sock)], newPseudo, PSEUDO_SIZE);
                send(client_sock, "Votre pseudo a été mis à jour avec succès.", 45, 0);
            } else {
                send(client_sock, "Erreur: Impossible de trouver votre ID utilisateur.", 55, 0);
            }
        } else {
            send(client_sock, "Erreur: Le pseudo est déjà pris ou les pseudos ne correspondent pas.", 68, 0);
        }
    }

    if (len <= 0) {
        // Si len est 0, le client s'est déconnecté proprement
        if (len == 0) {
            wattron(stdscr, COLOR_PAIR(SENT_MSG_COLOR_PAIR));
            wprintw(stdscr, "Client déconnecté proprement: socket %d\n", client_sock);
            wattroff(stdscr, COLOR_PAIR(SENT_MSG_COLOR_PAIR));
        } else {
            // Si len est inférieur à 0, une erreur de lecture s'est produite
            perror("recv error");
        }
        close_socket(client_sock, master_fds); // Fermer la socket du client
    } else {
        buffer[len] = '\0'; // Assurer la fin de chaîne du message

        int client_index = find_client_index_by_sock(client_sock);
        if (client_index == -1) {
            // Gérer l'erreur: client introuvable
            printf("Erreur: Client introuvable.\n");
            return;
        }

        // Construire et afficher le message complet avec le pseudo
        char full_message[FULL_MESSAGE_SIZE];
        snprintf(full_message, sizeof(full_message), "%s: %s", client_pseudos[client_index], buffer);

        wattron(stdscr, COLOR_PAIR(RECEIVED_MSG_COLOR_PAIR));
        wprintw(stdscr, "%s\n", full_message);
        wattroff(stdscr, COLOR_PAIR(RECEIVED_MSG_COLOR_PAIR));

        log_message(full_message);
        broadcast_message(client_sock, buffer, client_pseudos[client_index]);
    }
    wrefresh(stdscr);
}

int get_user_id_by_pseudo(const char *pseudo) {
    sqlite3 *db;
    sqlite3_stmt *stmt;
    const char *sql = "SELECT id FROM utilisateurs WHERE pseudo = ?";
    int userId = -1;

    if (sqlite3_open("MyTeams.db", &db) == SQLITE_OK) {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, pseudo, -1, SQLITE_STATIC);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                userId = sqlite3_column_int(stmt, 0);
            }
            sqlite3_finalize(stmt);
        }
        sqlite3_close(db);
    }

    return userId;
}

int check_pseudo_availability(char *pseudo) {
    sqlite3 *db;
    char *sql = "SELECT COUNT(*) FROM utilisateurs WHERE pseudo = ?";
    sqlite3_stmt *stmt;
    int count = 1; // Supposer pseudo non disponible

    if (sqlite3_open("MyTeams.db", &db) == SQLITE_OK) {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, pseudo, -1, SQLITE_STATIC);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                count = sqlite3_column_int(stmt, 0);
            }
            sqlite3_finalize(stmt);
        }
        sqlite3_close(db);
    }
    return count == 0; // Retourne 1 (true) si le pseudo est disponible
}

void update_pseudo_in_db(int userId, char *newPseudo) {
    sqlite3 *db;
    char *sql = "UPDATE utilisateurs SET pseudo = ? WHERE id = ?";
    sqlite3_stmt *stmt;

    if (sqlite3_open("MyTeams.db", &db) == SQLITE_OK) {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, newPseudo, -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 2, userId);

            if (sqlite3_step(stmt) != SQLITE_DONE) {
                fprintf(stderr, "Erreur lors de la mise à jour du pseudo.\n");
            } else {
                printf("Pseudo mis à jour avec succès.\n");
            }

            sqlite3_finalize(stmt);
        } else {
            fprintf(stderr, "Erreur lors de la préparation de la mise à jour du pseudo.\n");
        }
        sqlite3_close(db);
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
    close(sock); // Fermer la socket
    FD_CLR(sock, master_fds); // Retirer la socket de l'ensemble des descripteurs

    // Trouver l'index du client dans client_socks et le retirer
    for (int i = 0; i < total_clients; i++) {
        if (client_socks[i] == sock) {
            // Décaler tous les éléments après l'index trouvé vers la gauche
            for (int j = i; j < total_clients - 1; j++) {
                client_socks[j] = client_socks[j + 1];
                strncpy(client_pseudos[j], client_pseudos[j + 1], PSEUDO_SIZE);
            }
            client_socks[total_clients - 1] = 0; // Réinitialiser la dernière position
            memset(client_pseudos[total_clients - 1], 0, PSEUDO_SIZE); // Réinitialiser le dernier pseudo
            total_clients--; // Réduire le nombre total de clients
            break; // Sortir de la boucle après avoir trouvé et traité le client
        }
    }
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

