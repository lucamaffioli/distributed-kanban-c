
#include "../include/lavagna.h"

#define TIME_PING 90    // Secondi che il Thread attende dopo che una carta è stata messa in doing prima di inviare
                        // un eventuale PING all'utente se quasto non l'ha ancora eseguita
#define TIME_PONG 30    // Secondi che il Thread attende il PONG, dopo aver inviato il PING all'utente, prima di 
                        // rimettere la card in ToDo e rimuovere l'utente


// Funzioni di inizializzazione delle strutture

static int COUNTER_DASHBOARD = 1;   // Contatore per ID lavagna, nel caso di implementazione con più lavagne

// Inizializzazione della Lavagna
void init_dashboard(Dashboard *dashboard) {
    dashboard->ID = COUNTER_DASHBOARD++;
    dashboard->COUNTER_CARD = 1;
    dashboard->users_count = 0;
    dashboard->cards_count = 0;
    // Pulisco la memoria per evitare la presenza di dati che possono essere mal interpretati durante i controlli
    memset(dashboard->users, 0, sizeof(dashboard->users));  
    memset(dashboard->cards, 0, sizeof(dashboard->cards));
}


// Funzione che gestisce la move card implementando il workflow che prevede il passaggio da uno stato all'altro 
// nell'ordine dettato dall'enum State. Se user == NULL (nessun utente assegnato) lo stato deve tornare in ToDo, caso in     
// cui un'attività viene interrotta (per disconnessione utente). Altrimenti user ha il valore dell'utente.
// Non implementa controlli in quanto chimata solo effettuata dalla lavagna
void move_card(Card *card, User *user) {
    card->timestamp = time(NULL);   // Aggiorno timestamp
    card->ping_sent = 0;
    card->user = user;
    if (user == NULL) {
        card->owner_port = 0;
        card->state = ToDo;
    } else {
        card->owner_port = user->port;
        card->state++;
    }
    DBG("Card %d mossa in stato %d", card->ID, card->state);
}

// Funzione che manda a tutti gli utenti connessi il numero e la lista delle porte degli altri utenti
// Invocata ogni volta che viene rimosso un utente. Necessaria per consentire agli utenti di gestire i partecipati alle 
// aste e di aggiornarle nel caso un utente si sia disconnesso improvvisamente
void broadcast_user_list(Dashboard *dashboard) {
    // Per ogni utente attivo aggiungo al payload le porte di tutti gli altri e invio il messaggio
    for (int i = 0; i < MAX_USERS; i++) {
        User *user = &dashboard->users[i];
        if (user->active) {
            char payload[BUFFER_SIZE];
            int len = sprintf(payload, "%d", dashboard->users_count); // Numero di utenti attivi
            for (int j = 0; j < MAX_USERS; j++) {
                if (dashboard->users[j].active && &dashboard->users[j] != user) { // Per ogni altro utente attivo
                    len += sprintf(payload + len, " %d", dashboard->users[j].port); //Aggiungo la sua porta al messaggio
                }
            }
            // Invio SEND_USER_LIST all'utente
            DBG("Invio massaggio di SEND_USER_LIST all'utente %d: %s", user->port, payload);
            send_msg(dashboard->users[i].sock, SEND_USER_LIST, DASHBOARD_PORT, payload, len + 1); 
        }
    }
}

// Inizializzazione della Card, torna -1 in caso di errore 0 alrimenti.
int create_card(Dashboard *dashboard, char *desc) {
    //Inserimento nella lavagna se non ha raggiunto limite massimo
    if (dashboard->cards_count >= MAX_CARD) {
        return -1;
    } 
    for (int i = 0; i < MAX_CARD; i++) {
        Card *card = &dashboard->cards[i];
        if (card->ID == 0) {                            //Card libera
            dashboard->cards_count++;
            card->ID = dashboard->COUNTER_CARD++;
            strncpy(card->desc, desc, MAX_DESC - 1);    // Uso strn per copiare solo il massimo dei caratteri consentiti
                                                        // e non tutto il payload
            card->desc[MAX_DESC - 1] = '\0';            // Aggiungo terminatore di riga per evitare stampe scorrette
            card->user = NULL;                          // Quando viene creata la card è nella colonna TO DO, quindi 
                                                        // nessun utente la sta implementanto o l'ha implementata
            card->timestamp = time(NULL);               // Timestamp, momento della creazione della card
            card->state = ToDo;                         // La colonna della card alla creazione è sempre ToDo
            card->ping_sent = 0;                        // Ping non inviato  
            card->owner_port = 0;         
            return 0;     
        }
    }
    return -1; // Se il codice arriva qua c'è qualche inconsistenza
}


