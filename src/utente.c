#include "../include/common.h"
#include <pthread.h>

#define MAX_CONCURRENT_AUCTIONS 10   // Numero massimo di aste che è possibile gestire in contemporanea

// Variabili globali 
int port;               // Porta dell'utente
int dashboard_sd;       // Socket per comunicazione con la Lavagna
int p2p_listener;       // Socket in ascolto per gli altri utenti

// Pipe globale, per notifica di CARD_DONE da worker a main Thread
int int_pipe[2]; // [0]=Read, [1]=Write

// Struttura per la gestione dell'asta tra utenti per l'assegnazione della Card
typedef struct {
    int active;                     // Vale 1 se lo slot è occupato, 0 altrimenti
    int id_card;                    // Id della Card su cui si sta eseguendo l'asta
    int my_cost;                    // Costo dell'utente per eseguire la Card (generato casualmente)
    int costs_received;             // Numero di costi ricevuti dagli altri utenti
    int expected_costs;             // Numero di costi da ricevere per proseguire (num_user - 1)

    int participants[MAX_USERS];    // Lista delle porte degli utenti che devono partecipare a questa asta
    int costs[MAX_USERS];           // Lista dei costi associati algli utenti partecipanti (0 se l'utente non ha ancora inviato il costo
                                    // o __MAX_INT__ se l'utente si è disconnesso durante l'asta

    time_t creation_time;           // Necessario per rimozione di aste create in maniera incompleta a cause dell'arrivo
                                    // di un costo già disconnesso (arrivo in ritardo)
} Auction;

// Array per la gestione di più aste in contemporanea
Auction auctions[MAX_CONCURRENT_AUCTIONS];

// Prompt di input 
void prompt() {
    printf("%d> ", port);
    fflush(stdout);
}

// Funzioni di utilità per gestione aste

void init_auctions() {
    memset(auctions, 0, sizeof(auctions));  // Pulizia della memoria
}

// Trova asta attiva tramite ID della Card
Auction* get_auction_by_ID(int id_card) {
    for (int i = 0; i < MAX_CONCURRENT_AUCTIONS; i++) {
        if (auctions[i].active && auctions[i].id_card == id_card) {
            return &auctions[i];
        }
    }
    return NULL;
}

// Trova slot per nuova asta nell'array delle aste
Auction* get_free_auction_slot() {
    for (int i = 0; i < MAX_CONCURRENT_AUCTIONS; i++) {
        if (!auctions[i].active) {
            return &auctions[i];
        }
    }
    return NULL;
}


// Thread worker per eseguire la Card. Quando termina l'esecuzione (esce dalla sleep) 
// segnala al Thread principale tramite una pipe che ha finito. Questo consente di racchiudere tutta la 
// gestione della comunicazione solo sul main e non dover implementare semafori di mutua esclusione sul socket visto 
// che non è direttamente il Thread worker ad inviare il CARD_DONE alla lavagna
void *worker_func(void *arg) {
    int id_card = *(int*)arg;       // Casting per avere id card
    free(arg);                      // Libera la memoria allocata dal main per l'argomento passato alla funzione

    // Simulazione dell'esecuzione della Card tramite una sleep di durata casuale
    int time = 5 + (rand() % 10);
    DBG("Inizio lavoro su card %d", id_card);
    sleep(time);
    
    // Quando esco dalla sleep voul dire che il Thread worker ha concluso l'esecuzione della Card
    // Va notificato il main che si occuperà dell'invio del CARD_DONE alla lavagna:
    // Scrittura dell'id della Card nella pipe sul canale di scrittura. Questa operazione è thread-safe essendo
    // la scrittura sulla pipe atomica per dati piccoli come un intero: non c'è concorrenza da Thread worker diversi
    // La scrittura verrà intercettata dalla select nel main
    write(int_pipe[1], &id_card, sizeof(id_card));
    DBG("Fine lavoro su card %d", id_card);
    pthread_exit(NULL);
}

