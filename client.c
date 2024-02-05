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

#define BUFFER_SIZE 1024

#define RECEIVED_MSG_COLOR_PAIR 1
#define SENT_MSG_COLOR_PAIR 2

void init_connection(int *sock, struct sockaddr_in *serverAddr, char *serverAddress, int port);
void send_pseudo(int sock, char *pseudo);
void close_connection(int sock);

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <Server Address> <Port> <Pseudo>\n", argv[0]);
        return 1;
    }

    char *serverAddress = argv[1];
    int port = atoi(argv[2]);
    char *pseudo = argv[3];
    int sock;
    struct sockaddr_in serverAddr;

    initscr();
    cbreak();
    start_color();
    noecho();
    keypad(stdscr, TRUE);

    init_pair(RECEIVED_MSG_COLOR_PAIR, COLOR_CYAN, COLOR_BLACK); 
    init_pair(SENT_MSG_COLOR_PAIR, COLOR_GREEN, COLOR_BLACK); 

    init_connection(&sock, &serverAddr, serverAddress, port);
    send_pseudo(sock, pseudo);

    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    WINDOW *messages_win = newwin(max_y - 1, max_x, 0, 0);
    WINDOW *input_win = newwin(1, max_x, max_y - 1, 0);
    scrollok(messages_win, TRUE);

    wprintw(messages_win, "Connecté au serveur %s:%d en tant que %s\n", serverAddress, port, pseudo);
    wrefresh(messages_win);
    wrefresh(input_win);

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
            char input_buffer[BUFFER_SIZE] = {0};
            int input_pos = 0;
            werase(input_win);
            wrefresh(input_win);

            int ch;
            while ((ch = wgetch(input_win)) != '\n' && input_pos < BUFFER_SIZE - 1) {
                if (ch == KEY_BACKSPACE || ch == 127) {
                    if (input_pos > 0) {
                        input_pos--;
                        wmove(input_win, 0, input_pos);
                        wdelch(input_win);
                    }
                } else {
                    input_buffer[input_pos++] = (char)ch;
                    waddch(input_win, ch);
                }
            }
            input_buffer[input_pos] = '\0';

            // Vérifiez si le message est vide ou ne contient que des espaces blancs
            int is_empty = 1;
            for (int i = 0; i < input_pos; ++i) {
                if (!isspace((unsigned char)input_buffer[i])) {
                    is_empty = 0;
                    break;
                }
            }

            if (!is_empty) {
                if (strcmp(input_buffer, "/exit") == 0) {
                    break; // Sortie si "/exit" est entré
                } else {
                    wattron(messages_win, COLOR_PAIR(SENT_MSG_COLOR_PAIR)); // Activer la couleur pour les messages envoyés
                    wprintw(messages_win, "> %s\n", input_buffer);
                    wattroff(messages_win, COLOR_PAIR(SENT_MSG_COLOR_PAIR)); // Désactiver la couleur
                    wrefresh(messages_win);
                    send(sock, input_buffer, strlen(input_buffer), 0);
                }
            }
            werase(input_win); // Efface la fenêtre d'entrée après la vérification
            wrefresh(input_win);
        }

        if (FD_ISSET(sock, &read_fds)) {
            char buffer[BUFFER_SIZE] = {0};
            ssize_t len = recv(sock, buffer, BUFFER_SIZE - 1, 0);

            if (len <= 0) {
                wprintw(messages_win, "Déconnexion du serveur.\n");
                break;
            } else {
                wattron(messages_win, COLOR_PAIR(RECEIVED_MSG_COLOR_PAIR)); // Activer la couleur pour les messages reçus
                wprintw(messages_win, "%s\n", buffer);
                wattroff(messages_win, COLOR_PAIR(RECEIVED_MSG_COLOR_PAIR)); // Désactiver la couleur
                wrefresh(messages_win);
            }
        }
    }

    close_connection(sock);
    delwin(input_win);
    delwin(messages_win);
    endwin();
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

void close_connection(int sock) {
    close(sock);
}