// Mostra a video la lavagna, chiama la funzione implementata nel file disegno.c
void show_dashboard(Dashboard *dashboard) {
    char buffer[MAX_DASHBOARD_STRING];
    draw_dashboard(buffer, dashboard);
    printf("%s", buffer);
}


// Manda a tutti gli utenti connessi un messaggio di AVAILABLE_CARD insieme all'ID della card, il numero degli utenti
// connessi e le loro porte
// Formato payload: "ID NUM_UTENTI PORT1 PORT2 ..."
void available_card(Dashboard *dashboard, int id) {
    // Per ogni utente aggiungo al buffer le porte di tutti gli altri e invio il messaggio.
    for (int i = 0; i < MAX_USERS; i++) {
        User *user = &dashboard->users[i];
        if (user->active) {
            char payload[BUFFER_SIZE];
            int len = sprintf(payload, "%d %d", id, dashboard->users_count);        // Id card e numero di utenti attivi
            for (int j = 0; j < MAX_USERS; j++) {
                if (dashboard->users[j].active && &dashboard->users[j] != user) {   // Per ogni altro utente attivo
                    len += sprintf(payload + len, " %d", dashboard->users[j].port); // Aggiungo la sua porta al messaggio
                }
            }
            // Invio AVAILABLE_CARD all'utente
            DBG("Invio massaggio di AVAILABLE_CARD all'utente %d: %s", user->port, payload);
            send_msg(dashboard->users[i].sock, AVAILABLE_CARD, DASHBOARD_PORT, payload, len + 1); 
        }
    }
}

// Prova ad assegnare una carta in ToDo mandando in broadcast a tutti gli utenti connessi un messaggio di AVAILABLE_CARD
// Controlla se è presente almeno una card in ToDo e se ci sono almeno 2 utenti attivi
void assign_card(Dashboard *dashboard) {
    if (dashboard->users_count > 1) {
        DBG("assign_card: sono presenti %d utenti", dashboard->users_count);
        for (int i = 0; i < dashboard->cards_count; i++) {
            if (dashboard->cards[i].state == ToDo) {
                available_card(dashboard, dashboard->cards[i].ID);
                return; // Gestisco un'asta alla volta
            }
        }
    } else {
        DBG("assign_card: non sono presenti abbastanza utenti");
    }
}

// Rimozione dell'utente dalla lavagna, con invio di SEND_USER_LIST a tutti gli utenti per informarli
// Chiamata all'interno della quit.
void remove_user(Dashboard *dashboard, User *user) {
    for (int i = 0; i < MAX_USERS; i++) {
        if (&dashboard->users[i] == user) {
            DBG("Utente con porta %d disconnesso. Invio lista utenti connessi aggiornata a tutti.", dashboard->users[i].port);

            dashboard->users[i].active = 0;
            dashboard->users[i].port = 0;
            dashboard->users[i].sock = 0;
            dashboard->users_count--;
            
            broadcast_user_list(dashboard);
        }
    }
}

// Quit dell'utente user
// Rimozione dalla lista degli utenti della lavagna, chiusura del socket, e reset delle Card in ToDo
void quit(Dashboard *dashboard, User *user) {
    // Reset di tutte le carte in DOING dell'utente
    int flag_doing = 0;
    for (int j = 0; j < MAX_CARD; j++) {
        if (dashboard->cards[j].user == user && dashboard->cards[j].state == Doing) {
            flag_doing = 1;
            move_card(&dashboard->cards[j], NULL);
        }
    }
    // Chiudo socket ed rimouvo utente
    close(user->sock);
    remove_user(dashboard, user);
    
    // Se almeno una carta è stata spostata da Doing a ToDo allora cerco di assegnare una carta
    // Fondamentale per casi limite
    if (flag_doing) {
        assign_card(dashboard);
    }
}


// Funzione per il controllo di tutti i timer e per l'eventuale invio del PING all'utente
// Nel main la select ha un timer impostato ad 1 secondo. Questo serve per mandare in esecuzione frequentemente questa funzione
// che si occupa di verificare il tempo di permanenza delle carte nella colonna Doing, inviare eventualmente
// il messaggio di PING all'utente e anche di controllare se, una volta mandato il ping, l'utente manda in tempo il PONG,
// altrimenti lo rimuove
// Tutto questo è fatto ogni secondo tramite il controllo del timestamp e del flag card->ping_sent
void timer(Dashboard *dashboard) {
    time_t now = time(NULL); // Ora corrente
    for (int i = 0; i < dashboard->cards_count; i++) {
        Card *card = &dashboard->cards[i];
        if (card->state == Doing) {

            double elapsed = difftime(now, card->timestamp); // Tempo trascorso dall'ultima modifica o dall'invio del ping

            if (!card->ping_sent && elapsed >= TIME_PING) {
                // La card è in doing da troppo tempo, necessario inviare PING all'utente per verificare la presenza
                DBG("Card %d invio del PING all'utente %d, passati %d secondi dall'ultimo timestamp", card->ID, card->user->port, (int)elapsed);
                send_msg(card->user->sock, PING_USER, DASHBOARD_PORT, NULL, 0);
                card->ping_sent = 1;
                card->timestamp = now; // Reset del timestamp per successivo Pong
            } else if (card->ping_sent && elapsed >= TIME_PONG) {
                // Il ping è stato inviato ed è passato troppo tempo senza ricevere il PONG dall'utente, quindi 
                // viene considerato disconnesso. Necessario riportare tutte le sue card da Doing a ToDo e rimuoverlo
                DBG("Card %d dell'utente %d, passati %d secondi dall'invio del ping. Pong non ricevuto, disconnessione", card->ID, card->user->port, (int)elapsed);
                quit(dashboard, card->user);
                show_dashboard(dashboard); 
            }
        }
    }
}

