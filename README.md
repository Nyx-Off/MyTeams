# EchoNet : Système de Communication TCP

## Vue d'ensemble
EchoNet est un système de communication client-serveur TCP qui illustre les principes de base de la programmation réseau. Ce projet permet aux clients de se connecter à un serveur, d'envoyer des messages qui sont ensuite diffusés à tous les clients connectés, démontrant ainsi un échange de données en temps réel via TCP.

## Fonctionnalités
- **Serveur TCP** : Gère les connexions clients entrantes sur un port spécifié et prend en charge plusieurs clients simultanément.
- **Client TCP** : Se connecte au serveur et permet à l'utilisateur d'envoyer des messages. Les messages sont reçus de tous les clients connectés, permettant ainsi une communication interactive.
- **Gestion Multi-Clients** : Utilise `select()` pour gérer de multiples connexions clients de manière non bloquante, sans recourir à `fork()`, ce qui améliore l'efficacité du serveur.

## Pour Commencer

### Prérequis
- Un système d'exploitation Linux, macOS ou compatible UNIX.
- Le compilateur GCC pour la compilation des programmes.
- Make pour automatiser la compilation avec le `Makefile` fourni.

### Compilation
Un `Makefile` est fourni pour faciliter la compilation des programmes serveur et client. Suivez ces étapes pour compiler :

1. Ouvrez un terminal dans le répertoire du projet.
2. Exécutez la commande suivante pour compiler le serveur et le client :
   ```bash
   make
   ```
   Les exécutables seront placés dans le dossier `bin/`.

### Démarrage du Serveur
Pour lancer le serveur, exécutez :
```bash
./bin/serveur <port>
```
Remplacez `<port>` par le numéro de port sur lequel le serveur doit écouter.

### Démarrage du Client
Pour démarrer un client, exécutez :
```bash
./bin/client <adresse_serveur> <port> <pseudo>
```
Remplacez `<adresse_serveur>` par l'adresse IP du serveur, `<port>` par le numéro de port, et `<pseudo>` par le nom d'utilisateur souhaité.

## Contexte du Projet
Ce projet a été développé dans un contexte académique pour approfondir la compréhension de la programmation réseau, l'architecture client-serveur et les protocoles TCP/IP. Il vise à offrir une expérience pratique de la gestion de connexions réseau simultanées dans un environnement de serveur.

## Limitations
Bien que fonctionnel, EchoNet est un projet à visée éducative et pourrait ne pas être adapté à un usage en production sans modifications supplémentaires.

## Licence
EchoNet est mis à disposition sous Licence MIT. Pour plus de détails, consultez le fichier `LICENSE`.
