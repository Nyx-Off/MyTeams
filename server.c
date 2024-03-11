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

// Convertit une empreinte SHA256 en une chaîne de caractères hexadécimale
void sha256_to_string(unsigned char hash[SHA256_DIGEST_LENGTH], char outputBuffer[65]) {
    int i;
    // Itère sur chaque octet de l'empreinte SHA256
    for(i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        // Écrit deux caractères hexadécimaux dans le tampon de sortie pour l'octet actuel
        sprintf(outputBuffer + (i * 2), "%02x", hash[i]);
    }
    // Ajoute un caractère de terminaison à la fin de la chaîne de caractères
    outputBuffer[64] = 0;
}

// Hache un mot de passe en utilisant SHA256 et retourne le résultat sous forme de chaîne hexadécimale
void hash_password(const char* password, char hashedOutput[65]) {
    unsigned char hash[SHA256_DIGEST_LENGTH]; // Tableau pour stocker l'empreinte SHA256
    SHA256_CTX sha256; // Contexte SHA256 pour le hachage
    // Initialise le contexte SHA256
    SHA256_Init(&sha256);
    // Met à jour le contexte SHA256 avec le mot de passe
    SHA256_Update(&sha256, password, strlen(password));
    // Finalise le hachage et stocke le résultat dans `hash`
    SHA256_Final(hash, &sha256);
    // Convertit l'empreinte SHA256 en chaîne hexadécimale
    sha256_to_string(hash, hashedOutput);
}

// Envoie les détails d'un utilisateur au client à partir de la base de données
void send_user_details(int client_sock, const char *pseudo) {
    sqlite3 *db; // Pointeur vers la base de données SQLite
    sqlite3_stmt *stmt; // Déclaration préparée SQLite
    // Chaîne SQL pour sélectionner des informations d'utilisateur par pseudo
    const char *sql = "SELECT identifiant, permission, derniere_connections FROM utilisateurs WHERE pseudo = ?";
    // Ouvre la connexion à la base de données
    if (sqlite3_open("MyTeams.db", &db) == SQLITE_OK) {
        // Prépare la requête SQL pour exécution
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            // Lie le pseudo à la requête
            sqlite3_bind_text(stmt, 1, pseudo, -1, SQLITE_STATIC);
            // Exécute la requête et vérifie si une ligne est retournée
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                // Récupère les champs de la ligne retournée
                const char *identifiant = (const char *)sqlite3_column_text(stmt, 0);
                const char *permission = (const char *)sqlite3_column_text(stmt, 1);
                const char *derniere_connexion = (const char *)sqlite3_column_text(stmt, 2);
                char response[BUFFER_SIZE]; // Tampon pour la réponse
                // Formate la réponse avec les informations de l'utilisateur
                snprintf(response, BUFFER_SIZE, "Pseudo: %s\nIdentifiant: %s\nPermission: %s\nDernière connexion: %s\n",
                         pseudo, identifiant, permission, derniere_connexion);
                // Envoie la réponse au client
                send(client_sock, response, strlen(response), 0);
            } else {
                // Réponse en cas d'erreur ou si l'utilisateur n'est pas trouvé
                char response[BUFFER_SIZE] = "Erreur lors de la récupération des détails de l'utilisateur.\n";
                send(client_sock, response, strlen(response), 0);
            }
            // Finalise la déclaration pour libérer les ressources
            sqlite3_finalize(stmt);
        } else {
            // Réponse en cas d'erreur lors de la préparation de la requête
            char response[BUFFER_SIZE] = "Erreur lors de la préparation de la requête.\n";
            send(client_sock, response, strlen(response), 0);
        }
        // Ferme la connexion à la base de données
        sqlite3_close(db);
    } else {
        // Réponse en cas d'erreur de connexion à la base de données
        char response[BUFFER_SIZE] = "Erreur de connexion à la base de données.\n";
        send(client_sock, response, strlen(response), 0);
    }
}

