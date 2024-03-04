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
#define FULL_MESSAGE_SIZE (PSEUDO_SIZE + BUFFER_SIZE + 2) // +2 pour ": " et '\0'

#define RECEIVED_MSG_COLOR_PAIR 1
#define SENT_MSG_COLOR_PAIR 2
#define ADMIN_MSG_COLOR_PAIR 3

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
int create_user(const char *permission, const char *identifiant, const char *pseudo, const char *mdp);
int is_user_admin(const char *pseudo);
void hash_password(const char* password, char hashedOutput[65]);
void sha256_to_string(unsigned char hash[SHA256_DIGEST_LENGTH], char outputBuffer[65]);
void send_user_details(int client_sock, const char *pseudo);
void update_last_connection(const char *pseudo);
void update_disconnected(const char *pseudo) ;


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
    init_pair(ADMIN_MSG_COLOR_PAIR, COLOR_RED, COLOR_BLACK); 

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
    update_last_connection(pseudo);

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
        if (strncmp(buffer, "/info", 5) == 0) {
            char response[BUFFER_SIZE];
            int uptime_seconds = difftime(time(NULL), server_start_time);
            int hours = uptime_seconds / 3600;
            int minutes = (uptime_seconds % 3600) / 60;
            int seconds = uptime_seconds % 60;

            snprintf(response, BUFFER_SIZE, "IP: %s\nPort: %d\nMOTD: %s\nUptime: %02d:%02d:%02d\nClients: %d/%d\n", 
                    "127.0.0.1", port, motd, hours, minutes, seconds, total_clients, max_clients);

            send(client_sock, response, strlen(response), 0); // Envoyer uniquement au demandeur
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
                // Log pour débogage
                wprintw(stdscr, "Mode Admin activé pour /who\n");
                
                for (int i = 0; i < total_clients; i++) {
                    strcat(response, client_pseudos[i]);
                    strcat(response, " (Détails):\n");

                    // Appeler la fonction pour envoyer les détails de l'utilisateur
                    send_user_details(client_sock, client_pseudos[i]);

                    // Attendre un court instant pour éviter de mélanger les réponses
                    usleep(10000);
                }
            } else {
                // Log pour débogage
                wprintw(stdscr, "Mode Utilisateur normal pour /who\n");

                for (int i = 0; i < total_clients; i++) {
                    strcat(response, client_pseudos[i]);
                    strcat(response, "\n");
                }
            }

            // Envoyer la réponse
            send(client_sock, response, strlen(response), 0);
            return;
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

        if (strncmp(buffer, "/createuser", 11) == 0) {
            if (is_user_admin(client_pseudos[client_index])) {
                // Extraire les détails de la commande
                char permission[BUFFER_SIZE], identifiant[BUFFER_SIZE], confIdentifiant[BUFFER_SIZE],
                    pseudo[BUFFER_SIZE], confPseudo[BUFFER_SIZE], mdp[BUFFER_SIZE], confMdp[BUFFER_SIZE];

                if (sscanf(buffer, "/createuser %s %s %s %s %s %s %s", permission, identifiant, confIdentifiant,pseudo, confPseudo, mdp, confMdp) == 7) {
                    // Valider et créer le compte utilisateur...
                    if (strcmp(identifiant, confIdentifiant) == 0 && strcmp(pseudo, confPseudo) == 0 && strcmp(mdp, confMdp) == 0) {
                        // Appeler une fonction pour créer l'utilisateur dans la base de données
                        // Assurez-vous que cette fonction vérifie également l'unicité de l'identifiant et du pseudo
                        int creationStatus = create_user(permission, identifiant, pseudo, mdp);
                        if (creationStatus == 0) {
                            // Envoi d'un message de succès au client
                            char successMsg[BUFFER_SIZE] = "Utilisateur créé avec succès.\n";
                            send(client_sock, successMsg, strlen(successMsg), 0);
                            return;
                        } 
                        else {
                            // Envoi d'un message d'erreur au client
                            char errorMsg[BUFFER_SIZE] = "Erreur lors de la création de l'utilisateur.\n";
                            send(client_sock, errorMsg, strlen(errorMsg), 0);
                            return;
                        }
                    } 
                    else {
                        // Envoi d'un message d'erreur de validation au client
                        char validationMsg[BUFFER_SIZE] = "Erreur de validation des données.\n";
                        send(client_sock, validationMsg, strlen(validationMsg), 0);
                        return;
                    }
                } 
                else {
                    // Commande incomplète ou incorrecte
                    char errorMsg[BUFFER_SIZE] = "Commande /createuser format incorrect.\n";
                    send(client_sock, errorMsg, strlen(errorMsg), 0);
                    return;
                }
            }
            else {
                // Envoi d'un message d'erreur au client
                char errorMsg[BUFFER_SIZE] = "Vous n'êtes pas autorisé à exécuter cette commande.\n";
                send(client_sock, errorMsg, strlen(errorMsg), 0);
                return;
            }
        }  

        int isAdmin = is_user_admin(client_pseudos[client_index]);
        char full_message[FULL_MESSAGE_SIZE];
        snprintf(full_message, sizeof(full_message), "%s: %s", client_pseudos[client_index], buffer);

        if (isAdmin) {
            wattron(stdscr, COLOR_PAIR(ADMIN_MSG_COLOR_PAIR)); // Utiliser la couleur rouge pour les admins
            wprintw(stdscr, "%s\n", full_message);
            wattroff(stdscr, COLOR_PAIR(ADMIN_MSG_COLOR_PAIR));
        } else {
            wattron(stdscr, COLOR_PAIR(RECEIVED_MSG_COLOR_PAIR)); // Couleur par défaut pour les autres utilisateurs
            wprintw(stdscr, "%s\n", full_message);
            wattroff(stdscr, COLOR_PAIR(RECEIVED_MSG_COLOR_PAIR));
        }

        log_message(full_message); // Log le message comme avant
        broadcast_message(client_sock, buffer, client_pseudos[client_index]); // Broadcast le message comme avant

        wrefresh(stdscr); // Rafraîchir l'écran après l'ajout du message
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
    int isAdmin = 0; // Par défaut, l'utilisateur n'est pas admin
    
    if (sqlite3_open("MyTeams.db", &db) == SQLITE_OK) {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, pseudo, -1, SQLITE_STATIC);
            
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                const char *permission = (const char *)sqlite3_column_text(stmt, 0);
                isAdmin = strcmp(permission, "admin") == 0; // Vérifie si la permission est "admin"
            }
            
            sqlite3_finalize(stmt);
        }
        sqlite3_close(db);
    }
    
    return isAdmin;
}

