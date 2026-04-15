# Distributed Kanban Board (C / TCP Sockets)

A distributed terminal-based Kanban board implemented in C. This project was developed as the final assignment for the **Reti Informatiche** (Computer Networks) course at the **University of Pisa (UniPi)**.

The system implements a hybrid network architecture: a **Client-Server** model for global state synchronization and a **Peer-to-Peer (P2P)** model for managing task assignment auctions among users.

## 🌟 Project Overview

The application allows users to interact with a Kanban board (To Do, Doing, Done). When a task becomes available, connected users automatically start a P2P auction to determine who will handle the card, exchanging random costs via a distributed logic.

## 🛠 Tech Stack & Architecture

- **Language:** C (POSIX compliant, tested on Ubuntu 24.04)
- **Networking:** TCP Sockets
- **Concurrency:** I/O Multiplexing via `select()` to manage multiple clients and timers in a single thread.
- **Multithreading:** Worker threads (`pthread`) used in the client to simulate asynchronous task execution.
- **Build System:** Makefile

### Key Engineering Choices

- **Custom Application Protocol:** Communication relies on a hybrid protocol featuring a fixed 12-byte binary header (handling network endianness via `htonl`/`ntohl`) and a variable-length textual payload.
- **Deterministic Memory Management:** Zero dynamic memory allocation (no heap usage). Fixed-size arrays are used to guarantee stability, predictability, and prevent memory leaks.
- **P2P Short-lived Connections:** User auctions are handled via short-lived TCP connections to reduce state overhead while guaranteeing reliable delivery.
- **Terminal UI Engine:** A custom rendering module (`disegno.c`) dynamically builds the dashboard with an integrated word-wrapping algorithm for the terminal.

## 📂 Repository Structure

- `src/`: Source code modules (`common.c`, `lavagna.c`, `utente.c`, `disegno.c`).
- `include/`: Header files defining the protocol and data structures.
- `doc/`: Project documentation and original university assignments.

## 🚀 How to Build and Run

### 1. Compilation
Use the provided Makefile to build the project. Executables will be generated in the `bin/` directory.
```bash
make
```
*(Use `make debug` to enable verbose diagnostic logs).*

### 2. Start the Server (Lavagna)
The server runs on the default port 5678.
```bash
./bin/lavagna
```

### 3. Start a Client (Utente)
Open a new terminal for each user. Users must be assigned incremental ports starting from 5679.
```bash
./bin/utente 5679
```

---
*Developed by Luca Maffioli - BSc in Computer Engineering @ UniPi*
