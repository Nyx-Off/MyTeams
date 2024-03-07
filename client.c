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
    if (argc != 5) {
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

void send_credentials(int sock, char *identifiant, char *mdp) {
    char hashedPassword[65];
    hash_password(mdp, hashedPassword); 

    char credentials[1024]; 
    sprintf(credentials, "%s %s", identifiant, hashedPassword); 
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
    init_pair(ADMIN_MSG_COLOR_PAIR, COLOR_RED, COLOR_BLACK); 
    init_pair(STATUS_MSG_COLOR_PAIR, COLOR_MAGENTA, COLOR_BLACK);

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

    }else if (strncmp(input_buffer, "/status ", 7) == 0) {
        send(sock, input_buffer, strlen(input_buffer), 0);
    }else if (strlen(input_buffer) > 0) { 
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
        wprintw(messages_win, "Server disconnected. Press any key to exit.\n");
        close_application(sock, NULL, messages_win);
        exit(0);
    } else {
        if (strncmp(buffer, "[ADMIN]: ", 9) == 0) {
            wattron(messages_win, COLOR_PAIR(ADMIN_MSG_COLOR_PAIR));
            wprintw(messages_win, "%s\n", buffer); 
            wattroff(messages_win, COLOR_PAIR(ADMIN_MSG_COLOR_PAIR));
        } else if (strncmp(buffer, "STATUS de ", 10) == 0) {
            wattron(messages_win, COLOR_PAIR(STATUS_MSG_COLOR_PAIR));
            wprintw(messages_win, "%s\n", buffer);
            wattroff(messages_win, COLOR_PAIR(STATUS_MSG_COLOR_PAIR));
        } else {
            wattron(messages_win, COLOR_PAIR(RECEIVED_MSG_COLOR_PAIR));
            wprintw(messages_win, "%s\n", buffer);
            wattroff(messages_win, COLOR_PAIR(RECEIVED_MSG_COLOR_PAIR));
        }
    }
    wrefresh(messages_win);
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