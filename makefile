# Makefile pour compiler les programmes serveur et client sur différents OS

# Compilateur à utiliser
CC = gcc

# Options de compilation
CFLAGS = -Wall -g

# Options de liaison
LDLIBS = -lncurses -lsqlite3 -lssl -lcrypto

# Dossier pour les exécutables
BIN_DIR = bin

# Créer le dossier bin si nécessaire
$(shell mkdir -p $(BIN_DIR))

# Marquer all, server, client, clean, update et deps comme cibles toujours obsolètes
.PHONY: all server client clean update deps

# Construire les programmes serveur et client par défaut
all: server client
	@echo "Compilation du serveur et du client faite avec succès."

# Règle pour mettre à jour la liste des paquets
update:
	@echo "Mise à jour de la liste des paquets..."
	@sudo apt-get update

# Règle pour installer les dépendances nécessaires
deps: update
	@echo "Vérification des dépendances pour ncurses..."
	@dpkg -s libncurses5-dev libncursesw5-dev >/dev/null 2>&1 || (echo "Installation des dépendances manquantes pour ncurses..."; sudo apt-get install libncurses5-dev libncursesw5-dev)
	@echo "Vérification des dépendances pour sqlite3..."
	@dpkg -s sqlite3 libsqlite3-dev >/dev/null 2>&1 || (echo "Installation des dépendances manquantes pour sqlite3..."; sudo apt-get install sqlite3 libsqlite3-dev)
	@echo "Vérification des dépendances pour openssl..."
	@dpkg -s openssl libssl-dev >/dev/null 2>&1 || (echo "Installation des dépendances manquantes pour openssl..."; sudo apt-get install openssl libssl-dev)
	@echo "Vérification des dépendances pour make..."
	@dpkg -s make >/dev/null 2>&1 || (echo "Installation des dépendances manquantes pour make..."; sudo apt-get install make)
	@echo "Vérification des dépendances pour gcc..."
	@dpkg -s gcc >/dev/null 2>&1 || (echo "Installation des dépendances manquantes pour gcc..."; sudo apt-get install gcc)

# Règle pour construire le serveur
server: deps
	$(CC) $(CFLAGS) server.c $(LDLIBS) -o $(BIN_DIR)/server -Wno-deprecated-declarations

# Règle pour construire le client
client: deps
	$(CC) $(CFLAGS) client.c $(LDLIBS) -o $(BIN_DIR)/client -Wno-deprecated-declarations

# Règle pour nettoyer les fichiers compilés
clean:
	rm -f $(BIN_DIR)/server $(BIN_DIR)/client
