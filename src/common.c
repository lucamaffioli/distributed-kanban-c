#include "../include/common.h"

// Funzione per invio di un messaggio.
// Torna -1 in caso di errore, 0 altrimenti.
int send_msg(int sd, MsgType msg_type, int sender_port, const void *payload, int payload_len) {
    MsgHeader msgHeader;

    // Converto i parametri interi in formato network per evitare errori dovuti all'endianess. 
    // Ovviamente questo è inutile essendo il progetto eseguito sullo stesso calcolatore, ma se l'applicazione fosse
    // realmente distribuita sarebbe un passaggio fondamentale.
    msgHeader.msg_type = htonl((int)msg_type) ; // Va convertito essendo l'enum un int.
    msgHeader.sender_port = htonl(sender_port);
    msgHeader.payload_len = htonl(payload_len);

    // Invio dell'header del messaggio
    if (send(sd, &msgHeader, sizeof(MsgHeader), 0) < 0) {
        SYSERR("Errore durante la send dell'Header");
        return -1;
    }

    // Invio del payload del messaggio se presente. 
    // Non serve nessun tipo di conversione essendo il payload una stringa.
    if (payload_len > 0 && payload != NULL) {
        if (send(sd, payload, payload_len, 0) < 0) {
            SYSERR("Errore durante la send del payload");
            return -1;
        }
    }
    return 0;
}

// Funzione per ricere un MsgHeader.
// Torna 0 in caso di connessione chiusa, -1 per errore, altrimenti torna il numero di byte ricevuti.
int recv_header(int sd, MsgHeader *msgHeader) {

    MsgHeader tmp; //MsgHeader temporaneo per contenere dati non ancora convertiti con endianess dell'host.

    // Ricezione dei dati in formato network;
    int ret = recv(sd, &tmp, sizeof(MsgHeader), MSG_WAITALL);   // Flag MSG_WAITALL: la recv attende l'arrivo di                                                                     
                                                                // tutti i byte richiesti, non esce al primo byte.
    if (ret == 0) return 0; // Connessione chiusa.
    if (ret < 0) return -1; // Errore durante la recv

    // Conversione campi da endianess network a host
    msgHeader->msg_type = (MsgType)ntohl(tmp.msg_type);
    msgHeader->sender_port = ntohl(tmp.sender_port);
    msgHeader->payload_len = ntohl(tmp.payload_len);

    return ret;
}

// Funzione per ricevere il payload, viene chiamata dopo la recv_header se il campo payload_len è > 0.
// Torna 0 in caso di connessione chiusa, -1 per errore, altrimenti torna il numero di byte ricevuti.
int recv_payload(int sd, void *buffer, int len) {
    int ret = recv(sd, buffer, len, MSG_WAITALL);   // Flag MSG_WAITALL: la recv attende l'arrivo di                                                                     
                                                    // tutti i byte richiesti, non esce al primo byte.
    if (ret == 0) return 0; // Connessione chiusa.
    if (ret < 0) return -1; // Errore durante la recv
    return ret;
}

