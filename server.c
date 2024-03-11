#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <ncurses.h>
#include <ctype.h>
#include <time.h>
#include <sqlite3.h>
#include <openssl/sha.h>

#define MAX_CLIENTS 30
#define BUFFER_SIZE 1024
#define PSEUDO_SIZE 32
#define FULL_MESSAGE_SIZE (PSEUDO_SIZE + BUFFER_SIZE + 2)

#define RECEIVED_MSG_COLOR_PAIR 1
#define SENT_MSG_COLOR_PAIR 2
#define ADMIN_MSG_COLOR_PAIR 3
#define STATUS_MSG_COLOR_PAIR 4

int client_socks[MAX_CLIENTS] = {0};
char client_pseudos[MAX_CLIENTS][PSEUDO_SIZE] = {0};
int total_clients = 0;

char *motd = "Bienvenue sur le serveur!";
time_t server_start_time;
int max_clients = MAX_CLIENTS;
int client_dnd_mode[MAX_CLIENTS] = {0}; // 0: désactivé, 1: activé


void init_server(int *server_sock, struct sockaddr_in *server_addr, int port);
void handle_new_connection(int server_sock, fd_set *master_fds, int *fd_max);
void handle_client_message(int client_sock, fd_set *master_fds);
void broadcast_message(int sender_sock, char *message, char *sender_pseudo, bool is_status);
void close_socket(int sock, fd_set *master_fds);
void log_message(const char *message);
int verify_credentials(char *identifiant, char *mdp, char *pseudo);
int find_client_index(int sock);
int check_pseudo_availability(char *pseudo);
void update_pseudo_in_db(int userId, char *newPseudo);
int get_user_id_by_pseudo(const char *pseudo);
int create_user(const char *permission, const char *identifiant, const char *pseudo, const char *mdp);
int is_user_admin(const char *pseudo);
void hash_password(const char* password, char hashedOutput[65]);
void sha256_to_string(unsigned char hash[SHA256_DIGEST_LENGTH], char outputBuffer[65]);
void send_user_details(int client_sock, const char *pseudo);
void update_last_connection(const char *pseudo);
void update_disconnected(const char *pseudo) ;
void kick_client(char *targetPseudo, int sender_sock, fd_set *master_fds);

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <Port>\n", argv[0]);
        return 1;
    }
    server_start_time = time(NULL);
    int port = atoi(argv[1]);
    int server_sock;
    struct sockaddr_in server_addr;
    initscr();
    cbreak();
    noecho();
    start_color();
    init_pair(RECEIVED_MSG_COLOR_PAIR, COLOR_CYAN, COLOR_BLACK);
    init_pair(SENT_MSG_COLOR_PAIR, COLOR_GREEN, COLOR_BLACK);
    init_pair(ADMIN_MSG_COLOR_PAIR, COLOR_RED, COLOR_BLACK); 
    init_pair(STATUS_MSG_COLOR_PAIR, COLOR_MAGENTA, COLOR_BLACK);
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
    endwin();
    return 0;
}

void sha256_to_string(unsigned char hash[SHA256_DIGEST_LENGTH], char outputBuffer[65]) {
    int i;
    for(i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(outputBuffer + (i * 2), "%02x", hash[i]);
    }
    outputBuffer[64] = 0;
}

void hash_password(const char* password, char hashedOutput[65]) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, password, strlen(password));
    SHA256_Final(hash, &sha256);
    sha256_to_string(hash, hashedOutput);
}

