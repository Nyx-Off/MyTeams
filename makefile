# Makefile pour compiler les programmes serveur et client sur différents OS

# Détecter l'OS
UNAME_S := $(shell uname -s)

# Compilateur à utiliser
CC = gcc

# Options de compilation
CFLAGS = -Wall -g

# Commande de nettoyage selon l'OS
ifeq ($(UNAME_S),Linux)
    RM = rm -f
endif

# Marquer all, server, client, et clean comme cibles toujours obsolètes
.PHONY: all server client clean

# Construire les programmes serveur et client par défaut et afficher un message
all: server client
	@echo "Compilation du Server et du Client faite avec succès."

# Règle pour construire le serveur
server:
	$(CC) $(CFLAGS) server.c -o server

# Règle pour construire le client
client:
	$(CC) $(CFLAGS) client.c -o client

# Règle pour nettoyer les fichiers compilés
clean:
	$(RM) server client
