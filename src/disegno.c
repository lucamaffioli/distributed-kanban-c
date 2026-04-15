
#include "../include/lavagna.h" 

// File per formattazione taballa KanBan
// Utilizzati caratteri dello standard UTF-8 (Unicode) per il disegno dei bordi

// Costanti per dimensionamento
#define TEXT_WIDTH 24   // Massima larghezza riga
#define CARD_WIDTH (1 + 1 + TEXT_WIDTH + 1 + 1) // Riga più tratto verticale (pipe) e spazio in entrambi i lati
#define MAX_DESC_LINES 10 // Numero massimo di righe in cui il testo può essere suddiviso

// Strutture d'appoggio per il rendering della lavagna

// Card con informazioni formattate o utili alla stampa
typedef struct {
    int id;
    int user_port;
    char lines[MAX_DESC_LINES][TEXT_WIDTH + 1]; // Matrice contenente testo card formattato
    int text_lines_count;   // Numero di righe effettive del testo
    int total_height;       // Righe della card completa
} RenderCard;

// Struttura per memorizzare informazioni sulla singola colonna
typedef struct {
    int card_indices[MAX_CARD]; 
    int count;              // Numero di card presenti nella colonna
    int current_idx;        // Posizione della card nell'array card_indices
    int current_line;       // Linea corrente per la stampa della card attiva
    RenderCard active_card; // La struttura mantiene solo la RenderCard in fase di stampa.
    int is_finished;        // 1 quando non ci sono più card da stampare per la colonna.
} ColState;

// Funzioni di preparazione
static void prepare_render_card(Card *card, RenderCard *renderCard) {
    if (!card) { renderCard->total_height = 0; return; }
    
    renderCard->id = card->ID;
    renderCard->user_port = card->owner_port;
    char full_text[MAX_DESC]; 

    // Copia descrizione, solo caratteri effetivi, in full_text
    snprintf(full_text, sizeof(full_text), "%s", card->desc);
    
    int len = strlen(full_text);
    // Contatore linea 
    int line = 0;
    // Contatore carattere
    int i = 0;
    
    for(int k=0; k<MAX_DESC_LINES; k++) {
        memset(renderCard->lines[k], 0, TEXT_WIDTH+1); // Pulizia matrice testo
    } 

    // Divide la descrizione in righe di massimo TEXT_WIDTH, mantenendo le parole intere
    // Finchè non terminano i caratteri o viene raggiunta l'ultima linea
    while (i < len && line < MAX_DESC_LINES) {
        int char_count = 0, last_space = -1;
        int start_idx = i; // Indice inizio da analizzare
        while (i < len && char_count < TEXT_WIDTH) { // Dentro un blocco di TEXT_WIDTH, ricerca l'ultimo spazio
            if (full_text[i] == ' ')  { 
                last_space = i;
            }
            i++;
            char_count++;
        }
        // last_space è la posizione dell'ultimo spazio
        // Char_count varrà TEXT_WIDTH a meno che non siamo all'ultima riga che è più corta
        if (char_count == TEXT_WIDTH && i < len) {  // Riga intera, ma non è l'ultima
            if (last_space != -1 && last_space >= start_idx) { // Trovato uno spazio nella riga analizzata
                int n = last_space - start_idx; // Numero di caratteri da copiare (per non spezzare parole)
                strncpy(renderCard->lines[line], &full_text[start_idx], n); // Copia nella linea corretta della matrice di caratteri
                i = last_space + 1; // Rinizio dalla parola successiva rispetto all'ultimo spazio
            } else {
                strncpy(renderCard->lines[line], &full_text[start_idx], TEXT_WIDTH); // Nel caso la parola sia più lunga dello spazio disponibile la copia avviene comunque
                                                                                     // Verrà spezzata in più righe nella stampa
            }
        } else { 
            strncpy(renderCard->lines[line], &full_text[start_idx], char_count); // E' l'ultima riga, vieni copiata per la lunghezza effettiva nella matrice.
        }
        line++; // Incremento contatore riga analizzata.
    }
    renderCard->text_lines_count = line; // Aggiorno contatore delle righe del testo dopo la formattazione.
    renderCard->total_height = 2 + renderCard->text_lines_count; // Le rigghe totali della card includono intestazione e riga divisoria finale
}