void send_user_details(int client_sock, const char *pseudo) {
    sqlite3 *db;
    sqlite3_stmt *stmt;
    const char *sql = "SELECT identifiant, permission, derniere_connections FROM utilisateurs WHERE pseudo = ?";
    if (sqlite3_open("MyTeams.db", &db) == SQLITE_OK) {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, pseudo, -1, SQLITE_STATIC);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                const char *identifiant = (const char *)sqlite3_column_text(stmt, 0);
                const char *permission = (const char *)sqlite3_column_text(stmt, 1);
                const char *derniere_connexion = (const char *)sqlite3_column_text(stmt, 2);
                char response[BUFFER_SIZE];
                snprintf(response, BUFFER_SIZE, "Pseudo: %s\nIdentifiant: %s\nPermission: %s\nDernière connexion: %s\n",
                         pseudo, identifiant, permission, derniere_connexion);
                send(client_sock, response, strlen(response), 0);
            } else {
                char response[BUFFER_SIZE] = "Erreur lors de la récupération des détails de l'utilisateur.\n";
                send(client_sock, response, strlen(response), 0);
            }
            sqlite3_finalize(stmt);
        } else {
            char response[BUFFER_SIZE] = "Erreur lors de la préparation de la requête.\n";
            send(client_sock, response, strlen(response), 0);
        }
        sqlite3_close(db);
    } else {
        char response[BUFFER_SIZE] = "Erreur de connexion à la base de données.\n";
        send(client_sock, response, strlen(response), 0);
    }
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
        endwin(); 
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
    char buffer[1024]; 
    client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_addr_size);
    if (client_sock < 0) {
        perror("accept");
        endwin();
        return;
    }
    ssize_t len = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
    if (len <= 0) {
        close_socket(client_sock, master_fds);
        return;
    }
    buffer[len] = '\0';
    char identifiant[BUFFER_SIZE] = {0}, mdp[BUFFER_SIZE] = {0}, pseudo[PSEUDO_SIZE] = {0};
    sscanf(buffer, "%s %s", identifiant, mdp);
    if (!verify_credentials(identifiant, mdp, pseudo)) {
        wattron(stdscr, COLOR_PAIR(SENT_MSG_COLOR_PAIR));
        wprintw(stdscr, "Échec de la vérification des identifiants pour la socket %d\n", client_sock);
        wattroff(stdscr, COLOR_PAIR(SENT_MSG_COLOR_PAIR));
        close_socket(client_sock, master_fds);
        return;
    }
    update_last_connection(pseudo);
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
    ssize_t len = recv(client_sock, buffer, BUFFER_SIZE - 1, 0);
    if (len <= 0) {
        if (len == 0) {
            wattron(stdscr, COLOR_PAIR(SENT_MSG_COLOR_PAIR));
            wprintw(stdscr, "Client déconnecté proprement: socket %d\n", client_sock);
            wattroff(stdscr, COLOR_PAIR(SENT_MSG_COLOR_PAIR));
        } else {
            perror("recv error");
        }
        close_socket(client_sock, master_fds); 
    } else {
        buffer[len] = '\0'; 
        int client_index = find_client_index_by_sock(client_sock);
        if (client_index == -1) {
            printf("Erreur: Client introuvable.\n");
            return;
        }

        if (buffer[0] == '/') {
            wattron(stdscr, COLOR_PAIR(SENT_MSG_COLOR_PAIR));
            if (is_user_admin(client_pseudos[client_index])) {
                wprintw(stdscr, "[ADMIN] Commande reçue de %s: %s\n", client_pseudos[client_index], buffer);
            } else {
                wprintw(stdscr, "Commande reçue de %s: %s\n", client_pseudos[client_index], buffer);
            }
            wattroff(stdscr, COLOR_PAIR(SENT_MSG_COLOR_PAIR));
            wrefresh(stdscr);
        }

        if (strncmp(buffer, "/info", 5) == 0) {
            char response[BUFFER_SIZE];
            int uptime_seconds = difftime(time(NULL), server_start_time);
            int hours = uptime_seconds / 3600;
            int minutes = (uptime_seconds % 3600) / 60;
            int seconds = uptime_seconds % 60;
            snprintf(response, BUFFER_SIZE, "IP: %s\nPort: %d\nMOTD: %s\nUptime: %02d:%02d:%02d\nClients: %d/%d\n", 
                    "127.0.0.1", port, motd, hours, minutes, seconds, total_clients, max_clients);
            send(client_sock, response, strlen(response), 0); 
            return;
        }

        if (strncmp(buffer, "/pause", 6) == 0) {
            int client_index = find_client_index_by_sock(client_sock);
            if (client_index != -1) {
                client_dnd_mode[client_index] = !client_dnd_mode[client_index]; // Bascule l'état du mode
                if (client_dnd_mode[client_index]) {
                    send(client_sock, "Mode Ne Pas Déranger activé.\n", 30, 0);
                } else {
                    send(client_sock, "Mode Ne Pas Déranger désactivé.\n", 31, 0);
                }
            } else {
                send(client_sock, "Erreur: Client introuvable.\n", 28, 0);
            }
            return; 
        }
       if (strncmp(buffer, "/status ", 7) == 0) {
            char status_message[BUFFER_SIZE];
            snprintf(status_message, sizeof(status_message), "%s", buffer + 7);
            broadcast_message(client_sock, status_message, client_pseudos[client_index], true);
            return;
        }
        if (strncmp(buffer, "/help" , 5) == 0 ) {
            if (is_user_admin(client_pseudos[client_index])) {
                char response[BUFFER_SIZE] = "Commandes disponibles :\n";
                strcat(response, "/who : Liste des utilisateurs connectés\n");
                strcat(response, "/info : Informations sur le serveur\n");
                strcat(response, "/help : Liste des commandes disponibles\n");
                strcat(response, "/exit : Déconnexion du serveur\n");
                strcat(response, "/nickname <nouveau_pseudo> <confirmation> : Changer de pseudo\n");
                strcat(response, "/createuser <permission> <identifiant> <confirmation_identifiant> <pseudo> <confirmation_pseudo> <mdp> <confirmation_mdp> : Créer un nouvel utilisateur\n");
                send(client_sock, response, strlen(response), 0);
                return;
            }
            else {
                char response[BUFFER_SIZE] = "Commandes disponibles :\n";
                strcat(response, "/who : Liste des utilisateurs connectés\n");
                strcat(response, "/info : Informations sur le serveur\n");
                strcat(response, "/help : Liste des commandes disponibles\n");
                strcat(response, "/exit : Déconnexion du serveur\n");
                strcat(response, "/nickname <nouveau_pseudo> <confirmation> : Changer de pseudo\n");
                send(client_sock, response, strlen(response), 0);
                return;
            }
        }
        if (strncmp(buffer, "/who", 4) == 0) {
            int isAdmin = is_user_admin(client_pseudos[find_client_index_by_sock(client_sock)]);
            char response[BUFFER_SIZE] = "Utilisateurs connectés : \n";

            if (isAdmin) {
                wprintw(stdscr, "Mode Admin activé pour /who\n");
                
                for (int i = 0; i < total_clients; i++) {
                    strcat(response, client_pseudos[i]);
                    strcat(response, " (Détails):\n");
                    send_user_details(client_sock, client_pseudos[i]);
                    usleep(10000);
                }
            } else {
                wprintw(stdscr, "Mode Utilisateur normal pour /who\n");
                for (int i = 0; i < total_clients; i++) {
                    strcat(response, client_pseudos[i]);
                    strcat(response, "\n");
                }
            }
            send(client_sock, response, strlen(response), 0);
            return;
        }
        if (sscanf(buffer, "/nickname %s %s", newPseudo, confirmationPseudo) == 2) {
            if (strcmp(newPseudo, confirmationPseudo) == 0 && check_pseudo_availability(newPseudo)) {
                int userId = get_user_id_by_pseudo(client_pseudos[find_client_index_by_sock(client_sock)]);
                if (userId != -1) {
                    update_pseudo_in_db(userId, newPseudo);
                    strncpy(client_pseudos[find_client_index_by_sock(client_sock)], newPseudo, PSEUDO_SIZE);
                    send(client_sock, "Votre pseudo a été mis à jour avec succès.", 45, 0);
                    return;
                } else {
                    send(client_sock, "Erreur: Impossible de trouver votre ID utilisateur.", 55, 0);
                    return;
                }
            } else {
                send(client_sock, "Erreur: Le pseudo est déjà pris ou les pseudos ne correspondent pas.", 68, 0);
                return;
            }
        }     
        
        if (strncmp(buffer, "/kick ", 6) == 0) {
            char targetPseudo[PSEUDO_SIZE] = {0};
            sscanf(buffer + 6, "%s", targetPseudo);

            if (is_user_admin(client_pseudos[find_client_index_by_sock(client_sock)])) {
                kick_client(targetPseudo, client_sock, master_fds);
            } else {
                char errorMsg[BUFFER_SIZE] = "Vous n'avez pas la permission d'utiliser cette commande.\n";
                send(client_sock, errorMsg, strlen(errorMsg), 0);
            }
            return;
        }

        if (strncmp(buffer, "/createuser", 11) == 0) {
            if (is_user_admin(client_pseudos[client_index])) {
                char permission[BUFFER_SIZE], identifiant[BUFFER_SIZE], confIdentifiant[BUFFER_SIZE],
                    pseudo[BUFFER_SIZE], confPseudo[BUFFER_SIZE], mdp[BUFFER_SIZE], confMdp[BUFFER_SIZE];
                if (sscanf(buffer, "/createuser %s %s %s %s %s %s %s", permission, identifiant, confIdentifiant,pseudo, confPseudo, mdp, confMdp) == 7) {
                    if (strcmp(identifiant, confIdentifiant) == 0 && strcmp(pseudo, confPseudo) == 0 && strcmp(mdp, confMdp) == 0) {
                        int creationStatus = create_user(permission, identifiant, pseudo, mdp);
                        if (creationStatus == 0) {
                            char successMsg[BUFFER_SIZE] = "Utilisateur créé avec succès.\n";
                            send(client_sock, successMsg, strlen(successMsg), 0);
                            return;
                        } 
                        else {
                            char errorMsg[BUFFER_SIZE] = "Erreur lors de la création de l'utilisateur.\n";
                            send(client_sock, errorMsg, strlen(errorMsg), 0);
                            return;
                        }
                    } 
                    else {
                        char validationMsg[BUFFER_SIZE] = "Erreur de validation des données.\n";
                        send(client_sock, validationMsg, strlen(validationMsg), 0);
                        return;
                    }
                } 
                else {
                    char errorMsg[BUFFER_SIZE] = "Commande /createuser format incorrect.\n";
                    send(client_sock, errorMsg, strlen(errorMsg), 0);
                    return;
                }
            }
            else {
                char errorMsg[BUFFER_SIZE] = "Vous n'êtes pas autorisé à exécuter cette commande.\n";
                send(client_sock, errorMsg, strlen(errorMsg), 0);
                return;
            }
        }  
        int isAdmin = is_user_admin(client_pseudos[client_index]);
        char full_message[FULL_MESSAGE_SIZE];
        snprintf(full_message, sizeof(full_message), "%s: %s", client_pseudos[client_index], buffer);
        if (isAdmin) {
            wattron(stdscr, COLOR_PAIR(ADMIN_MSG_COLOR_PAIR)); 
            wprintw(stdscr, "%s\n", full_message);
            wattroff(stdscr, COLOR_PAIR(ADMIN_MSG_COLOR_PAIR));
        } else {
            wattron(stdscr, COLOR_PAIR(RECEIVED_MSG_COLOR_PAIR)); 
            wprintw(stdscr, "%s\n", full_message);
            wattroff(stdscr, COLOR_PAIR(RECEIVED_MSG_COLOR_PAIR));
        }
        broadcast_message(client_sock, buffer, client_pseudos[client_index], false);
        wrefresh(stdscr); 
    }
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

