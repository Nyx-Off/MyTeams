#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <ncurses.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <openssl/sha.h>

#define BUFFER_SIZE 1024
#define RECEIVED_MSG_COLOR_PAIR 1
#define SENT_MSG_COLOR_PAIR 2
#define ADMIN_MSG_COLOR_PAIR 3
#define STATUS_MSG_COLOR_PAIR 4

void initialize_ncurses();
void setup_color_pairs();
void init_connection(int *sock, struct sockaddr_in *serverAddr, char *serverAddress, int port);
void handle_user_input(WINDOW *input_win, WINDOW *messages_win, int sock);
void handle_server_message(WINDOW *messages_win, int sock);
void close_application(int sock, WINDOW *input_win, WINDOW *messages_win);
void send_credentials(int sock, char *identifiant, char *mdp);
void sha256_to_string(unsigned char hash[SHA256_DIGEST_LENGTH], char outputBuffer[65]);
void hash_password(const char* password, char hashedOutput[65]);

int main(int argc, char *argv[]) {
    // Vérifie si le nombre d'arguments est correct
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <Server Address> <Port> <Identifiant> <MotDePasse>\n", argv[0]);
        return 1;
    }

    // Initialise ncurses et configure les paires de couleurs
    initialize_ncurses();
    setup_color_pairs();

    // Récupère les arguments de la ligne de commande
    char *serverAddress = argv[1];
    int port = atoi(argv[2]);
    char *identifiant = argv[3];
    char *mdp = argv[4];

    // Initialise la connexion au serveur
    int sock;
    struct sockaddr_in serverAddr;
    init_connection(&sock, &serverAddr, serverAddress, port);

    // Envoie les identifiants au serveur
    send_credentials(sock, identifiant, mdp);

    // Crée les fenêtres pour les messages et l'entrée utilisateur
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    WINDOW *messages_win = newwin(max_y - 1, max_x, 0, 0);
    WINDOW *input_win = newwin(1, max_x, max_y - 1, 0);
    scrollok(messages_win, TRUE);

    // Affiche un message de connexion réussie
    wprintw(messages_win, "Connected to server %s:%d as %s\n", serverAddress, port, identifiant);
    wrefresh(messages_win);
    wrefresh(input_win);

    // Initialise le set de descripteurs de fichiers pour select()
    fd_set read_fds;
    int fd_max = sock;

    // Boucle principale : attend l'entrée de l'utilisateur ou un message du serveur
    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(sock, &read_fds);

        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;

        // Utilise select() pour attendre une activité sur l'un des descripteurs de fichiers
        if (select(fd_max + 1, &read_fds, NULL, NULL, &tv) == -1) {
            perror("select");
            break;
        }

        // Si l'entrée standard est prête à être lue, gère l'entrée de l'utilisateur
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            handle_user_input(input_win, messages_win, sock);
        }

        // Si le socket est prêt à être lu, gère le message du serveur
        if (FD_ISSET(sock, &read_fds)) {
            handle_server_message(messages_win, sock);
        }
    }

    // Ferme l'application
    close_application(sock, input_win, messages_win);
    return 0;
}
// Convertit un hash SHA256 en une chaîne de caractères
void sha256_to_string(unsigned char hash[SHA256_DIGEST_LENGTH], char outputBuffer[65]) {
    int i;
    for(i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        // Convertit chaque octet du hash en une chaîne hexadécimale
        sprintf(outputBuffer + (i * 2), "%02x", hash[i]);
    }
    // Termine la chaîne avec un caractère nul
    outputBuffer[64] = 0;
}

// Hache un mot de passe avec SHA256
void hash_password(const char* password, char hashedOutput[65]) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    // Initialise le contexte SHA256
    SHA256_Init(&sha256);
    // Met à jour le contexte avec le mot de passe
    SHA256_Update(&sha256, password, strlen(password));
    // Finalise le hash
    SHA256_Final(hash, &sha256);
    // Convertit le hash en une chaîne de caractères
    sha256_to_string(hash, hashedOutput);
}

