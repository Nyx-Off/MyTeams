# Makefile pour compiler les programmes serveur et client sur différents OS

# Détecter l'OS
UNAME_S := $(shell uname -s)

# Compilateur à utiliser
CC = gcc

# Options de compilation
CFLAGS = -Wall -g

# Options de linkage
LDLIBS = -lncurses

# Dossier pour les exécutables
BIN_DIR = bin

# Commande de nettoyage selon l'OS
ifeq ($(UNAME_S),Linux)
    RM = rm -f
    INSTALL = sudo apt-get install
else ifeq ($(UNAME_S),Darwin) # Ajout pour macOS
    RM = rm -f
    # Ajoutez ici la commande d'installation pour macOS si nécessaire
endif

# Créer le dossier bin si nécessaire
$(shell mkdir -p $(BIN_DIR))

# Marquer all, server, client, clean et deps comme cibles toujours obsolètes
.PHONY: all server client clean deps

# Construire les programmes serveur et client par défaut après avoir installé les dépendances
all: deps server client
	@echo "Compilation du Server et du Client faite avec succès."

# Règle pour installer les dépendances
deps:
	@echo "Vérification des dépendances..."
	@dpkg -s libncurses5-dev libncursesw5-dev >/dev/null 2>&1 || (echo "Installation des dépendances manquantes..."; $(INSTALL) libncurses5-dev libncursesw5-dev)

# Règle pour construire le serveur
server:
	$(CC) $(CFLAGS) server.c -o $(BIN_DIR)/server

# Règle pour construire le client
client:
	$(CC) $(CFLAGS) client.c $(LDLIBS) -o $(BIN_DIR)/client

# Règle pour nettoyer les fichiers compilés
clean:
	$(RM) $(BIN_DIR)/server $(BIN_DIR)/client