int is_user_admin(const char *pseudo) {
    sqlite3 *db;
    sqlite3_stmt *stmt;
    const char *sql = "SELECT permission FROM utilisateurs WHERE pseudo = ?";
    int isAdmin = 0;
    if (sqlite3_open("MyTeams.db", &db) == SQLITE_OK) {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, pseudo, -1, SQLITE_STATIC);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                const char *permission = (const char *)sqlite3_column_text(stmt, 0);
                isAdmin = strcmp(permission, "admin") == 0; 
            }
            sqlite3_finalize(stmt);
        }
        sqlite3_close(db);
    }
    return isAdmin;
}

int create_user(const char *permission, const char *identifiant, const char *pseudo, const char *mdp) {
    sqlite3 *db;
    int rc = sqlite3_open("MyTeams.db", &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return -1;
    }
    char *sql = "INSERT INTO utilisateurs (permission, identifiant, pseudo, mdp) VALUES (?, ?, ?, ?);";
    sqlite3_stmt *stmt;
    char hashedPassword[65];
    hash_password(mdp, hashedPassword);
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, permission, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, identifiant, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, pseudo, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, hashedPassword, -1, SQLITE_STATIC);
    } else {
        fprintf(stderr, "Failed to execute statement: %s\n", sqlite3_errmsg(db));
    }
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        printf("ERROR inserting data: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return -2;
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
}