// Vérifie les identifiants d'un utilisateur dans la base de données
int verify_credentials(char *identifiant, char *mdp, char *pseudo) {
    sqlite3 *db; // Pointeur vers la base de données SQLite
    char *sql; // Chaîne pour stocker la requête SQL
    int result = 0; // Résultat de la vérification, 0 par défaut (échec)
    sqlite3_stmt *stmt; // Déclaration préparée SQLite
    // Ouvre la connexion à la base de données
    if (sqlite3_open("MyTeams.db", &db) == SQLITE_OK) {
        // Définit la requête SQL pour vérifier l'identifiant et le mot de passe
        sql = "SELECT pseudo FROM utilisateurs WHERE identifiant = ? AND mdp = ?";
        // Prépare la requête SQL pour exécution
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            // Lie l'identifiant et le mot de passe à la requête
            sqlite3_bind_text(stmt, 1, identifiant, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, mdp, -1, SQLITE_STATIC);
            // Exécute la requête
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                // Récupère le pseudo si la vérification est réussie
                const char *retrieved_pseudo = (const char *)sqlite3_column_text(stmt, 0);
                if (retrieved_pseudo) {
                    // Copie le pseudo dans le paramètre fourni, en limitant la taille
                    strncpy(pseudo, retrieved_pseudo, PSEUDO_SIZE - 1);
                    pseudo[PSEUDO_SIZE - 1] = '\0';
                    result = 1; // Indique le succès
                }
            }
            // Finalise la déclaration pour libérer les ressources
            sqlite3_finalize(stmt);
        }
        // Ferme la connexion à la base de données
        sqlite3_close(db);
    }
    // Retourne le résultat de la vérification
    return result;
}

// Initialise le serveur pour écouter sur un port donné
void init_server(int *server_sock, struct sockaddr_in *server_addr, int port) {
    // Crée un socket pour le serveur
    *server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (*server_sock < 0) {
        perror("socket");
        exit(1); // Quitte en cas d'erreur
    }
    int optval = 1;
    // Configure le socket pour réutiliser l'adresse rapidement après une fermeture
    if (setsockopt(*server_sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        perror("setsockopt");
        close(*server_sock); // Ferme le socket en cas d'erreur
        exit(1);
    }
    // Initialise la structure d'adresse du serveur à zéro
    memset(server_addr, 0, sizeof(*server_addr));
    // Configure l'adresse du serveur
    server_addr->sin_family = AF_INET;
    server_addr->sin_addr.s_addr = INADDR_ANY; // Écoute sur toutes les interfaces
    server_addr->sin_port = htons(port); // Définit le port d'écoute
    // Lie l'adresse au socket
    if (bind(*server_sock, (struct sockaddr *)server_addr, sizeof(*server_addr)) < 0) {
        perror("bind");
        close(*server_sock); // Ferme le socket en cas d'erreur
        exit(1);
    }
    // Configure le socket pour écouter les connexions entrantes
    if (listen(*server_sock, MAX_CLIENTS) < 0) {
        perror("listen");
        close(*server_sock); // Ferme le socket en cas d'erreur
        exit(1);
    }
}



// Traite une nouvelle connexion sur le socket serveur
void handle_new_connection(int server_sock, fd_set *master_fds, int *fd_max) {
    int client_sock; // Socket pour le nouveau client
    struct sockaddr_in client_addr; // Adresse du client
    socklen_t client_addr_size = sizeof(client_addr);
    char buffer[1024]; // Tampon pour recevoir les données
    // Accepte une nouvelle connexion
    client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_addr_size);
    if (client_sock < 0) {
        perror("accept");
        return; // Retourne en cas d'erreur
    }
    // Reçoit les données du client
    ssize_t len = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
    if (len <= 0) {
        close(client_sock); // Ferme le socket client en cas d'erreur ou de déconnexion
        return;
    }
    buffer[len] = '\0'; // Assure que les données reçues forment une chaîne valide
    char identifiant[BUFFER_SIZE] = {0}, mdp[BUFFER_SIZE] = {0}, pseudo[PSEUDO_SIZE] = {0};
    // Extrait l'identifiant et le mot de passe du tampon
    sscanf(buffer, "%s %s", identifiant, mdp);
    // Vérifie les identifiants du client
    if (!verify_credentials(identifiant, mdp, pseudo)) {
        // Affiche un message en cas d'échec de la vérification
        printf("Échec de la vérification des identifiants pour la socket %d\n", client_sock);
        close(client_sock); // Ferme le socket client
        return;
    }
    // Met à jour la dernière connexion du client
    // Ajoute le socket client à l'ensemble des descripteurs
    FD_SET(client_sock, master_fds);
    // Met à jour le descripteur maximum si nécessaire
    if (client_sock > *fd_max) {
        *fd_max = client_sock;
    }
    // Stocke les informations du client pour une utilisation ultérieure
    // Affiche un message indiquant le succès de la connexion
    printf("Client connecté : %s (socket %d)\n", pseudo, client_sock);
}

