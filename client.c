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

void initialize_ncurses();
void setup_color_pairs();
void init_connection(int *sock, struct sockaddr_in *serverAddr, char *serverAddress, int port);
void send_pseudo(int sock, char *pseudo);
void handle_user_input(WINDOW *input_win, WINDOW *messages_win, int sock);
void handle_server_message(WINDOW *messages_win, int sock);
void close_application(int sock, WINDOW *input_win, WINDOW *messages_win);
void send_credentials(int sock, char *identifiant, char *mdp);


int main(int argc, char *argv[]) {
    if (argc != 5) { // Modifier ici pour accepter l'identifiant et le mdp comme arguments supplémentaires
        fprintf(stderr, "Usage: %s <Server Address> <Port> <Identifiant> <MotDePasse>\n", argv[0]);
        return 1;
    }

    initialize_ncurses();
    setup_color_pairs();

    char *serverAddress = argv[1];
    int port = atoi(argv[2]);
    char *identifiant = argv[3];
    char *mdp = argv[4];
    int sock;
    struct sockaddr_in serverAddr;

    init_connection(&sock, &serverAddr, serverAddress, port);

    // Envoyer l'identifiant et le mdp au serveur avant le pseudo
    send_credentials(sock, identifiant, mdp);

    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    WINDOW *messages_win = newwin(max_y - 1, max_x, 0, 0);
    WINDOW *input_win = newwin(1, max_x, max_y - 1, 0);
    scrollok(messages_win, TRUE);

    wprintw(messages_win, "Connected to server %s:%d as %s\n", serverAddress, port, identifiant);
    wrefresh(messages_win);
    wrefresh(input_win);

    fd_set read_fds;
    int fd_max = sock;

    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(sock, &read_fds);

        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;

        if (select(fd_max + 1, &read_fds, NULL, NULL, &tv) == -1) {
            perror("select");
            break;
        }

        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            handle_user_input(input_win, messages_win, sock);
        }

        if (FD_ISSET(sock, &read_fds)) {
            handle_server_message(messages_win, sock);
        }
    }

    close_application(sock, input_win, messages_win);
    return 0;
}


void send_credentials(int sock, char *identifiant, char *mdp) {
    // Concaténer identifiant et mdp séparés par un espace ou un autre séparateur
    char credentials[1024]; // Assurez-vous que cela est suffisamment grand
    sprintf(credentials, "%s %s", identifiant, mdp);
    if (send(sock, credentials, strlen(credentials), 0) < 0) {
        perror("send credentials");
        close(sock);
        endwin();
        exit(1);
    }
}

void initialize_ncurses() {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    start_color();
}

void setup_color_pairs() {
    init_pair(RECEIVED_MSG_COLOR_PAIR, COLOR_CYAN, COLOR_BLACK);
    init_pair(SENT_MSG_COLOR_PAIR, COLOR_GREEN, COLOR_BLACK);
}

void init_connection(int *sock, struct sockaddr_in *serverAddr, char *serverAddress, int port) {
    *sock = socket(AF_INET, SOCK_STREAM, 0);
    if (*sock < 0) {
        perror("socket");
        endwin();
        exit(1);
    }

    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(*sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
    setsockopt(*sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof tv);

    memset(serverAddr, 0, sizeof(*serverAddr));
    serverAddr->sin_family = AF_INET;
    serverAddr->sin_port = htons(port);
    if (inet_pton(AF_INET, serverAddress, &serverAddr->sin_addr) <= 0) {
        perror("inet_pton");
        close(*sock);
        endwin();
        exit(1);
    }

    if (connect(*sock, (struct sockaddr *)serverAddr, sizeof(*serverAddr)) < 0) {
        perror("connect");
        close(*sock);
        endwin();
        exit(1);
    }
}

void send_pseudo(int sock, char *pseudo) {
    if (send(sock, pseudo, strlen(pseudo), 0) < 0) {
        perror("send pseudo");
        close(sock);
        endwin();
        exit(1);
    }
}

void handle_user_input(WINDOW *input_win, WINDOW *messages_win, int sock) {
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

    if (strcmp(input_buffer, "/exit") == 0) {
        close_application(sock, input_win, messages_win);
        exit(0);
    }
    if (strcmp(input_buffer, "/who") == 0) {
        send(sock, "/who", strlen("/who"), 0); // Envoyer la commande spéciale au serveur
    } 
    if (strcmp(input_buffer, "/info") == 0) {
        send(sock, "/server_info", strlen("/server_info"), 0); // Envoyer la commande spéciale au serveur
    }
    if (strcmp(input_buffer, "/help") || strcmp(input_buffer, "/?")) {
        send(sock, "/help", strlen("/help"), 0); // Envoyer la commande spéciale au serveur
    }
    else if (strlen(input_buffer) > 0) { 
        wattron(messages_win, COLOR_PAIR(SENT_MSG_COLOR_PAIR));
        wprintw(messages_win, "> %s\n", input_buffer);
        wattroff(messages_win, COLOR_PAIR(SENT_MSG_COLOR_PAIR));
        wrefresh(messages_win);
        send(sock, input_buffer, strlen(input_buffer), 0);
    }

    werase(input_win);
    wrefresh(input_win);
}

void handle_server_message(WINDOW *messages_win, int sock) {
    char buffer[BUFFER_SIZE] = {0};
    ssize_t len = recv(sock, buffer, BUFFER_SIZE - 1, 0);

    if (len <= 0) {
        wprintw(messages_win, "Server disconnected.\n");
        wrefresh(messages_win);
        close_application(sock, NULL, messages_win);
        exit(0);
    } else {
        wattron(messages_win, COLOR_PAIR(RECEIVED_MSG_COLOR_PAIR));
        wprintw(messages_win, "%s\n", buffer);
        wattroff(messages_win, COLOR_PAIR(RECEIVED_MSG_COLOR_PAIR));
        wrefresh(messages_win);
    }
}

void close_application(int sock, WINDOW *input_win, WINDOW *messages_win) {
    if (input_win != NULL) {
        delwin(input_win);
    }
    if (messages_win != NULL) {
        delwin(messages_win);
    }
    endwin();
    close(sock);
    printf("Connection closed.\n");
}