int check_pseudo_availability(char *pseudo) {
    sqlite3 *db;
    char *sql = "SELECT COUNT(*) FROM utilisateurs WHERE pseudo = ?";
    sqlite3_stmt *stmt;
    int count = 1;
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
    return count == 0; 
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
                printw("Erreur lors de la mise à jour du pseudo.\n");
            } else {
                printw("Pseudo mis à jour avec succès.\n");
            }
            sqlite3_finalize(stmt);
        } else {
            printw("Erreur lors de la préparation de la mise à jour du pseudo.\n");
        }
        sqlite3_close(db);
    }
}

void broadcast_message(int sender_sock, char *message, char *sender_pseudo, bool is_status) {
    int isAdmin = is_user_admin(sender_pseudo);
    char full_message[FULL_MESSAGE_SIZE];

    if (is_status) {
        snprintf(full_message, sizeof(full_message), "STATUS de %s: %s", sender_pseudo, message);
    } else if (isAdmin) {
        snprintf(full_message, sizeof(full_message), "%s: %s", sender_pseudo, message);
    } else {
        snprintf(full_message, sizeof(full_message), "%s: %s", sender_pseudo, message);
    }

    for (int i = 0; i < total_clients; i++) {
        if (client_socks[i] != sender_sock && !client_dnd_mode[i]) {
            send(client_socks[i], full_message, strlen(full_message), 0);
        }
    }
    log_message(full_message);
}