// Trouve l'index d'un client dans le tableau `client_socks` en utilisant son socket
int find_client_index_by_sock(int sock) {
    // Itère sur le tableau des sockets clients
    for (int i = 0; i < total_clients; i++) {
        // Compare le socket passé en paramètre avec celui du client actuel dans la boucle
        if (client_socks[i] == sock) {
            // Si les sockets correspondent, retourne l'index du client
            return i;
        }
    }
    // Si aucun client correspondant n'a été trouvé, retourne -1
    return -1;
}
// Gère les messages reçus d'un client spécifique
void handle_client_message(int client_sock, fd_set *master_fds) {
    int port = 4242; // Le port sur lequel le serveur écoute
    char buffer[BUFFER_SIZE] = {0}; // Buffer pour stocker le message reçu
    char newPseudo[PSEUDO_SIZE] = {0}; // Buffer pour un nouveau pseudo demandé par le client
    char confirmationPseudo[PSEUDO_SIZE] = {0}; // Buffer pour la confirmation du nouveau pseudo
    // Reçoit le message du client
    ssize_t len = recv(client_sock, buffer, BUFFER_SIZE - 1, 0);
    
    // Vérifie si la réception a échoué ou si le client s'est déconnecté
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
            // Affichage de la commande reçue côté serveur
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
            return; // Ne pas traiter d'autres commandes ou messages
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

            // Vérifier si l'utilisateur est un admin
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
        broadcast_message(client_sock, buffer, client_pseudos[client_index], false); // Ajouter le quatrième argument "false"
        wrefresh(stdscr); 
    }
}

// Récupère l'identifiant d'un utilisateur à partir de son pseudo
int get_user_id_by_pseudo(const char *pseudo) {
    sqlite3 *db; // Pointeur vers la base de données
    sqlite3_stmt *stmt; // Déclaration préparée SQLite
    // Requête SQL pour sélectionner l'id d'un utilisateur par son pseudo
    const char *sql = "SELECT id FROM utilisateurs WHERE pseudo = ?";
    int userId = -1; // Variable pour stocker l'identifiant utilisateur, -1 si non trouvé

    // Tente d'ouvrir la base de données
    if (sqlite3_open("MyTeams.db", &db) == SQLITE_OK) {
        // Prépare la requête SQL pour l'exécution
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            // Lie le pseudo à la requête
            sqlite3_bind_text(stmt, 1, pseudo, -1, SQLITE_STATIC);
            // Exécute la requête
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                // Récupère l'identifiant utilisateur si trouvé
                userId = sqlite3_column_int(stmt, 0);
            }
            // Libère la mémoire de la déclaration préparée
            sqlite3_finalize(stmt);
        }
        // Ferme la connexion à la base de données
        sqlite3_close(db);
    }
    return userId; // Retourne l'identifiant utilisateur ou -1 si non trouvé
}

// Vérifie si un utilisateur est administrateur à partir de son pseudo
int is_user_admin(const char *pseudo) {
    sqlite3 *db; // Pointeur vers la base de données
    sqlite3_stmt *stmt; // Déclaration préparée SQLite
    // Requête SQL pour sélectionner les permissions d'un utilisateur par son pseudo
    const char *sql = "SELECT permission FROM utilisateurs WHERE pseudo = ?";
    int isAdmin = 0; // Flag pour indiquer si l'utilisateur est admin

    // Tente d'ouvrir la base de données
    if (sqlite3_open("MyTeams.db", &db) == SQLITE_OK) {
        // Prépare la requête SQL pour l'exécution
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            // Lie le pseudo à la requête
            sqlite3_bind_text(stmt, 1, pseudo, -1, SQLITE_STATIC);
            // Exécute la requête
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                // Récupère la permission et vérifie si elle correspond à "admin"
                const char *permission = (const char *)sqlite3_column_text(stmt, 0);
                isAdmin = strcmp(permission, "admin") == 0;
            }
            // Libère la mémoire de la déclaration préparée
            sqlite3_finalize(stmt);
        }
        // Ferme la connexion à la base de données
        sqlite3_close(db);
    }
    return isAdmin; // Retourne 1 si admin, 0 sinon
}