// Creazione socket e connect
void connect_to_dashboard() {
    //Creazione socket per comunicazione con la lavagna
    if ((dashboard_sd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        SYSERR("Errore durante la creazione del socket");
        exit(EXIT_FAILURE); 
    }
    //Creazione e impostazione dell'indirizzo della lavagna
    struct sockaddr_in dashboard_addr;

    memset(&dashboard_addr, 0, sizeof(dashboard_addr));
    dashboard_addr.sin_family = AF_INET;
    dashboard_addr.sin_port = htons(DASHBOARD_PORT);

    if(inet_pton(AF_INET, DASHBOARD_ADDR, &dashboard_addr.sin_addr) <= 0) {
        SYSERR("Errore durante la conversione dell'indirizzo ip");
        close(dashboard_sd);
        exit(EXIT_FAILURE); 
    }

    //Connessione alla lavagna
    if(connect(dashboard_sd, (struct sockaddr*)&dashboard_addr, sizeof(dashboard_addr)) == -1) {
        SYSERR("Errore durante la connesione alla lavagna");
        close(dashboard_sd);
        exit(EXIT_FAILURE);
    }
}

// Gestione della comunicazione peer-to-peer tra utenti per lo scambio dei costi e la gestione dell'asta.

// Creazione Socket e ascolto per comunicazione P2P.
void start_p2p_listener() {
    struct sockaddr_in addr;
    p2p_listener = socket(AF_INET, SOCK_STREAM, 0);
    
    // Opzione per poter riavviare subito il client sulla stessa porta
    int opt = 1;
    setsockopt(p2p_listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(p2p_listener, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Errore Bind P2P");
        exit(EXIT_FAILURE);
    }
    listen(p2p_listener, 10);
}

// Funzione per inviare il costo ad un'altro utente
// Apro una connessione TCP di preve durata che viene chiusa una volta inviato il singolo costo.
// La strategia di sfruttare connessioni TCP brevi consente di sfruttare TCP come protocollo affidabile senza doverne implementare 
// uno a livello applicazione e senza la necessità di gestire connessioni permanenti con gli utenti.
// Questo semplifica l'implementazione considerando anche il tipo di applicazione che non risente di piccoli overhead 
// dati da handshake e chiusura del TCP. 
void send_cost(int target_port, int card_id, Auction *auction) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1"); // Indirizzo utente target (localhost)
    addr.sin_port = htons(target_port);
    
    if(connect(sock, (struct sockaddr*)&addr, sizeof(addr)) >= 0) {
        char buf[64];
        // Payload: "ID_CARD COSTO"
        sprintf(buf, "%d %d", card_id, auction->my_cost);
        send_msg(sock, CHOOSE_USER, port, buf, strlen(buf)+1);
        close(sock);        // Dopo l'invio chiudo il socket
    } else {
        SYSERR("Impossibile connettersi al peer %d\n", target_port);
        DBG("Utente %d crashato! Rimuovo dall'asta per card %d", target_port, auction->id_card);
        // Se connessione con peer non va a buon fine, significa che l'utente si è disconnesso e va rimosso da tutte le aste 
        // in corso in cui era partecipante            
        for (int i = 0; i < auction->expected_costs; i++) {
            if (auction->participants[i] == target_port) {
                // Se l'utente si è disconnesso prima di aver ricevuo il suo costo aggiorno comunque auction->costs_received
                // come se l'avessi ricevuto in modo da matenere congruente il numero di messagi aspettati con quelli ricevuti
                if (auction->costs[i] == 0) {
                    auction->costs_received++; 
                }
                auction->costs[i] = __INT_MAX__; // Assegno un costo elevatissimo per escluderlo dall'asta (questo rappresenta
                                                 // anche un flag per verificare se è già stato disconnesso in precedenza)
            }
        }
    }
}

// Verifica se l'asta è conclusa. In caso di vincita manda ACK alla lavagna e avvia Thread Worker
void check_winner(Auction *auction) {
    // L'asta è conclusa solo se l'utente ha ricevuto tutti i costi dagli altri peer.
    if (auction->costs_received == auction->expected_costs) {

        // Prelevo miglior costo tra quelli dei partecipanti
        int best_cost = auction->my_cost;
        int best_port = port;

        for (int i = 0; i < auction->expected_costs; i++) {
            if (auction->participants[i] != 0) {
                if ((auction->costs[i] < best_cost) || (auction->costs[i] == best_cost && auction->participants[i] < best_port)) {
                best_cost = auction->costs[i];
                best_port = auction->participants[i];
                }
            }
        }

        // Controllo se l'utente ha vinto confrontando la porta del miglior costo ricevuto
        if (best_port == port) { 

            INFO("Ho vinto asta per card %d", auction->id_card);
            prompt();

            // ACK_CARD alla lavagna
            char payload[32];
            sprintf(payload, "%d", auction->id_card);
            send_msg(dashboard_sd, ACK_CARD, port, payload, strlen(payload)+1);

            // Thread Worker per esecuzione Card
            pthread_t tid;
            int *arg = malloc(sizeof(int));
            *arg = auction->id_card;
            
            if (pthread_create(&tid, NULL, worker_func, arg) == 0) {
                pthread_detach(tid); // Auto-cleanup
            } else {
                SYSERR("Creazione Thread wroker fallita"); 
                free(arg);
            }
        }
        // Libero lo slot per future aste
        memset(auction, 0, sizeof(Auction)); 
    }

}

// Funzione invocata alla ricezione di AVAILABLE_CARD da parte della lavagna. Necessaria per far partire l'asta 
// con gli altri utenti inviando loro il costo. La funzione ha come argomento il payload del messaggio AVAILABLE_CARD
void auction_start(char *payload) {
    
    // FORMATO PAYLOAD: "ID_CARD NUM_USERS PORT1 PORT2 ..." 
    DBG("auction_start: PAYLOAD: %s", payload);

    char *token = strtok(payload, " "); // Divide il payload in token separati da spazio, il primo elemento è id_card.
    if (!token) {
        return;
    }
    int id_card = atoi(token); // Primo parametro passato nel payload

    // Controllo se l'asta è già stata creata a fronte di un prezzo ricevuto da un peer prima dell'arrivo di questo
    // AVAILABLE_CARD. 
    Auction *auction = get_auction_by_ID(id_card);

    if (auction == NULL) {
        // Slot libero per asta
        auction = get_free_auction_slot();
        if (auction == NULL) {
            // Non ci sono più aste libere
            ERR("Raggiunto il numero massimo di aste contemporanee disponibili");
            return;
        }
        auction->active = 1;
        auction->id_card = id_card;
        auction->costs_received = 0;
        
        memset(auction->participants, 0, sizeof(auction->participants));
        memset(auction->costs, 0, sizeof(auction->costs));
    }

    if (auction->costs_received == 0) { 
        auction->creation_time = time(NULL); 
    }

    // Prelevo numero utenti dal payload
    token = strtok(NULL, " ");
    int num_users = atoi(token);
    auction->expected_costs = num_users - 1;    // Numero utenti meno utente stesso

    // Configuro l'asta o riempio i dati mancanti
    srand(time(NULL) + port + id_card);         // Seed univoco per generazione costo casuale
    auction->my_cost = rand() % 100;            // Costo casuale
    
    DBG("Asta per card %d inizializzata: cost = %d, expected_costs = %d",id_card, auction->my_cost, auction->expected_costs);

    // Prelevo lista porte dal payload
    int valid_ports[MAX_USERS];
    int valid_count = 0;

    while ((token = strtok(NULL, " ")) != NULL) {
        valid_ports[valid_count++] = atoi(token);
    }

    // Sanitizzazione e compattamento
    // Necessaria per eliminare eventuali costi ricevuti in ritardo nella precedente asta e non più validi
    // Questo può accadere se nell'asta precedente sulla stessa card (ora tornata in ToDo a causa della disconnessione del
    // vincitore e riproposta dalla lavagna) il messaggio di un utente che si era discconnesso, e quindi era stato rimosso dall'asta,
    // arriva in ritardo ricreando così un'asta incompleta contenente il suo costo (nonostante risulti disconnesso)
    if (auction->costs_received > 0) {
        int write_idx = 0; // Indice scrittura dati validi

        // Scorre tutti i partecipanti attuali
        for (int i = 0; i < MAX_USERS; i++) {
            if (auction->participants[i] == 0) continue; // Salta slot vuoti

            int p_port = auction->participants[i];
            int is_valid = 0;

            // Verifica se p_port è nella lista valid_ports
            for (int k = 0; k < valid_count; k++) {
                if (valid_ports[k] == p_port) {
                    is_valid = 1;
                    break;
                }
            }

            if (is_valid) {
                // Se la porta dell'utente era già presente copio il costo nella posizione write_idx così da compattare
                // l'array senza lasciare buchi
                auction->participants[write_idx] = auction->participants[i];
                auction->costs[write_idx] = auction->costs[i];
                write_idx++;
            } else {
                // Se una porta era presente ma non trova riscontro nella lista aggiornata delle porte ricevuta con
                // l'AVAILABLE_CARD allora rimuovo l'utente. Verrà sovrascritto o pulito
                DBG("Sanitizzazione: Rimosso utente %d (non in lista)", p_port);
                auction->costs_received--; 
            }
        }
        // Puliamo la "coda" sporca dell'array (da write_idx in poi)
        for (int i = write_idx; i < MAX_USERS; i++) {
            auction->participants[i] = 0;
            auction->costs[i] = 0;
        }
    }

    // Aggiunta utenti, e invio del costo a tutti
    for (int i = 0; i < valid_count; i++) {
        int peer_port = valid_ports[i];

        // Cerchiamo se il peer è già nella lista (magari sopravvissuto alla sanitizzazione)
        int already_present = 0;
        int first_free_slot = -1;

        for (int j = 0; j < MAX_USERS; j++) {
            if (auction->participants[j] == peer_port) {
                already_present = 1;
            }
            else if (auction->participants[j] == 0 && first_free_slot == -1) {
                first_free_slot = j;
            }
        }

        // Se non c'è, lo aggiungiamo
        if (!already_present && first_free_slot != -1) {
            auction->participants[first_free_slot] = peer_port;
        }

        // Inviamo il costo a tutti, anche chi era già presente
        send_cost(peer_port, id_card, auction);
    }

    // Controllo se l'asta è finita perchè potrei aver già ricevuto tutti i costi e poter verificare se l'utente ha vinto
    check_winner(auction);
    
}


// Funzione che si occcupa di eliminare aste create in maniera incompleta (dovute all'arrivo di costi di utenti
// che precedono l'arrivo dell'AVAILABLE_CARD) che non vengono poi completate dall'arrivo di AVAILABLE_CARD
void clean_audictions() {
    time_t now = time(NULL);
    for (int i = 0; i < MAX_CONCURRENT_AUCTIONS; i++) {
        // Se l'asta è attiva ma non ha ancora ricevuto AVAILABLE_CARD (expected_costs == 0)
        // ed è vecchia più di 2 secondi, viene rimossa.
        if (auctions[i].active && auctions[i].expected_costs == 0) {
            if (difftime(now, auctions[i].creation_time) > 2.0) {
                DBG("clean_audictions: Rimossa asta scaduta per card %d", auctions[i].id_card);
                memset(&auctions[i], 0, sizeof(Auction));
            }
        }
    }
}

// La lavagna in caso di disconnession/QUIT di un utente, manda a tutti gli utenti SEND_USER_LIST con la lista di utenti connessi
// Funzione chiamata quando arriva SEND_USER_LIST dalla lavagna, aggiorna lista utenti in tutte le aste attive
// Se l'asta era bloccata in attesa della risposta di un utente che risulta disconnesso perchè non presente nella lista,
// allora viene chiusa l'asta verificando se l'utente ha vinto
// La funzione ha come argomento il payload di SEND_USER_LIST
void user_list_update(char *payload) {
    // Numero e lista utenti connessi
    int ports[MAX_USERS];
    int count = 0;
    
    DBG("user_list_update: payload: %s", payload);

    char *token = strtok(payload, " ");
    
    while ((token = strtok(NULL, " ")) != NULL) {
        ports[count++] = atoi(token); // Numero di porta
    }
    
    // Controllo delle aste attive
    for (int i = 0; i < MAX_CONCURRENT_AUCTIONS; i++) {
        Auction *auction = &auctions[i];
        // Salta aste non attive.  
        // Salta anche aste incomplete (auction->expected_costs == 0) in quanto gli utenti coinvolti in queste non possono essere
        // quelli che hanno causato l'arrivo di send user list.
        // Essendo la lavagna un unico thread l'ordine dei messaggi che arrivano da questa è seriale. La lavagna avrà inviato in successione
        // l'AVAILABLE_CARD a tutti gli utenti e successivamente avrà recepito la disconnessione di un utente inviando SEND_USER_LIST
        // L'utente si aspetta quindi prima AVAILABLE e successivamente un SEND_USER_LIST che coinvolge un utente ricevuto anche nell AVAIABLE.  
        if (auction->active && auction->expected_costs != 0)  {
        
            // Verifico i partecipanti dell'asta
            for (int p = 0; p < auction->expected_costs; p++) {
                
                int p_port = auction->participants[p];
                int is_alive = 0;

                // Cerco se la sua porta è nella lista dei vivi
                for (int k = 0; k < count; k++) {
                    if (ports[k] == p_port) {
                        is_alive = 1;
                        break;
                    }
                }

                // Se l'utente non è presente nella lista (si è disconnesso) e non è già stato individuato come disconnesso
                if (!is_alive &&  auction->costs[p] != __INT_MAX__) {

                    DBG("Utente %d crashato! Rimuovo dall'asta per card %d", p_port, auction->id_card);
                    
                    // Se l'utente si è disconnesso prima di aver ricevuo il suo costo aggiorno comunque auction->costs_received
                    // come se l'avessi ricevuto in modo da matenere congruente il numero di messagi aspettati con quelli ricevuti.
                    if (auction->costs[p] == 0) {
                        auction->costs_received++; 
                    }
    
                    auction->costs[p] = __INT_MAX__; // Assegno un costo elevatissimo per escluderlo dall'asta (questo rappresenta
                                                    // anche un flag per verificare se è già stato disconnesso in precedenza)
                    
                }
            }
            // Controllo vincita utente
            check_winner(auction);

        }
    }
}

// MAIN

int main(int argc, char **argv) {

    if (argc != 2) {
        printf("E' necessario inserire il numero di porta\n");
        exit(EXIT_FAILURE);
    } else {
        port = atoi(argv[1]);
        if (port <= DASHBOARD_PORT) {
            printf("Il numero di porta deve essere maggiore di %d\n", DASHBOARD_PORT);
            exit(EXIT_FAILURE); 
        }
    }

    // Inizializzazione array aste
    init_auctions();

    // Creazione pipe
    if (pipe(int_pipe) < 0) {
        SYSERR("Pipe fallita"); 
        exit(1);
    }

    // Creazione socket e connect lavagna
    connect_to_dashboard();

    // Socket per messaggi dai peer
    start_p2p_listener();

    // Invio del messaggio HELLO alla lavagna
    DBG("Invio HELLO alla lavagna");
    send_msg(dashboard_sd, HELLO, port, NULL, 0);
    
    // Variabili per select
    fd_set readfds;
    int max_sd;
    
    printf("Inserire comando. HELP per elenco comandi disponibili.\n");
    prompt();

    while(1) {

        FD_ZERO(&readfds);                  // Pulisce i set di socket
        FD_SET(STDIN_FILENO, &readfds);     // Aggiunge stdin al set (Per comandi da CLI)
        FD_SET(dashboard_sd, &readfds);     // Aggiunge socket lavagna al set
        FD_SET(p2p_listener, &readfds);     // Aggiunge socket comunicazione peer al set

        FD_SET(int_pipe[0], &readfds);      // Aggiunge pipe al set, così che la select senta se un worker termina

        max_sd = dashboard_sd;
        if (p2p_listener > max_sd) max_sd = p2p_listener;
        if (int_pipe[0] > max_sd) max_sd = int_pipe[0];

        // Select
        int activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);

        // Gestione errore select
        if ((activity < 0) && (errno != EINTR)) {
            SYSERR("Select fallito");
            exit(EXIT_FAILURE);
        }

        // Pulizia aste incomplete e scadute
        clean_audictions();

        // Scrittura sulla pipe, Thread worker ha terminato l'esecuzione della Card
        if (FD_ISSET(int_pipe[0], &readfds)) {
            int completed_card;
            if (read(int_pipe[0], &completed_card, sizeof(int)) > 0) {
                // Invio CARD_DONE alla lavagna
                char payload[32];
                sprintf(payload, "%d", completed_card);
                DBG("Invio CARD_DONE %d alla lavagna", completed_card);
                send_msg(dashboard_sd, CARD_DONE, port, payload, strlen(payload) + 1);
            }
        }

        // Gestione input da tastiera dell'utente
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            char buffer[BUFFER_SIZE]; // Buffer per messaggi da riga di comando
            memset(buffer, 0, BUFFER_SIZE);
            if (read(STDIN_FILENO, buffer, sizeof(buffer) - 1) > 0) {
                buffer[strcspn(buffer, "\n")] = 0;  // Rimuovo carattere new line

                // Unici comandi invocabili direttamente da riga di comando dall'utente

                if (strncmp(buffer, "CREATE_CARD", 11) == 0) {
                    // Invio alla lavagna: CREATE_CARD + descrizione attività, solo se è stata scritta ancheu una descrizione
                    if (strlen(buffer) > 12) {
                        send_msg(dashboard_sd, CREATE_CARD, port, buffer + 12, strlen(buffer + 12) + 1);
                        prompt();
                    } else {
                        printf("Errore: Inserire una descrizione (es. CREATE_CARD Lavoro1)\n");
                        prompt();
                    }
                }
                else if (strncmp(buffer, "SHOW_LAVAGNA", 12) == 0) {
                    // Invio alla lavagna: SHOW_LAVAGNA
                    send_msg(dashboard_sd, SHOW_LAVAGNA, port, NULL, 0);
                }
                else if (strncmp(buffer, "QUIT", 4) == 0) {
                    // Invio alla lavagna: QUIT
                    send_msg(dashboard_sd, QUIT, port, NULL, 0);
                    break; // Per uscire dal loop
                } else if (strncmp(buffer, "HELP", 4) == 0) { // HELP comando di utilità
                    printf("╭─────────────────────────────────────────────────────────────────────────╮\n");
                    printf("│                              LISTA COMANDI                              │\n");
                    printf("├─────────────────────────────────────────────────────────────────────────┤\n");
                    printf("│ CREATE_CARD desc : Aggiunge una card alla lavagna con descrizione desc. │\n");
                    printf("│ SHOW_LAVAGNA     : Visualizza la lavagna a schermo.                     │\n");
                    printf("│ QUIT             : Chiude la conessione con la lavagna e termina.       │\n");
                    printf("╰─────────────────────────────────────────────────────────────────────────╯\n");
                    prompt();
                }
                else {
                    printf("Comando non valido. HELP per informazioni sui comandi disponibili.\n");
                    prompt();
                }
                
            } 
        }

        // Gestione messaggi dalla lavagna
        if (FD_ISSET(dashboard_sd, &readfds)) {
            MsgHeader msgHeader;
            if (recv_header(dashboard_sd, &msgHeader) <= 0) {
                // Errore o connessione chiusa
                ERR("Lavagna disconnessa. Chiudo.\n");
                break;
            }

            char payload[BUFFER_SIZE] = {0};
            if (msgHeader.payload_len > 0) { // Recv sul payload solo se l'header lo prevede
                if (recv_payload(dashboard_sd, payload, msgHeader.payload_len) <= 0) {
                    // Errore o connessione chiusa
                    ERR("Lavagna disconnessa. Chiudo.\n");
                    break;
                }
            }

            // Possibili comandi dalla lavagna
            switch (msgHeader.msg_type) {
                case DASHBOARD_VIEW:
                    // Stampo la lavagna contenuta nel payload sottoforma di stringa
                    DBG("Ricevuto DASHBOARD_VIEW dalla lavagna, mostro lavagna a schermo");
                    printf("%s", payload);
                    prompt();
                    break;

                case PING_USER:
                    // Invio risposta (PONG) al PING essendo ancora attivo
                    DBG("Ricevuto PING_USER dalla lavagna, invio il PONG in risposta");
                    send_msg(dashboard_sd, PONG_LAVAGNA, port, NULL, 0);
                    break;

                case AVAILABLE_CARD:
                    DBG("Ricevuto AVAILABLE_CARD dalla lavagna, avvio asta");
                    auction_start(payload);
                    break;

                case SEND_USER_LIST:
                    DBG("Ricevuto SEND_USER_LIST dalla lavagna, controllo aste attive");
                    user_list_update(payload);
                    break;

                default:
                    break;
            }
        }

        // Costi degli altri utenti
        if (FD_ISSET(p2p_listener, &readfds)) {
            DBG("Connessione da peer");
            struct sockaddr_in peer_addr;
            socklen_t len = sizeof(peer_addr);
            int sock = accept(p2p_listener, (struct sockaddr*)&peer_addr, &len);

            if (sock >= 0) {
                MsgHeader msgHeader;
                if (recv_header(sock, &msgHeader) > 0) {
                    char payload[BUFFER_SIZE] = {0};
                    if (msgHeader.payload_len > 0) {
                        recv_payload(sock, payload, msgHeader.payload_len);
                    }
                    if (msgHeader.msg_type == CHOOSE_USER) {
                        int id, cost;
                        sscanf(payload, "%d %d", &id, &cost); // Prelevo dati dal payload

                        DBG("Ricevuto da utente %d, costo %d per card %d", msgHeader.sender_port, cost, id);

                        // Ricerca asta dedicata alla Card
                        Auction *auction = get_auction_by_ID(id);
                        if (auction != NULL) {
                            
                            // Se AVAILABLE_CARD è già stato ricevuto, cerco la porta tra quelle comunicate e inserisco il costo
                            if (auction->expected_costs > 0) {
                                for (int i = 0; i < auction->expected_costs; i++) {
                                    if (auction->participants[i] == msgHeader.sender_port) {
                                        if (auction->costs[i] != __INT_MAX__) { // Se non è un costo di un utente precedentemente disonnesso.
                                            auction->costs[i] = cost;
                                            auction->costs_received++;
                                        }
                                        break;
                                    }
                                }
                                // Controllo per vittoria
                                check_winner(auction);

                            } else { // Se non è ancora stato ricevuto l'AVAILABLE_CARD, memorizzo il costo nella prima locazione disponibile
                                auction->participants[auction->costs_received] = msgHeader.sender_port;
                                auction->costs[auction->costs_received] = cost;
                                auction->costs_received++;
                            }

                        } else {    // Ricezione di un CHOOSE_USER con un costo di un utente per un asta di cui non ho 
                                    // ancora ricevuto AVAILABLE_CARD dalla lavagna e nessun altro costo da un utente.
                                    // L'asta non è quindi inizializzata. 
                                    // Trovo slot libero per asta e assegno campi noti.   
                            DBG("Creazione asta incompleta per card %d (deve arrivare ancora AVAILABLE_CARD)", id);
                            auction = get_free_auction_slot();
                            auction->active = 1;
                            auction->expected_costs = 0;    // Indica che non è stato ancora ricevuto AVAILABLE_CARD dalla lavagna
                            auction->id_card = id;
                            auction->costs_received = 1;
                            auction->creation_time = time(NULL); // Serve solo per clean_audictions()

                            auction->participants[0] =  msgHeader.sender_port;
                            auction->costs[0] = cost;
                        }
                    }
                }
                close(sock);
            } else {
                SYSERR("Errore in accept su sock P2P");
                // Non gestisco errore, in quanto a questo errore seguira sicuramente un messaggio da parte dalla lavagna
                // causato dalla disconnessione
            }
        }
    }
    close(dashboard_sd); // Chiusura socket
    close(p2p_listener);
    close(int_pipe[0]);
    close(int_pipe[1]);
    exit(0);
}


