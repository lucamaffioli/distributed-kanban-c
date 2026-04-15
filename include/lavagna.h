#ifndef LAVAGNA_H
#define LAVAGNA_H

#include "common.h" 

// Valori possibili per una colonna e quindi per lo stato di una Card
typedef enum { 
    ToDo,
    Doing,
    Done
} State;

// Struttura Utente
typedef struct {
    int port;       // Porta dell'utente
    int sock;       // Socket utente
    int active;     // 1 se slot occupato, 0 se libero
} User;

// Struttura Card
typedef struct {
    int ID;                 // Identificativo della card
    State state;            // Colonna in cui si trova la card
    char desc[MAX_DESC];    // Descrizione dell'attività
    
    User *user;             // Utente che sta facendo o ha fatto l'attività (non significativa con colonna = ToDo)
    int owner_port;         // Porta dell'utente (Necessaria una copia per mantenerla anche se la carta è in Done e
                            // l'utente si è disconesso causando user = NULL)
    time_t timestamp;       // Ultima modifica, compreso l'arrivo del PONG da parte dell'utente
    int ping_sent;          // Flag per gestione timer, vale 1 se è già stato inviato il Ping all'utente, 0 altrimenti
} Card;


// Struttura Lavagna
typedef struct {
    int ID;                         // ID della lavagna
    User users[MAX_USERS];          // Array degli utenti connessi;
    int users_count;                // Numero utenti presenti
    Card cards[MAX_CARD];           // Array delle Card contenute nella lavagna;
    int cards_count;                // Numero di card presenti;
    int COUNTER_CARD;               // Contatore per ID Card, l'id viene assegnato automaticamente in modo incrementale 
                                    // dalla lavagna
} Dashboard;


// Definizione della funzione implementata in src/disegno.c
void draw_dashboard(char *buffer, Dashboard *lavagna);

#endif