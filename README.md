# InventixDB (WIP)

> **Status**: Prototype Phase 1 (Under Active Development)
>
> This project is currently in the early stages of development. It supports basic CRUD operations, a custom lexer/parser, and a foundational TCP server architecture. Distributed features and robust transaction support are planned for future phases.

InventixDB is a custom, mini distributed database system implemented in C. It features a unique **Roman-Urdu (Hinglish)** query syntax and a **Hybrid Architecture** supporting both structured (SQL-like) and semi-structured (JSON) data.

## System Architecture

The system follows a Master-Worker architecture designed for distributed data handling.

![System Architecture](docs/image.png)

### Architecture Overview
*   **Client Layer**: currently supports a CLI (`inventixdb`) for interacting with the database.
*   **Master Node**:
    *   **TCP Listener**: Accepts client connections.
    *   **Hinglish Lexer & Parser**: Tokenizes and parses the custom `TABLE BANAO`, `SELECT ...` syntax into an Abstract Syntax Tree (AST).
    *   **Query Router**: (Planned) Routes queries to appropriate Worker Nodes based on Sharding Logic (Hash Partitioning).
*   **Worker Node**:
    *   **Storage Engine**: A Thread-safe Key-Value store supporting Table mappings and Documents.
    *   **Persistence**: Uses an Append-Only Log (`inventix.log`) and periodic snapshots (`inventix.snap`) for durability.

---

## Features (Phase 1 Implemented)

- [x] **Custom Syntax**: Write queries in Hinglish (e.g., `JAHAN` instead of `WHERE`).
- [x] **DataType Support**: `INT`, `FLOAT`, `STRING`/`TEXT`, `BOOL`.
- [x] **Primary Keys**: Validates unique constraints on IDs.
- [x] **Auto Increment**: Support for `AUTO` keyword in `INSERT` statements.
- [x] **Persistence**: Data survives restarts (Log-Structured storage).
- [x] **Hybrid Model**: Supports both Relational Tables and basic JSON Document storage.

---

## Hinglish Query Language (HQL) Reference

### 1. Data Definition (DDL)

**Create Table**
```sql
TABLE BANAO users (
    id INT PRIMARY KEY,
    name TEXT,
    is_active BOOL
);
```

**Drop Table**
```sql
TABLE GIRAO users;
```

### 2. Data Manipulation (DML)

**Insert Data**
```sql
-- Manual ID
INSERT KARO users VALUES (1, "Ali", 1);

-- Auto Increment ID
INSERT KARO users VALUES (AUTO, "Sara", 1);
```

**Select Data**
```sql
SELECT name FROM users JAHAN id = 1;
```

**Delete Data**
```sql
NIKALO FROM users JAHAN id = 1;
```

### 3. NoSQL / Document Operations

**Store Document**
```sql
RAKHO logs "{\"event\": \"login\", \"time\": 1234}";
```

---

## Build & Run

### Prerequisites
*   GCC (MinGW on Windows)
*   Make

### Compilation
```bash
make
```

### Running the CLI
```bash
./inventixdb
```

### Running the Server (Experimental)
```bash
./inventix-server --port 8888 --master
```
