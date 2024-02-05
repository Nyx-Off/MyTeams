# Makefile pour compiler les programmes serveur et client sur différents OS

# Compilateur à utiliser
CC = gcc

# Options de compilation
CFLAGS = -Wall -g

# Options de liaison
LDLIBS = -lncurses

# Dossier pour les exécutables
BIN_DIR = bin

# Créer le dossier bin si nécessaire
$(shell mkdir -p $(BIN_DIR))

# Marquer all, server, client, clean et deps comme cibles toujours obsolètes
.PHONY: all server client clean deps

# Construire les programmes serveur et client par défaut
all: server client
	@echo "Compilation du serveur et du client faite avec succès."

# Règle pour construire le serveur
server: deps
	$(CC) $(CFLAGS) server.c $(LDLIBS) -o $(BIN_DIR)/server

# Règle pour construire le client
client: deps
	$(CC) $(CFLAGS) client.c $(LDLIBS) -o $(BIN_DIR)/client

# Règle pour nettoyer les fichiers compilés
clean:
	rm -f $(BIN_DIR)/server $(BIN_DIR)/client

# Règle pour installer les dépendances nécessaires
deps:
	@echo "Vérification des dépendances pour ncurses..."
	@dpkg -s libncurses5-dev libncursesw5-dev >/dev/null 2>&1 || (echo "Installation des dépendances manquantes pour ncurses..."; sudo apt-get install libncurses5-dev libncursesw5-dev)