// Funzione invocata dalla lavagna.
// Scrive dentro il buffer formattando la stampa su più colonne.
void draw_dashboard(char *buffer, Dashboard *dashboard) {
    char *p = buffer;
    ColState cols[3]; // Considero tre Colonne per la stampa
    memset(cols, 0, sizeof(cols)); 
     
    // Inserimento in ogni ColState delle relative card (divisione per stato)
    for (int i = 0; i < dashboard->cards_count; i++) {
        int state = dashboard->cards[i].state;
        cols[state].card_indices[cols[state].count++] = i;  // Dentro la struttura della colonna, segno solo l'indice che
                                                            // la card occupa nell'array della lavagna
    }

    // Ciclo su ogni colonna, renderizzando la prima card su ogni colonna, se presente.
    for (int c = 0; c < 3; c++) {
        if (cols[c].count > 0) { // Se è presente almeno una card
            Card *src = &dashboard->cards[cols[c].card_indices[0]]; // Preleva dalla dashboard la prima card 
            prepare_render_card(src, &cols[c].active_card); // Esecuzione del rendering salvando il risultato nella struttura
        } else {
            cols[c].is_finished = 1; // Flag in caso di colonna vuota
        }
    }

    // Header Tabella
    int col_w = 1 + CARD_WIDTH + 1; // Larghezza di un colonna
    int title_w = (col_w*3) + 2;

    // Bordo Superiore
    p += sprintf(p, "┌");
    for(int k=0; k<title_w; k++) { p+=sprintf(p, "─"); } 
    p+=sprintf(p, "┐\n");

    // Titolo
    char title[title_w];
    sprintf(title, "LAVAGNA ID: %d (Card Totali: %d)", dashboard->ID, dashboard->cards_count);
    p += sprintf(p, "│ %-*s │\n", col_w*3, title); // %-*s allineamento a siistra più paramentro per padding

    
    // Linea divisoria titolo colonne
    p += sprintf(p, "├");
    for(int k=0; k<col_w; k++) { p+=sprintf(p, "─"); } p+=sprintf(p, "┬");
    for(int k=0; k<col_w; k++) { p+=sprintf(p, "─"); } p+=sprintf(p, "┬");
    for(int k=0; k<col_w; k++) { p+=sprintf(p, "─"); } p+=sprintf(p, "┤\n");

    // Nomi colonne
    p += sprintf(p, "│ %-*s │ %-*s │ %-*s │\n", col_w-2, "TO DO", col_w-2, "DOING", col_w-2, "DONE");

    // Linea divisoria
    p += sprintf(p, "├");
    for(int k=0; k<col_w; k++) { p+=sprintf(p, "─"); } p+=sprintf(p, "┼");
    for(int k=0; k<col_w; k++) { p+=sprintf(p, "─"); } p+=sprintf(p, "┼");
    for(int k=0; k<col_w; k++) { p+=sprintf(p, "─"); } p+=sprintf(p, "┤\n");

    // Loop Rendering dell Card
    while (!(cols[0].is_finished && cols[1].is_finished && cols[2].is_finished)) { // Continua finchè ci sono card da stampare

        p += sprintf(p, "│"); 

        // Per ogni colonna viene stampata una riga della card attiva che viene anche aggiornata se necessario.
        for (int c = 0; c < 3; c++) {
            p += sprintf(p, " ");
            ColState *S = &cols[c];

            if (S->is_finished) {
                p += sprintf(p, "%-*s", CARD_WIDTH, ""); // Stampo riga vuota se nella colonna non ho card da stampare.
            } 
            else {
                RenderCard *rc = &S->active_card;
                int r = S->current_line; // Linea da stampare per la card attiva.
            
                if (r == 0) { // Stampa intestazione della card, stampa user solo se colonna è DOING o DONE
                    if (rc->user_port == 0) {
                        p += sprintf(p, "│ ID: %-*d │", TEXT_WIDTH - 4, rc->id);
                    } else {
                        p += sprintf(p, "│ ID: %-2d        USER: %-*d │", rc->id, TEXT_WIDTH - 20,  rc->user_port);
                    }
                }
                else if (r >= 1 && r < (1 + rc->text_lines_count)) { // Se non è intestazione ma è riga della descrizione
                    p += sprintf(p, "│ %-*s │", TEXT_WIDTH, rc->lines[r-1]);
                }
                else if (r == rc->total_height - 1) { // Ultima riga, stampa bordo inferiore
                    p += sprintf(p, "╰"); 
                    for(int k=0; k<TEXT_WIDTH+2; k++) { p+=sprintf(p, "─"); } 
                    p += sprintf(p, "╯");
                }
            
                S->current_line++; // Incremento linea per prossima stampa

                if (S->current_line >= rc->total_height) { // Se stampa card completata
                    S->current_idx++; // Passa alla prossima card
                    S->current_line = 0; // Partendo dalla prima riga
                    
                    if (S->current_idx < S->count) { // Se sono presenti altre card
                        Card *next_src = &dashboard->cards[S->card_indices[S->current_idx]]; //prelevo prossima card dalla lavagna  
                        prepare_render_card(next_src, &S->active_card); // Formattazione della card
                    } else {
                        S->is_finished = 1; // Altrimenti flag colonna come terminata
                    }
                }
            }
            p += sprintf(p, " │");
        }
        p += sprintf(p, "\n");
    }

    // Bordo Inferiore 
    p += sprintf(p, "└");
    for(int k=0; k<col_w; k++) { p+=sprintf(p, "─"); } p+=sprintf(p, "┴");
    for(int k=0; k<col_w; k++) { p+=sprintf(p, "─"); } p+=sprintf(p, "┴");
    for(int k=0; k<col_w; k++) { p+=sprintf(p, "─"); } p+=sprintf(p, "┘\n");
}