// Crée un nouvel utilisateur dans la base de données
int create_user(const char *permission, const char *identifiant, const char *pseudo, const char *mdp) {
    sqlite3 *db; // Pointeur vers la base de données
    // Tente d'ouvrir la base de données
    int rc = sqlite3_open("MyTeams.db", &db);
    if (rc != SQLITE_OK) {
        // Gestion de l'erreur d'ouverture de la base de données
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return -1; // Retourne -1 en cas d'erreur
    }
    // Requête SQL pour insérer un nouvel utilisateur
    char *sql = "INSERT INTO utilisateurs (permission, identifiant, pseudo, mdp) VALUES (?, ?, ?, ?);";
    sqlite3_stmt *stmt;
    char hashedPassword[65]; // Tampon pour stocker le mot de passe haché
    // Hache le mot de passe avant de l'insérer dans la base de données
    hash_password(mdp, hashedPassword);
    
    // Prépare la requête SQL pour l'exécution
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc == SQLITE_OK) {
        // Lie les paramètres à la requête
        sqlite3_bind_text(stmt, 1, permission, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, identifiant, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, pseudo, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, hashedPassword, -1, SQLITE_STATIC);
    } else {
        // Gestion de l'erreur de préparation de la requête
        fprintf(stderr, "Failed to execute statement: %s\n", sqlite3_errmsg(db));
    }
    // Exécute la requête
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        // Gestion de l'erreur d'insertion
        printf("ERROR inserting data: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return -2; // Retourne -2 en cas d'erreur d'insertion
    }
    // Libère la mémoire de la déclaration préparée et ferme la base de données
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0; // Succès
}


// Vérifie si un pseudo est disponible (non utilisé) dans la base de données
int check_pseudo_availability(char *pseudo) {
    sqlite3 *db; // Pointeur vers la base de données
    // Requête SQL pour compter le nombre d'occurrences d'un pseudo
    char *sql = "SELECT COUNT(*) FROM utilisateurs WHERE pseudo = ?";
    sqlite3_stmt *stmt; // Déclaration préparée SQLite
    int count = 1; // Initialise le compteur à 1 (indisponible par défaut)

    // Tente d'ouvrir la base de données
    if (sqlite3_open("MyTeams.db", &db) == SQLITE_OK) {
        // Prépare la requête SQL pour l'exécution
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            // Lie le pseudo à la requête
            sqlite3_bind_text(stmt, 1, pseudo, -1, SQLITE_STATIC);
            // Exécute la requête
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                // Récupère le résultat (nombre d'occurrences du pseudo)
                count = sqlite3_column_int(stmt, 0);
            }
            // Libère la mémoire de la déclaration préparée
            sqlite3_finalize(stmt);
        }
        // Ferme la connexion à la base de données
        sqlite3_close(db);
    }
    return count == 0; // Retourne vrai (1) si le pseudo est disponible, faux (0) sinon
}

// Met à jour le pseudo d'un utilisateur dans la base de données
void update_pseudo_in_db(int userId, char *newPseudo) {
    sqlite3 *db; // Pointeur vers la base de données
    // Requête SQL pour mettre à jour le pseudo d'un utilisateur par son identifiant
    char *sql = "UPDATE utilisateurs SET pseudo = ? WHERE id = ?";
    sqlite3_stmt *stmt; // Déclaration préparée SQLite

    // Tente d'ouvrir la base de données
    if (sqlite3_open("MyTeams.db", &db) == SQLITE_OK) {
        // Prépare la requête SQL pour l'exécution
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            // Lie le nouveau pseudo et l'identifiant de l'utilisateur à la requête
            sqlite3_bind_text(stmt, 1, newPseudo, -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 2, userId);
            // Exécute la requête
            if (sqlite3_step(stmt) != SQLITE_DONE) {
                // Affiche un message d'erreur si la mise à jour échoue
                printw("Erreur lors de la mise à jour du pseudo.\n");
            } else {
                // Affiche un message de succès si la mise à jour réussit
                printw("Pseudo mis à jour avec succès.\n");
            }
            // Libère la mémoire de la déclaration préparée
            sqlite3_finalize(stmt);
        } else {
            // Affiche un message d'erreur si la requête ne peut pas être préparée
            printw("Erreur lors de la préparation de la mise à jour du pseudo.\n");
        }
        // Ferme la connexion à la base de données
        sqlite3_close(db);
    }
}


