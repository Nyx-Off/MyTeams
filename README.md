# EchoNet : Système de Communication TCP

## Vue d'ensemble
EchoNet est un système de communication client-serveur TCP simple, développé dans le cadre d'un projet de cours par moi. L'objectif principal de ce projet est de démontrer les principes de base de la programmation réseau, y compris la programmation de sockets, la gestion de multiples connexions clients à l'aide de `select()` et la gestion de processus avec `fork()`. Le système permet aux clients de se connecter au serveur, d'envoyer des messages et de les voir écho, illustrant ainsi un échange de données en temps réel sur TCP.

## Fonctionnalités
- **Serveur TCP** : Écoute les connexions clients entrantes sur un port spécifié et gère plusieurs clients simultanément.
- **Client TCP** : Se connecte au serveur TCP et envoie des messages saisis par l'utilisateur. Fonctionne en continu pour permettre l'envoi de plusieurs messages lors d'une session.
- **Gestion Multi-Clients** : Utilise `select()` pour gérer plusieurs sockets clients et `fork()` pour créer un nouveau processus pour chaque connexion client, assurant une gestion isolée des requêtes clients.

## Pour Commencer

### Prérequis
- Un système d'exploitation Linux ou de type UNIX
- Le compilateur GCC

### Compilation
Pour compiler les programmes serveur et client, utilisez les commandes suivantes dans votre terminal :

```bash
gcc -o serveur serveur.c
gcc -o client client.c
```

### Démarrage du Serveur
Lancez le serveur en spécifiant le numéro de port sur lequel il doit écouter les connexions entrantes :

```bash
./serveur 4242
```

### Démarrage du Client
Démarrez le client en spécifiant l'adresse IP du serveur et le numéro de port :

```bash
./client 127.0.0.1 4242
```

## Contexte du Projet
Ce projet est principalement un exercice académique, faisant partie d'un cours sur le réseau, visant à comprendre le fonctionnement des protocoles TCP/IP, l'architecture client-serveur et la gestion concurrente du serveur. Il sert d'approche pratique pour apprendre la programmation réseau et comprendre les défis et les solutions dans la gestion de multiples connexions clients dans un environnement réseau.

## Limitations
- Ce projet est une représentation simpliste du modèle client-serveur et est destiné à des fins éducatives.
- Le serveur gère chaque client dans un processus séparé, ce qui pourrait ne pas être la méthode la plus efficace pour un grand nombre de clients concurrents.

## Licence
Ce projet est mis à disposition sous la Licence MIT. Consultez le fichier LICENSE pour plus de détails.