void close_socket(int sock, fd_set *master_fds) {
    close(sock);
    FD_CLR(sock, master_fds); 
    int client_index = find_client_index(sock);
    if (client_index != -1) {
        update_disconnected(client_pseudos[client_index]);
    }
    for (int i = 0; i < total_clients; i++) {
        if (client_socks[i] == sock) {
            for (int j = i; j < total_clients - 1; j++) {
                client_socks[j] = client_socks[j + 1];
                strncpy(client_pseudos[j], client_pseudos[j + 1], PSEUDO_SIZE);
            }
            client_socks[total_clients - 1] = 0; 
            memset(client_pseudos[total_clients - 1], 0, PSEUDO_SIZE); 
            total_clients--; 
            break; 
        }
    }
}

void update_last_connection(const char *pseudo) {
    sqlite3 *db;
    char *sql = "UPDATE utilisateurs SET derniere_connections = CURRENT_TIMESTAMP, actif = 1 WHERE pseudo = ?";
    sqlite3_stmt *stmt;
    if (sqlite3_open("MyTeams.db", &db) == SQLITE_OK) {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, pseudo, -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) != SQLITE_DONE) {
                printw("Erreur lors de la mise à jour de derniere_connections et actif.\n");
            } else {
                printw("Mise à jour réussie de derniere_connections et actif.\n");
            }
            sqlite3_finalize(stmt);
        } else {
            printw("Erreur lors de la préparation de la mise à jour de derniere_connections et actif.\n");
        }
        sqlite3_close(db);
    }
}

void update_disconnected(const char *pseudo) {
    sqlite3 *db;
    char *sql = "UPDATE utilisateurs SET actif = 0 WHERE pseudo = ?";
    sqlite3_stmt *stmt;
    if (sqlite3_open("MyTeams.db", &db) == SQLITE_OK) {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, pseudo, -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) != SQLITE_DONE) {
                printw("Erreur lors de la mise à jour de actif.\n");
            } else {
                printw("Mise à jour réussie de actif.\n");
            }
            sqlite3_finalize(stmt);
        } else {
            printw("Erreur lors de la préparation de la mise à jour de actif.\n");
        }
        sqlite3_close(db);
    }
}

int find_client_index(int sock) {
    for (int i = 0; i < total_clients; i++) {
        if (client_socks[i] == sock) {
            return i;
        }
    }
    return -1;
}

void kick_client(char *targetPseudo, int sender_sock, fd_set *master_fds) {
    for (int i = 0; i < total_clients; i++) {
        if (strcmp(client_pseudos[i], targetPseudo) == 0) {
            int targetSock = client_socks[i];

            char kickMsg[BUFFER_SIZE] = "Vous avez été expulsé du serveur.\n";
            send(targetSock, kickMsg, strlen(kickMsg), 0);

            close_socket(targetSock, master_fds);
            char logMsg[BUFFER_SIZE];
            snprintf(logMsg, sizeof(logMsg), "L'utilisateur %s a été expulsé par %s\n", targetPseudo, client_pseudos[find_client_index(sender_sock)]);
            log_message(logMsg);

            char broadcastMsg[BUFFER_SIZE];
            snprintf(broadcastMsg, sizeof(broadcastMsg), "%s a été expulsé du serveur.", targetPseudo);
            broadcast_message(sender_sock, broadcastMsg, "Serveur", true);
            return;
        }
    }

    char errorMsg[BUFFER_SIZE] = "Pseudo non trouvé.\n";
    send(sender_sock, errorMsg, strlen(errorMsg), 0);
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