int create_user(const char *permission, const char *identifiant, const char *pseudo, const char *mdp) {
    sqlite3 *db;
    //char *err_msg = 0;
    int rc = sqlite3_open("MyTeams.db", &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return -1; // Échec de l'ouverture de la base de données
    }

    char *sql = "INSERT INTO utilisateurs (permission, identifiant, pseudo, mdp) VALUES (?, ?, ?, ?);";
    sqlite3_stmt *stmt;

    char hashedPassword[65];
    hash_password(mdp, hashedPassword);

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc == SQLITE_OK) {
        // Binder les paramètres
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
        return -2; // Échec de l'insertion
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return 0; // Succès
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

#include <ncurses.h>

void update_pseudo_in_db(int userId, char *newPseudo) {
    sqlite3 *db;
    char *sql = "UPDATE utilisateurs SET pseudo = ? WHERE id = ?";
    sqlite3_stmt *stmt;

    if (sqlite3_open("MyTeams.db", &db) == SQLITE_OK) {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, newPseudo, -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 2, userId);

            if (sqlite3_step(stmt) != SQLITE_DONE) {
                // Erreur lors de la mise à jour du pseudo.
                printw("Erreur lors de la mise à jour du pseudo.\n");
            } else {
                // Pseudo mis à jour avec succès.
                printw("Pseudo mis à jour avec succès.\n");
            }

            sqlite3_finalize(stmt);
        } else {
            // Erreur lors de la préparation de la mise à jour du pseudo.
            printw("Erreur lors de la préparation de la mise à jour du pseudo.\n");
        }
        sqlite3_close(db);
    }
}



void broadcast_message(int sender_sock, char *message, char *sender_pseudo) {
    int isAdmin = is_user_admin(sender_pseudo); // Vérifie si l'expéditeur est un admin
    char full_message[BUFFER_SIZE + PSEUDO_SIZE + 10]; // +10 pour préfixe potentiel et espace supplémentaire

    // Préfixer le message avec "ADMIN:" si l'expéditeur est un administrateur
    if (isAdmin) {
        snprintf(full_message, sizeof(full_message), "ADMIN: %s: %s", sender_pseudo, message);
    } else {
        snprintf(full_message, sizeof(full_message), "%s: %s", sender_pseudo, message);
    }

    for (int i = 0; i < total_clients; i++) {
        if (client_socks[i] != sender_sock) {
            send(client_socks[i], full_message, strlen(full_message), 0);
        }
    }
}


void close_socket(int sock, fd_set *master_fds) {
    close(sock); // Fermer la socket
    FD_CLR(sock, master_fds); // Retirer la socket de l'ensemble des descripteurs

    // Mettre à jour la colonne actif
    int client_index = find_client_index(sock);
    if (client_index != -1) {
        update_disconnected(client_pseudos[client_index]);
    }

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



void update_last_connection(const char *pseudo) {
    sqlite3 *db;
    char *sql = "UPDATE utilisateurs SET derniere_connections = CURRENT_TIMESTAMP, actif = 1 WHERE pseudo = ?";
    sqlite3_stmt *stmt;

    if (sqlite3_open("MyTeams.db", &db) == SQLITE_OK) {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, pseudo, -1, SQLITE_TRANSIENT);

            if (sqlite3_step(stmt) != SQLITE_DONE) {
                // Erreur lors de la mise à jour de derniere_connections et actif.
                printw("Erreur lors de la mise à jour de derniere_connections et actif.\n");
            } else {
                // Mise à jour réussie.
                printw("Mise à jour réussie de derniere_connections et actif.\n");
            }

            sqlite3_finalize(stmt);
        } else {
            // Erreur lors de la préparation de la mise à jour de derniere_connections et actif.
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
                // Erreur lors de la mise à jour de actif.
                printw("Erreur lors de la mise à jour de actif.\n");
            } else {
                // Mise à jour réussie.
                printw("Mise à jour réussie de actif.\n");
            }

            sqlite3_finalize(stmt);
        } else {
            // Erreur lors de la préparation de la mise à jour de actif.
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