// Envoie les identifiants au serveur
void send_credentials(int sock, char *identifiant, char *mdp) {
    char hashedPassword[65];
    // Hache le mot de passe
    hash_password(mdp, hashedPassword); 

    char credentials[1024]; 
    // Prépare la chaîne à envoyer
    sprintf(credentials, "%s %s", identifiant, hashedPassword); 
    // Envoie la chaîne au serveur
    if (send(sock, credentials, strlen(credentials), 0) < 0) {
        perror("send credentials");
        close(sock);
        endwin();
        exit(1);
    }
}

// Initialise ncurses
void initialize_ncurses() {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    start_color();
}

// Configure les paires de couleurs pour ncurses
void setup_color_pairs() {
    init_pair(RECEIVED_MSG_COLOR_PAIR, COLOR_CYAN, COLOR_BLACK);
    init_pair(SENT_MSG_COLOR_PAIR, COLOR_GREEN, COLOR_BLACK);
    init_pair(ADMIN_MSG_COLOR_PAIR, COLOR_RED, COLOR_BLACK); 
    init_pair(STATUS_MSG_COLOR_PAIR, COLOR_MAGENTA, COLOR_BLACK);
}

// Initialise une connexion TCP au serveur spécifié par son adresse et port.
void init_connection(int *sock, struct sockaddr_in *serverAddr, char *serverAddress, int port) {
    // Création du socket TCP.
    *sock = socket(AF_INET, SOCK_STREAM, 0);
    if (*sock < 0) {
        perror("socket");
        endwin(); // Ferme l'environnement ncurses proprement avant de quitter.
        exit(1);
    }

    // Configuration du timeout pour les opérations de réception et d'envoi.
    struct timeval tv;
    tv.tv_sec = 5; // Délai d'attente de 5 secondes.
    tv.tv_usec = 0;
    setsockopt(*sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
    setsockopt(*sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof tv);

    // Initialisation de la structure d'adresse avec zéro.
    memset(serverAddr, 0, sizeof(*serverAddr));
    serverAddr->sin_family = AF_INET; // Utilisation d'IPv4.
    serverAddr->sin_port = htons(port); // Convertit le numéro de port au format réseau.
    // Convertit l'adresse IP en texte en format binaire.
    if (inet_pton(AF_INET, serverAddress, &serverAddr->sin_addr) <= 0) {
        perror("inet_pton");
        close(*sock); // Ferme le socket en cas d'erreur.
        endwin(); // Ferme l'environnement ncurses proprement.
        exit(1);
    }

    // Établissement de la connexion au serveur.
    if (connect(*sock, (struct sockaddr *)serverAddr, sizeof(*serverAddr)) < 0) {
        perror("connect");
        close(*sock); // Ferme le socket en cas d'erreur.
        endwin(); // Ferme l'environnement ncurses proprement.
        exit(1);
    }
}

// Traite les entrées utilisateur depuis une fenêtre ncurses et les envoie au serveur.
void handle_user_input(WINDOW *input_win, WINDOW *messages_win, int sock) {
    char input_buffer[BUFFER_SIZE] = {0}; // Buffer pour stocker l'entrée utilisateur.
    int input_pos = 0; // Position actuelle dans le buffer.
    werase(input_win); // Efface le contenu de la fenêtre d'entrée.
    wrefresh(input_win); // Rafraîchit la fenêtre pour afficher les modifications.

    int ch; // Caractère lu.
    // Lecture des caractères jusqu'à la saisie d'Entrée ou que le buffer soit plein.
    while ((ch = wgetch(input_win)) != '\n' && input_pos < BUFFER_SIZE - 1) {
        if (ch == KEY_BACKSPACE || ch == 127) { // Gestion de la touche de retour arrière.
            if (input_pos > 0) {
                input_pos--;
                wmove(input_win, 0, input_pos); // Déplace le curseur.
                wdelch(input_win); // Supprime le caractère sous le curseur.
            }
        } else {
            input_buffer[input_pos++] = (char)ch; // Ajoute le caractère au buffer.
            waddch(input_win, ch); // Affiche le caractère dans la fenêtre.
        }
    }
    input_buffer[input_pos] = '\0'; // Termine la chaîne.

    // Traite les commandes spéciales ("/exit") ou envoie le message au serveur.
    if (strcmp(input_buffer, "/exit") == 0) {
        close_application(sock, input_win, messages_win); // Ferme l'application proprement.
        exit(0);
    } else if (strncmp(input_buffer, "/status ", 7) == 0) {
        send(sock, input_buffer, strlen(input_buffer), 0); // Envoie la commande de statut au serveur.
    } else if (strlen(input_buffer) > 0) {
        wattron(messages_win, COLOR_PAIR(SENT_MSG_COLOR_PAIR)); // Active la couleur pour le message envoyé.
        wprintw(messages_win, "> %s\n", input_buffer); // Affiche le message dans la fenêtre des messages.
        wattroff(messages_win, COLOR_PAIR(SENT_MSG_COLOR_PAIR)); // Désactive la couleur.
        wrefresh(messages_win); // Rafraîchit la fenêtre pour afficher les modifications.
        send(sock, input_buffer, strlen(input_buffer), 0); // Envoie le message au serveur.
    }

    werase(input_win); // Efface le contenu de la fenêtre d'entrée après l'envoi.
    wrefresh(input_win); // Rafraîchit la fenêtre pour afficher les modifications.
}

// Gère les messages reçus du serveur et les affiche dans la fenêtre des messages.
void handle_server_message(WINDOW *messages_win, int sock) {
    char buffer[BUFFER_SIZE] = {0}; // Buffer pour stocker le message reçu.
    // Tente de lire un message du serveur.
    ssize_t len = recv(sock, buffer, BUFFER_SIZE - 1, 0);

    // Vérifie si la connexion avec le serveur a été perdue.
    if (len <= 0) {
        // Informe l'utilisateur que le serveur est déconnecté.
        wprintw(messages_win, "Server disconnected. Press any key to exit.\n");
        // Ferme l'application proprement.
        close_application(sock, NULL, messages_win);
        exit(0);
    } else {
        // Gère différents types de messages en fonction de leur préfixe.
        if (strncmp(buffer, "[ADMIN]: ", 9) == 0) {
            // Affiche les messages administrateur avec une couleur spécifique.
            wattron(messages_win, COLOR_PAIR(ADMIN_MSG_COLOR_PAIR));
            wprintw(messages_win, "%s\n", buffer); 
            wattroff(messages_win, COLOR_PAIR(ADMIN_MSG_COLOR_PAIR));
        } else if (strncmp(buffer, "STATUS de ", 10) == 0) {
            // Affiche les messages de statut avec une couleur spécifique.
            wattron(messages_win, COLOR_PAIR(STATUS_MSG_COLOR_PAIR));
            wprintw(messages_win, "%s\n", buffer);
            wattroff(messages_win, COLOR_PAIR(STATUS_MSG_COLOR_PAIR));
        } else {
            // Affiche les autres messages avec une couleur par défaut pour les messages reçus.
            wattron(messages_win, COLOR_PAIR(RECEIVED_MSG_COLOR_PAIR));
            wprintw(messages_win, "%s\n", buffer);
            wattroff(messages_win, COLOR_PAIR(RECEIVED_MSG_COLOR_PAIR));
        }
    }
    // Rafraîchit la fenêtre des messages pour afficher les modifications.
    wrefresh(messages_win);
}

// Ferme l'application proprement, en libérant les ressources et en fermant la connexion réseau.
void close_application(int sock, WINDOW *input_win, WINDOW *messages_win) {
    // Ferme la fenêtre d'entrée si elle n'est pas NULL.
    if (input_win != NULL) {
        delwin(input_win);
    }
    // Ferme la fenêtre des messages si elle n'est pas NULL.
    if (messages_win != NULL) {
        delwin(messages_win);
    }
    // Ferme l'environnement ncurses.
    endwin();
    // Ferme le socket de connexion au serveur.
    close(sock);
    // Affiche un message de confirmation de fermeture dans le terminal.
    printf("Connection closed.\n");
}