// Diffuse un message de l'expéditeur à tous les autres clients connectés
void broadcast_message(int sender_sock, char *message, char *sender_pseudo, bool is_status) {
    // Vérifie si l'expéditeur est administrateur
    int isAdmin = is_user_admin(sender_pseudo);
    char full_message[FULL_MESSAGE_SIZE]; // Prépare le message complet à diffuser

    // Construit le message selon que c'est un statut ou un message d'un admin/utilisateur
    if (is_status) {
        // Format spécifique pour les messages de statut
        snprintf(full_message, sizeof(full_message), "STATUS de %s: %s", sender_pseudo, message);
    } else if (isAdmin) {
        // Les messages des administrateurs peuvent être formatés différemment ici si nécessaire
        snprintf(full_message, sizeof(full_message), "%s: %s", sender_pseudo, message);
    } else {
        // Format pour les messages des utilisateurs non administrateurs
        snprintf(full_message, sizeof(full_message), "%s: %s", sender_pseudo, message);
    }

    // Boucle sur tous les clients connectés pour diffuser le message
    for (int i = 0; i < total_clients; i++) {
        // Vérifie que le client n'est pas l'expéditeur et qu'il n'est pas en mode Ne Pas Déranger
        if (client_socks[i] != sender_sock && !client_dnd_mode[i]) {
            // Envoie le message
            send(client_socks[i], full_message, strlen(full_message), 0);
        }
    }

    // Optionnel : loggue le message pour un suivi ou des audits
    log_message(full_message);
}

// Ferme une socket spécifiée et la supprime de l'ensemble des sockets actives
void close_socket(int sock, fd_set *master_fds) {
    // Ferme la socket
    close(sock);
    // Supprime la socket de l'ensemble des sockets actives
    FD_CLR(sock, master_fds);

    // Trouve l'index du client associé à la socket
    int client_index = find_client_index(sock);
    // Si le client est trouvé, met à jour son statut comme déconnecté
    if (client_index != -1) {
        update_disconnected(client_pseudos[client_index]);
    }

    // Supprime le client de la liste des clients actifs
    for (int i = 0; i < total_clients; i++) {
        if (client_socks[i] == sock) {
            // Décale tous les éléments suivants d'une position vers le bas pour remplir le trou
            for (int j = i; j < total_clients - 1; j++) {
                client_socks[j] = client_socks[j + 1];
                strncpy(client_pseudos[j], client_pseudos[j + 1], PSEUDO_SIZE);
            }
            // Réinitialise le dernier élément puisqu'il est maintenant en double
            client_socks[total_clients - 1] = 0;
            memset(client_pseudos[total_clients - 1], 0, PSEUDO_SIZE);
            // Réduit le nombre total de clients actifs
            total_clients--;
            break;
        }
    }
}


// Met à jour la dernière connexion et le statut d'activité d'un utilisateur
void update_last_connection(const char *pseudo) {
    sqlite3 *db; // Pointeur vers la base de données SQLite
    // Requête SQL pour mettre à jour la dernière connexion et le statut d'activité
    char *sql = "UPDATE utilisateurs SET derniere_connections = CURRENT_TIMESTAMP, actif = 1 WHERE pseudo = ?";
    sqlite3_stmt *stmt; // Déclaration préparée SQLite

    // Ouvre la base de données
    if (sqlite3_open("MyTeams.db", &db) == SQLITE_OK) {
        // Prépare la requête SQL pour l'exécution
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            // Lie le pseudo à la requête
            sqlite3_bind_text(stmt, 1, pseudo, -1, SQLITE_TRANSIENT);
            // Exécute la requête
            if (sqlite3_step(stmt) != SQLITE_DONE) {
                // Affiche un message d'erreur si la mise à jour échoue
                printw("Erreur lors de la mise à jour de derniere_connections et actif.\n");
            } else {
                // Affiche un message de succès si la mise à jour réussit
                printw("Mise à jour réussie de derniere_connections et actif.\n");
            }
            // Libère la mémoire de la déclaration préparée
            sqlite3_finalize(stmt);
        } else {
            // Affiche un message d'erreur si la requête ne peut pas être préparée
            printw("Erreur lors de la préparation de la mise à jour de derniere_connections et actif.\n");
        }
        // Ferme la connexion à la base de données
        sqlite3_close(db);
    }
}

