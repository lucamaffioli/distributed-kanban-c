# --- CONFIGURAZIONE ---
CC = gcc
# Flag base (SENZA -DDEBUG)
CFLAGS = -Wall -Wextra -g -pthread -Iinclude

SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin

# Path Eseguibili
LAVAGNA_BIN = $(BIN_DIR)/lavagna
UTENTE_BIN = $(BIN_DIR)/utente

# --- TARGET FALSI ---
.PHONY: all debug clean directories

# --- TARGET PRINCIPALI ---

# Default: Pulisce tutto, crea cartelle e compila in modalità RELEASE
all: clean directories $(LAVAGNA_BIN) $(UTENTE_BIN)
	@echo "--- Compilazione completata (RELEASE) ---"

# Debug: Aggiunge il flag -DDEBUG e poi esegue 'all'
debug: CFLAGS += -DDEBUG
debug: all
	@echo "--- Compilazione completata (DEBUG) ---"

directories:
	@mkdir -p $(OBJ_DIR)
	@mkdir -p $(BIN_DIR)

# --- COMPILAZIONE OGGETTI COMUNI ---

# Compila common.o
$(OBJ_DIR)/common.o: $(SRC_DIR)/common.c
	@echo "Compilazione Common..."
	$(CC) $(CFLAGS) -c $(SRC_DIR)/common.c -o $(OBJ_DIR)/common.o

# Compila disegno.o
$(OBJ_DIR)/disegno.o: $(SRC_DIR)/disegno.c
	@echo "Compilazione Disegno..."
	$(CC) $(CFLAGS) -c $(SRC_DIR)/disegno.c -o $(OBJ_DIR)/disegno.o

# --- LINKING ESEGUIBILI ---

# Lavagna (dipende da common.o e disegno.o)
$(LAVAGNA_BIN): $(SRC_DIR)/lavagna.c $(OBJ_DIR)/common.o $(OBJ_DIR)/disegno.o
	@echo "Link Lavagna..."
	$(CC) $(CFLAGS) $(SRC_DIR)/lavagna.c $(OBJ_DIR)/common.o $(OBJ_DIR)/disegno.o -o $(LAVAGNA_BIN)

# Utente (dipende da common.o)
$(UTENTE_BIN): $(SRC_DIR)/utente.c $(OBJ_DIR)/common.o
	@echo "Link Utente..."
	$(CC) $(CFLAGS) $(SRC_DIR)/utente.c $(OBJ_DIR)/common.o -o $(UTENTE_BIN)

# --- PULIZIA ---

clean:
	@echo "Pulizia file oggetto e binari..."
	rm -rf $(OBJ_DIR) $(BIN_DIR)