// Main

int main() {

    Dashboard dashboard;
    init_dashboard(&dashboard);

    // Aggiungo 10 Card per testing, diverse lunghezze per testare dinamicità del layout
    char *test_descriptions[] = {
        "Analisi e risoluzione del memory leak critico nel servizio di backend che causa crash ogni 24 ore",
        "Aggiornare README",
        "Implementare la dark mode per l'interfaccia utente mobile",
        "Migrazione completa del database legacy verso la nuova infrastruttura cloud distribuita per garantire alta disponibilita'",
        "Fix typo header",
        "Configurare pipeline CI/CD per deploy automatico",
        "Riscrivere la logica di validazione dei form lato client per supportare i nuovi requisiti di sicurezza internazionali",
        "Ottimizzare immagini PNG per ridurre tempo caricamento",
        "Bump version v2.1",
        "Eseguire audit di sicurezza sulle dipendenze esterne"
    };
    for(int i=0; i<10; i++) {
        char buffer[512];
        sprintf(buffer, "%s", test_descriptions[i]);
        create_card(&dashboard, buffer);
    }
    
    int server_fd, max_sd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    fd_set readfds;

    // Creazione del socket TCP
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        SYSERR("Creazione del socket fallita");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); // Necessario per poter riutilizzare la porta
                                                                        // senza aspettare

    // Assegnazione indirizzo e porta al socket
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(DASHBOARD_PORT);

    // Binding del socket
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        SYSERR("Bind fallita");
        exit(EXIT_FAILURE);
    }

    // Ascolta le connessioni
    if (listen(server_fd, 10) < 0) {
        SYSERR("Listen fallita");
        exit(EXIT_FAILURE);
    }

    INFO("Lavagna in ascolto sulla porta %d", DASHBOARD_PORT);

    // Se tutto è andato a buon fine, stampa situazione iniziale della lavagna
    show_dashboard(&dashboard);

    while(1) {

        // Pulisce i set
        FD_ZERO(&readfds);

        // Aggiunge il socket del server al set
        FD_SET(server_fd, &readfds);
        max_sd = server_fd;

        // Aggiunge socket degli utenti attivi al set
        for (int i = 0; i < MAX_USERS; i++) {
            if (dashboard.users[i].active) {
                int sd = dashboard.users[i].sock;
                FD_SET(sd, &readfds);
                if (sd > max_sd) {
                    max_sd = sd;
                }
            }
        }

        // Timout di un secondo per controllo delle carte in doing
        struct timeval timeout = {1, 0};

        // Select con timer
        int activity = select(max_sd + 1, &readfds, NULL, NULL, &timeout);

        // Gestione errore select
        if ((activity < 0) && (errno != EINTR)) {
            SYSERR("Select fallito");
            exit(EXIT_FAILURE);
        }

        // Gestione nuove connessioni utenti
        if (FD_ISSET(server_fd, &readfds)) {
            if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
                SYSERR("Accept fallito");
                exit(EXIT_FAILURE);
            }

            // Aggiungo agli utenti connessi alla lavagna solo se non ho raggiunto il massimo numero di utenti
            if (dashboard.users_count < MAX_USERS) {
                for (int i = 0; i < MAX_USERS; i++) {
                    if (!dashboard.users[i].active) {
                        dashboard.users[i].active = 1;
                        dashboard.users[i].sock = new_socket;
                        dashboard.users[i].port = 0;        // L'assegnamento della porta avverà alla ricezione del 
                                                            // messaggio di HELLO da parte dell'utente.
                        dashboard.users_count++;
                        DBG("Utente connesso all'indice %d", i);
                        break;
                    }
                }
            } else {
                // Massimo numero di utenti raggiunto, chiudo socket
                ERR("Massimo numero di utenti raggiunto.");
                close(new_socket);
            }
        }

        // Controlla se ci sono dati da uno dei client connessi
        for (int i = 0; i < MAX_USERS; i++) {
            User *user = &dashboard.users[i];
            if (user->active && FD_ISSET(user->sock, &readfds)) {
                MsgHeader msgHeader;
                if (recv_header(user->sock, &msgHeader) <= 0) {
                    // Disconnessione o Errore nella recv
                    ERR("Disconnessione dell'utente %d o errore sulla recv_header", user->port);
                    quit(&dashboard, user);
                    continue;
                }
                // Se non ci sono errori e il payload_len nel messaggio è > 0
                char payload[BUFFER_SIZE] = {0}; 
                if (msgHeader.payload_len > 0) {
                    if (recv_payload(user->sock, payload, msgHeader.payload_len) <= 0) {
                        // Disconnessione o Errore nella recv
                        ERR("Disconnessione dell'utente %d o errore sulla recv_payload", user->port);
                        quit(&dashboard, user);
                        continue;
                    }
                }

                int id;

                // Camandi dell'Utente
                switch(msgHeader.msg_type) {
                    case HELLO: // passato il numero di porta del payload
                        INFO("Rivevuto HELLO da utente %d ", msgHeader.sender_port);
                        user->port = msgHeader.sender_port; // Assegno la porta all'utente
                        DBG("DOPO HELLO: Dati utente: indice %d, port %d, sock %d", i, user->port, user->sock);
                        assign_card(&dashboard); // Essendo arrivato un nuovo utente tento di inviare un AVAILABLE_CARD
                        break;

                    case QUIT:  // Niente payload
                        INFO("Rivevuto QUIT da utente %d ", msgHeader.sender_port);
                        quit(&dashboard, user);
                        break;

                    case CREATE_CARD:   // Nel payload viene passato solo la descrizione della card
                        DBG("Rivevuto CREATE_CARD da utente %d ", msgHeader.sender_port);
                        if (create_card(&dashboard, payload) == 0) {
                            show_dashboard(&dashboard); //Aggiorno lavagna a video
                            assign_card(&dashboard);    // Arrivata una nuova card, tento di inviare un AVAILABLE_CARD
                        }
                        break;

                    case SHOW_LAVAGNA: // Niente payload
                        DBG("Rivevuto SHOW_LAVAGNA da utente %d ", msgHeader.sender_port);
                        char buffer[MAX_DASHBOARD_STRING];
                        draw_dashboard(buffer, &dashboard);
                        // Invio di DASHBOARD_VIEW iniseme alla lavagna al client
                        DBG("Invio DASHBOARD_VIEW all'utente %d ", msgHeader.sender_port);
                        send_msg(user->sock, DASHBOARD_VIEW, DASHBOARD_PORT, buffer, sizeof(buffer)+1); 
                        break;
                    
                    case ACK_CARD: // Payload: Id della card che l'utente deve eseguire
                        id = atoi(payload);
                        DBG("Rivevuto ACK_CARD %d da utente %d ", id, msgHeader.sender_port);
                        for (int i = 0; i < dashboard.cards_count; i++) {
                            if (dashboard.cards[i].ID == id) {
                                move_card(&dashboard.cards[i], user); // Move della card in doing
                            }
                        }
                        show_dashboard(&dashboard); // Aggiorno lavagna a video
                        break;

                    case CARD_DONE: // Payload: Id della carda appena eseguita dall'utente
                        id = atoi(payload);
                        DBG("Rivevuto CARD_DONE %d da utente %d ", id, msgHeader.sender_port);
                        for (int i = 0; i < dashboard.cards_count; i++) {
                            if (dashboard.cards[i].ID == id) {
                                move_card(&dashboard.cards[i], user);   // Move della card in Done
                            }
                        }
                        assign_card(&dashboard);    // Avendo l'utente finito una card tento di inviare un AVAILABLE_CARD
                        show_dashboard(&dashboard); // Aggiorno lavagna a video
                        break;

                    case PONG_LAVAGNA: // Nessun payload, il messaggio certifica solo che l'utente non si è disconnesso
                        // Non essendosi disconnesso, per tutte le card in doing deve essere rimesso a zero il flag
                        // ping_send e aggiornato il timestamp. In questo modo la funzione timer attenderà di nuovamente
                        // TIME_PING secondi prima di ricontrollare la presenza dell'utente
                        DBG("Rivevuto PONG_LAVAGNA da utente %d ", msgHeader.sender_port);
                        for (int i = 0; i < dashboard.cards_count; i++) {
                            if (dashboard.cards[i].user == user) {
                                dashboard.cards[i].ping_sent = 0;
                                dashboard.cards[i].timestamp = time(NULL);
                            }
                        }
                        break;

                    default: break;
                }

            }

        }

        // Gestione dei timer per tutte le card in Doing, eventuale Send del PING all'utente.
        timer(&dashboard);

    }
    exit(0);
}