// Met à jour le statut d'activité d'un utilisateur pour indiquer qu'il est déconnecté
void update_disconnected(const char *pseudo) {
    sqlite3 *db; // Pointeur vers la base de données SQLite
    // Requête SQL pour mettre à jour le statut d'activité
    char *sql = "UPDATE utilisateurs SET actif = 0 WHERE pseudo = ?";
    sqlite3_stmt *stmt; // Déclaration préparée SQLite

    // Ouvre la base de données
    if (sqlite3_open("MyTeams.db", &db) == SQLITE_OK) {
        // Prépare la requête SQL pour l'exécution
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            // Lie le pseudo à la requête
            sqlite3_bind_text(stmt, 1, pseudo, -1, SQLITE_TRANSIENT);
            // Exécute la requête
            if (sqlite3_step(stmt) != SQLITE_DONE) {
                // Affiche un message d'erreur si la mise à jour échoue
                printw("Erreur lors de la mise à jour de actif.\n");
            } else {
                // Affiche un message de succès si la mise à jour réussit
                printw("Mise à jour réussie de actif.\n");
            }
            // Libère la mémoire de la déclaration préparée
            sqlite3_finalize(stmt);
        } else {
            // Affiche un message d'erreur si la requête ne peut pas être préparée
            printw("Erreur lors de la préparation de la mise à jour de actif.\n");
        }
        // Ferme la connexion à la base de données
        sqlite3_close(db);
    }
}

// Trouve l'index d'un client dans le tableau `client_socks` en utilisant son socket
int find_client_index(int sock) {
    // Itère sur le tableau des sockets clients
    for (int i = 0; i < total_clients; i++) {
        if (client_socks[i] == sock) {
            return i; // Retourne l'index du client si trouvé
        }
    }
    return -1; // Retourne -1 si le client n'est pas trouvé
}

// Expulse un client spécifié par son pseudo
void kick_client(char *targetPseudo, int sender_sock, fd_set *master_fds) {
    // Parcourt la liste des clients connectés
    for (int i = 0; i < total_clients; i++) {
        // Vérifie si le pseudo correspond à celui d'un client connecté
        if (strcmp(client_pseudos[i], targetPseudo) == 0) {
            int targetSock = client_socks[i]; // Récupère le socket du client à expulser

            // Envoie un message d'expulsion au client concerné
            char kickMsg[BUFFER_SIZE] = "Vous avez été expulsé du serveur.\n";
            send(targetSock, kickMsg, strlen(kickMsg), 0);

            // Ferme la socket du client expulsé et met à jour les structures de données
            close_socket(targetSock, master_fds);

            // Log l'action d'expulsion
            char logMsg[BUFFER_SIZE];
            snprintf(logMsg, sizeof(logMsg), "L'utilisateur %s a été expulsé par %s\n", targetPseudo, client_pseudos[find_client_index(sender_sock)]);
            log_message(logMsg);

            // Informe tous les autres clients de l'expulsion
            char broadcastMsg[BUFFER_SIZE];
            snprintf(broadcastMsg, sizeof(broadcastMsg), "%s a été expulsé du serveur.", targetPseudo);
            broadcast_message(sender_sock, broadcastMsg, "Serveur", true);
            return; // Quitte la fonction après l'expulsion
        }
    }

    // Si le pseudo n'a pas été trouvé, informe le client demandeur
    char errorMsg[BUFFER_SIZE] = "Pseudo non trouvé.\n";
    send(sender_sock, errorMsg, strlen(errorMsg), 0);
}

// Journalise un message dans un fichier de log avec un horodatage
void log_message(const char *message) {
    // Ouvre le fichier de log en mode append
    FILE *file = fopen("messages.log", "a");
    if (file == NULL) {
        // Gère l'erreur d'ouverture du fichier
        perror("Erreur lors de l'ouverture du fichier de log");
        return;
    }
    // Obtient l'heure actuelle et la formate
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char formatted_time[20]; 
    strftime(formatted_time, sizeof(formatted_time), "%Y-%m-%d %H:%M:%S", t);

    // Écrit le message dans le fichier avec l'horodatage
    fprintf(file, "[%s] %s\n", formatted_time, message);

    // Ferme le fichier
    fclose(file);
}
