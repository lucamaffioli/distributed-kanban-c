#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <sys/select.h>
#include <sys/time.h> 


// Per stampe di debug ed errori

// Colori
#define C_RED     "\x1b[31m"  // Per Errori
#define C_GREEN   "\x1b[32m"  // Per info
#define C_YELLOW  "\x1b[33m"  // Per Debug
#define C_RESET   "\x1b[0m"   // Per reset colori

// Messaggi sempre visibili: info e errori anche di sistema

// INFO: Messaggi di stato normali. 
#define INFO(fmt, ...) fprintf(stdout, C_GREEN "[INFO] " fmt C_RESET "\n", ##__VA_ARGS__)

// ERR: Errori Logici dell'applicazione.
#define ERR(fmt, ...) fprintf(stderr, C_RED "[ERROR] %s:%d: " fmt C_RESET "\n", __FILE__, __LINE__, ##__VA_ARGS__)

// SYSERR: Errori di Sistema (che usano errno).
#define SYSERR(fmt, ...) fprintf(stderr, C_RED "[SYSERR] %s:%d: " fmt ": %s\n" C_RESET, __FILE__, __LINE__, ##__VA_ARGS__, strerror(errno))

// Messaggi condizionali (Visibili solo con opzione -DDEBUG nel Makefile)
#ifdef DEBUG
    // DBG: Per tracciare il flusso del codice.
    // Viene stampata anche l'orario così da poter tenere traccia dell'ordine dell'esecuzione.

    #define DBG(fmt, ...) do { \
        struct timeval _t; \
        gettimeofday(&_t, NULL); \
        struct tm _tm; \
        localtime_r(&_t.tv_sec, &_tm); \
        fprintf(stdout, C_YELLOW "[DBG %02d:%02d:%02d.%03ld] %s:%d: " fmt C_RESET "\n", \
            _tm.tm_hour, _tm.tm_min, _tm.tm_sec, _t.tv_usec / 1000, \
            __FILE__, __LINE__, ##__VA_ARGS__); \
    } while(0)

#else
    // Se non c'è -DDEBUG, queste righe vengono cancellate dal compilatore
    #define DBG(fmt, ...) do {} while (0)
#endif


#define DASHBOARD_ADDR "127.0.0.1"
#define DASHBOARD_PORT 5678 // Porta della lavagna
#define BUFFER_SIZE 4096    // Dimensione massima del buffer usato per comandi da riga di comando
#define MAX_DESC 128        // Dimensione massima per la descrizione di una Card
#define MAX_USERS 10        // Numero massimo di utenti collagati contemporaneamente e quindi di peers per l'utente
#define MAX_CARD 50         // Numero massimo di card 

#define MAX_DASHBOARD_STRING 10000  // Massima dimensione della lavagna, usato anche per l'invio di questa 

// Tipi di messaggi
typedef enum {
    HELLO,              // Utente -> Lavagna (Registrazione)
    QUIT,               // Utente -> Lavagna (Uscita utente)
    CREATE_CARD,        // Utente -> Lavagna (Creazione card)
    SHOW_LAVAGNA,       // Utente -> Lavagna (Richiesta layout lavagna)
    AVAILABLE_CARD,     // Lavagna -> Utente (Carta disponibile)
    CHOOSE_USER,        // Utente -> Utente  (Invio costo personale)
    ACK_CARD,           // Utente -> Lavagna (Asta vinta)
    CARD_DONE,          // Utente -> Lavagna (Lavoro finito)
    PING_USER,          // Lavagna -> Utente (Messaggio per capire se l'utente è connesso)
    PONG_LAVAGNA,       // Utente -> Lavagna (Risposta al PING)
    DASHBOARD_VIEW,     // Lavagna -> Utente (Risposta a SHOW_LAVAGNA)
    SEND_USER_LIST      // Lavagna -> Utente (Invio lista utenti attivi)
} MsgType;

// Struttura fissa del Header del messaggio
// L'attributo "__attribute__((packed))" serve per dire al compilatore di non aggiungere nessun bit di spazio tra i
// campi della struttura. Questo serve perchè se l'applicazione fosse distribuita su calcolatori diversi, i diversi 
// compilatori potrebbero gestire in maniera diversa il padding e lo scambio di messaggi non andrebbe a buon fine.
typedef struct {
    MsgType msg_type;   // Tipo del messaggio 
    int sender_port;    // Porta del mittente
    int payload_len;    // Lunghezza dati nel payload (dipende dal formato del messaggio)
} __attribute__((packed)) MsgHeader; 


// Funzioni per inivio e ricezione di messaggi (più dettagli in src/common.c)

// Funzione per invio di un messaggio 
int send_msg(int sd, MsgType msg_type, int sender_port, const void *payload, int payload_len);

// Funzione per ricere un MsgHeader
int recv_header(int sd, MsgHeader *msgHeader);

// Funzione per ricevere il payload, viene chiamata dopo la recv_header se il campo payload_len è > 0.
int recv_payload(int sd, void *buffer, int len);

#endif