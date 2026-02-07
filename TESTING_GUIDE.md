# 🧪 InventixDB — Full Manual Testing Guide
### Server v1.3.0 | Client v1.2.0 | CLI v0.5

> **⚠️ Port Note**: Server listens on `8888` (inventix.conf). Client defaults to `9876`.  
> Always use `inventix-client -p 8888` or it won't connect!

---

## 📋 TABLE OF CONTENTS

1. [Terminal Setup](#1-terminal-setup)
2. [CLI (inventixdb.exe) Tests](#2-cli-inventixdbexe-tests)
3. [Server + Client Tests](#3-server--client-tests)
4. [Security & Auth Tests](#4-security--auth-tests)
5. [Full SQL/Hinglish Test Commands](#5-full-sqlhinglish-test-commands)
6. [Stress & Edge Cases](#6-stress--edge-cases)

---

## 1. TERMINAL SETUP

Open **3 separate terminals** in `d:\semester 5\inventixDB`:

| Terminal | Purpose | Command |
|----------|---------|---------|
| **T1** | Server | `./inventix-server` |
| **T2** | Client | `./inventix-client -p 8888` |
| **T3** | Second Client (stress) | `./inventix-client -p 8888` |

---

## 2. CLI (inventixdb.exe) Tests

### 2.1 — Authentication Tests
```
> ./inventixdb
```
You should see the login prompt:
```
=== InventixDB Authentication / Tasdiq ===
  Default: admin / Admin@123

Username: 
```

| # | Test | Input | Expected Result |
|---|------|-------|-----------------|
| 1 | **Correct login** | `admin` / `Admin@123` | `Login successful! Welcome, admin (role: superadmin)` |
| 2 | **Wrong password** | `admin` / `wrongpass` | `Login failed: ... (1/3 attempts)` |
| 3 | **Empty username** | *(just press Enter)* | `Username cannot be empty.` |
| 4 | **3 failed attempts** | Wrong password 3x | `Too many failed login attempts. Exiting.` (program exits) |
| 5 | **Wrong user** | `nobody` / `test` | `Login failed: ...` |

### 2.2 — Help & Exit Commands
After successful login:
```sql
help
madad
exit
```
Re-launch and login, then try:
```sql
bahar
```
Re-launch and login, then:
```sql
quit
niklo
```

---

## 3. SERVER + CLIENT TESTS

### 3.1 — Start Server
**Terminal T1:**
```
./inventix-server
```
Expected: Server starts on port 8888, shows banner.

### 3.2 — Client Connection
**Terminal T2:**
```
./inventix-client -p 8888
```
Expected: Connected banner, prompt shows `public@127.0.0.1:8888>`

### 3.3 — Client Login
```sql
LOGIN admin Admin@123
```
Expected: Login success message.

### 3.4 — Client Meta-Commands

| # | Command | Expected |
|---|---------|----------|
| 1 | `\help` | Full help menu |
| 2 | `help` | Full help menu |
| 3 | `\status` | Host, port, DB info |
| 4 | `\db` | Shows current database (public) |
| 5 | `\list` | Shows databases (like SHOW DATABASES) |
| 6 | `\history` | Shows command history |
| 7 | `\clear` | Clears screen |

### 3.5 — Server Status Commands
```sql
STATUS
HALAT
STHITI
WHOAMI
```

### 3.6 — Client File Execution
Create a file `test.sql`:
```sql
CREATE TABLE filetest (id INT PRIMARY KEY, name TEXT);
INSERT INTO filetest VALUES (1, 'from_file');
SELECT * FROM filetest;
DROP TABLE filetest;
```
Run:
```
./inventix-client -p 8888 -f test.sql
```

### 3.7 — Client Single Command
```
./inventix-client -p 8888 -c "SELECT * FROM students;"
```

### 3.8 — Client Disconnect/Reconnect
```
\disconnect
\reconnect
```

### 3.9 — Client Exit Commands
```
quit
exit
bahar
niklo
\q
\quit
```

---

## 4. SECURITY & AUTH TESTS

### 4.1 — Login / Logout Cycle (Client)
```sql
LOGIN admin Admin@123
WHOAMI
LOGOUT
WHOAMI
LOGIN admin Admin@123
```

### 4.2 — Wrong Password (Brute-Force Test)
```sql
LOGIN admin wrong1
LOGIN admin wrong2
LOGIN admin wrong3
LOGIN admin wrong4
LOGIN admin wrong5
```
Expected: After 5th failure → **IP lockout for 5 minutes** (300 seconds).
6th attempt should say "IP locked out."

### 4.3 — After Lockout
```sql
LOGIN admin Admin@123
```
Expected: Still rejected (locked out for 5 minutes).

### 4.4 — User Management
```sql
LOGIN admin Admin@123
CREATE USER testuser PASSWORD 'Test@1234' ROLE developer;
SHOW USERS
LOGIN testuser Test@1234
WHOAMI
```

### 4.5 — Show Users (RBAC)
```sql
SHOW USERS
```
Expected: Table with USERNAME, ROLE, STATUS columns.

---

## 5. FULL SQL/HINGLISH TEST COMMANDS

### ═══════════════════════════════════════
### 5.1 — DATABASE MANAGEMENT
### ═══════════════════════════════════════

```sql
-- Create databases (SQL)
CREATE DATABASE college;
CREATE DATABASE testdb;
CREATE DATABASE tempdb;

-- Show all databases (SQL + Hinglish)
SHOW DATABASES;
DEKHO DATABASES;

-- Switch database
USE college;
ISTEMAAL college;

-- Drop database (SQL)
DROP DATABASE tempdb;

-- Drop database (Hinglish - GIRAO)
GIRAO DATABASE testdb;

-- Re-create to test HATAO
CREATE DATABASE testdb2;
SHOW DATABASES;

-- Drop database (Hinglish - HATAO)
DATABASE HATAO testdb2;
SHOW DATABASES;

-- Delete syntax
CREATE DATABASE deltest;
DELETE DATABASE deltest;
SHOW DATABASES;

-- Cannot drop "public" database
DROP DATABASE public;

-- Cannot drop currently-used database
USE college;
DROP DATABASE college;

-- Switch back and drop
USE public;
DROP DATABASE college;
SHOW DATABASES;
```

### ═══════════════════════════════════════
### 5.2 — TABLE DDL (Create / Drop)
### ═══════════════════════════════════════

```sql
-- SQL syntax
CREATE TABLE students (
    id INT PRIMARY KEY,
    name TEXT,
    age INT,
    grade TEXT
);

-- Hinglish syntax
TABLE BANAO teachers (
    id INT PK,
    name TEXT,
    subject TEXT,
    salary INT
);

-- Show tables
SHOW TABLES;
DEKHO TABLES;

-- Create with index
CREATE INDEX ON students (name);

-- Drop table (SQL)
DROP TABLE teachers;

-- Drop table (Hinglish)
TABLE BANAO temp (id INT PK, x TEXT);
GIRAO TABLE temp;

SHOW TABLES;
```

### ═══════════════════════════════════════
### 5.3 — INSERT (DALO / INSERT INTO)
### ═══════════════════════════════════════

```sql
-- SQL syntax
INSERT INTO students VALUES (1, 'Rahul', 20, 'A');
INSERT INTO students VALUES (2, 'Priya', 21, 'A+');
INSERT INTO students VALUES (3, 'Amit', 19, 'B');
INSERT INTO students VALUES (4, 'Sneha', 22, 'A');
INSERT INTO students VALUES (5, 'Vikram', 20, 'C');

-- Hinglish syntax
DALO students MAAN (6, 'Neha', 21, 'B+');
DALO students MAAN (7, 'Karan', 23, 'A');
DALO students MAAN (8, 'Pooja', 19, 'A+');
DALO students MAAN (9, 'Rohit', 20, 'B');
DALO students MAAN (10, 'Anjali', 22, 'A');
```

### ═══════════════════════════════════════
### 5.4 — SELECT (DIKHAO / SELECT)
### ═══════════════════════════════════════

```sql
-- Select all (SQL)
SELECT * FROM students;

-- Select all (Hinglish)
DIKHAO * SE students;

-- Select with WHERE (SQL)
SELECT * FROM students WHERE age = 20;
SELECT * FROM students WHERE grade = 'A';

-- Select with WHERE (Hinglish)
DIKHAO * SE students JAHAN age = 20;
DIKHAO * SE students JAHAN grade = 'A+';

-- Multiple conditions
SELECT * FROM students WHERE age > 20;
SELECT * FROM students WHERE age >= 20;

-- Select with no matching rows
SELECT * FROM students WHERE age = 99;
```

### ═══════════════════════════════════════
### 5.5 — UPDATE (BADLO / UPDATE)
### ═══════════════════════════════════════

```sql
-- SQL syntax
UPDATE students SET grade = 'A+' WHERE id = 3;
SELECT * FROM students WHERE id = 3;

-- Hinglish syntax
BADLO students RAKHO_YEH grade = 'B' JAHAN id = 5;
SELECT * FROM students WHERE id = 5;

-- Update name
UPDATE students SET name = 'Rahul Kumar' WHERE id = 1;
DIKHAO * SE students JAHAN id = 1;

-- Verify all
SELECT * FROM students;
```

### ═══════════════════════════════════════
### 5.6 — DELETE (NIKALO / DELETE)
### ═══════════════════════════════════════

```sql
-- SQL syntax
DELETE FROM students WHERE id = 10;
SELECT * FROM students;

-- Hinglish syntax
NIKALO SE students JAHAN id = 9;
SELECT * FROM students;

-- Verify count
SELECT * FROM students;
```

### ═══════════════════════════════════════
### 5.7 — TRANSACTIONS (BEGIN/COMMIT/ROLLBACK)
### ═══════════════════════════════════════

```sql
-- Basic commit
BEGIN;
INSERT INTO students VALUES (11, 'TestCommit', 25, 'X');
COMMIT;
SELECT * FROM students WHERE id = 11;

-- Basic rollback
BEGIN;
INSERT INTO students VALUES (12, 'TestRollback', 99, 'Z');
SELECT * FROM students WHERE id = 12;
ROLLBACK;
SELECT * FROM students WHERE id = 12;

-- Hinglish transaction
SHURU;
DALO students MAAN (13, 'HinglishTxn', 30, 'H');
PUKKA;
DIKHAO * SE students JAHAN id = 13;

-- Hinglish rollback
SHURU;
DALO students MAAN (14, 'WillRollback', 31, 'W');
WAPAS;
SELECT * FROM students WHERE id = 14;

-- Savepoint test
BEGIN;
INSERT INTO students VALUES (20, 'Save1', 20, 'S');
SAVEPOINT sp1;
INSERT INTO students VALUES (21, 'Save2', 21, 'S');
ROLLBACK TO sp1;
COMMIT;
SELECT * FROM students WHERE id = 20;
SELECT * FROM students WHERE id = 21;
```

### ═══════════════════════════════════════
### 5.8 — NoSQL / DOCUMENT STORE
### ═══════════════════════════════════════

```sql
-- Insert documents
RAKHO logs {"level":"info","msg":"Server started","ts":1001};
RAKHO logs {"level":"error","msg":"Disk full","ts":1002};
RAKHO logs {"level":"warn","msg":"Memory high","ts":1003};
RAKHO logs {"level":"info","msg":"User login","ts":1004};
RAKHO logs {"level":"debug","msg":"Query parsed","ts":1005};

-- Get all documents
MANGWAO logs;

-- Find specific document by ID
DHUNDO logs JAHAN id=1;
DHUNDO logs JAHAN id=3;

-- Remove a document
HATAO logs 2;
MANGWAO logs;

-- Verify removal
DHUNDO logs JAHAN id=2;
```

### ═══════════════════════════════════════
### 5.9 — SUBQUERIES (Scalar)
### ═══════════════════════════════════════

```sql
-- Setup
CREATE TABLE departments (id INT PRIMARY KEY, name TEXT, head_id INT);
INSERT INTO departments VALUES (1, 'CS', 1);
INSERT INTO departments VALUES (2, 'Math', 3);

-- Subquery: find students who head a department
SELECT * FROM students WHERE id = (SELECT head_id FROM departments WHERE name = 'CS');
DIKHAO * SE students JAHAN id = (SELECT head_id FROM departments JAHAN name = 'CS');
```

### ═══════════════════════════════════════
### 5.10 — ALTER TABLE (Schema Evolution)
### ═══════════════════════════════════════

```sql
-- Add column
ALTER TABLE students ADD COLUMN email TEXT;

-- Drop column
ALTER TABLE students DROP COLUMN email;

-- Rename column
ALTER TABLE students RENAME COLUMN grade TO marks;

-- Verify
SHOW TABLES;
```

### ═══════════════════════════════════════
### 5.11 — SYSTEM COMMANDS
### ═══════════════════════════════════════

```sql
-- Checkpoint (force save)
CHECKPOINT;

-- Switch databases
USE public;
ISTEMAAL public;

-- Show tables/databases
SHOW TABLES;
DEKHO TABLES;
SHOW DATABASES;
DEKHO DATABASES;
```

### ═══════════════════════════════════════
### 5.12 — PREPARED STATEMENTS (Server Only)
### ═══════════════════════════════════════

```sql
-- Only works via client→server connection
PREPARE get_student AS SELECT * FROM students WHERE id = ?;
EXECUTE get_student USING (1);
EXECUTE get_student USING (3);
DEALLOCATE get_student;

-- Hinglish
TAYYAR search JAISE SELECT * FROM students WHERE age = ?;
CHALAO search ISTEMAL (20);
DEALLOCATE search;
```

### ═══════════════════════════════════════
### 5.13 — BACKUP & RESTORE
### ═══════════════════════════════════════

```sql
BACKUP;
RESTORE;
```

### ═══════════════════════════════════════
### 5.14 — CLUSTER / DISTRIBUTED (Server Only)
### ═══════════════════════════════════════

```sql
CLUSTER
JHAAD
NODES
GRANTHI
```

---

## 6. STRESS & EDGE CASES

### 6.1 — Empty Table Query
```sql
CREATE TABLE empty_tbl (id INT PRIMARY KEY, val TEXT);
SELECT * FROM empty_tbl;
DROP TABLE empty_tbl;
```

### 6.2 — Duplicate Primary Key
```sql
INSERT INTO students VALUES (1, 'Duplicate', 99, 'X');
```
Expected: Error (PK already exists).

### 6.3 — Non-Existent Table
```sql
SELECT * FROM nonexistent;
INSERT INTO ghost VALUES (1, 'boo');
DROP TABLE ghost;
```
Expected: Error messages.

### 6.4 — Non-Existent Database
```sql
USE fantasyland;
DROP DATABASE fantasyland;
```

### 6.5 — Special Characters in Values
```sql
INSERT INTO students VALUES (30, 'O''Brien', 25, 'A');
SELECT * FROM students WHERE name = 'O''Brien';
```

### 6.6 — Very Long Input
```sql
INSERT INTO students VALUES (31, 'AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA', 20, 'X');
SELECT * FROM students;
```

### 6.7 — Rapid Fire Queries (Rate Limit Test)
Send 50+ queries rapidly through the client to test the 1000/min rate limiter:
```sql
SELECT * FROM students;
SELECT * FROM students;
SELECT * FROM students;
SELECT * FROM students;
SELECT * FROM students;
SELECT * FROM students;
SELECT * FROM students;
SELECT * FROM students;
SELECT * FROM students;
SELECT * FROM students;
```
(Repeat — should NOT hit limit unless > 1000/min)

### 6.8 — Multi-Client Test
Open T2 and T3 both connected:
```
Terminal T2: LOGIN admin Admin@123
Terminal T3: LOGIN admin Admin@123
Terminal T2: INSERT INTO students VALUES (40, 'Client2', 40, 'T');
Terminal T3: SELECT * FROM students WHERE id = 40;
```
T3 should see the row inserted by T2.

### 6.9 — Client Disconnect While Server Runs
```
Terminal T2: \disconnect
Terminal T2: \reconnect
Terminal T2: LOGIN admin Admin@123
Terminal T2: SELECT * FROM students;
```

### 6.10 — Kill Client Abruptly
Close Terminal T3 window entirely (simulates crash).
Server should handle the disconnect gracefully (no crash).

### 6.11 — Server HELP
```sql
HELP
MADAD
?
```

---

## 🧾 QUICK COPY-PASTE FULL TEST SEQUENCE

### For CLI (inventixdb.exe):
```
./inventixdb
```
Login: `admin` / `Admin@123`
```sql
help
CREATE DATABASE testcli;
SHOW DATABASES;
USE testcli;
TABLE BANAO items (id INT PK, name TEXT, price INT);
SHOW TABLES;
DALO items MAAN (1, 'Laptop', 50000);
DALO items MAAN (2, 'Phone', 20000);
DALO items MAAN (3, 'Tablet', 30000);
DIKHAO * SE items;
BADLO items RAKHO_YEH price = 55000 JAHAN id = 1;
SELECT * FROM items WHERE id = 1;
NIKALO SE items JAHAN id = 3;
SELECT * FROM items;
SHURU;
DALO items MAAN (4, 'Watch', 10000);
WAPAS;
SELECT * FROM items WHERE id = 4;
BEGIN;
INSERT INTO items VALUES (5, 'Headphones', 5000);
COMMIT;
SELECT * FROM items;
RAKHO events {"type":"click","page":"home"};
RAKHO events {"type":"scroll","page":"about"};
MANGWAO events;
DHUNDO events JAHAN id=1;
HATAO events 2;
MANGWAO events;
CHECKPOINT;
GIRAO TABLE items;
SHOW TABLES;
USE public;
DATABASE HATAO testcli;
SHOW DATABASES;
DEKHO DATABASES;
exit
```

### For Server + Client:
**T1:** `./inventix-server`  
**T2:** `./inventix-client -p 8888`
```sql
LOGIN admin Admin@123
WHOAMI
STATUS
CREATE DATABASE servertest;
SHOW DATABASES;
USE servertest;
CREATE TABLE orders (id INT PRIMARY KEY, item TEXT, qty INT);
INSERT INTO orders VALUES (1, 'Widget', 100);
INSERT INTO orders VALUES (2, 'Gadget', 50);
SELECT * FROM orders;
UPDATE orders SET qty = 200 WHERE id = 1;
SELECT * FROM orders;
DELETE FROM orders WHERE id = 2;
SELECT * FROM orders;
SHOW TABLES;
PREPARE find_order AS SELECT * FROM orders WHERE id = ?;
EXECUTE find_order USING (1);
DEALLOCATE find_order;
CLUSTER
NODES
HELP
USE public;
DROP DATABASE servertest;
SHOW DATABASES;
LOGOUT
LOGIN admin wrong1
LOGIN admin wrong2
LOGIN admin wrong3
LOGIN admin wrong4
LOGIN admin wrong5
LOGIN admin Admin@123
quit
```

---

## ✅ EXPECTED RESULTS CHECKLIST

| # | Test Area | Pass Criteria |
|---|-----------|---------------|
| 1 | CLI Login | 3-attempt lockout, masked password, correct auth |
| 2 | Server Start | Binds port 8888, shows banner |
| 3 | Client Connect | Connects with `-p 8888`, shows prompt |
| 4 | LOGIN/LOGOUT | Auth works, WHOAMI reflects state |
| 5 | Brute-Force | 5 failures → IP locked 5 min |
| 6 | CREATE DATABASE | Creates DB, shows in SHOW DATABASES |
| 7 | DROP DATABASE | SQL + Hinglish (GIRAO/HATAO/DELETE) all work |
| 8 | SHOW DATABASES | SQL + DEKHO DATABASES show list |
| 9 | Cannot drop public | Error message |
| 10 | Cannot drop current DB | Error message |
| 11 | CREATE TABLE | SQL + Hinglish (BANAO), PK shorthand |
| 12 | DROP TABLE | SQL + Hinglish (GIRAO) |
| 13 | INSERT | SQL + Hinglish (DALO ... MAAN) |
| 14 | SELECT | SQL + Hinglish (DIKHAO ... SE ... JAHAN) |
| 15 | UPDATE | SQL + Hinglish (BADLO ... RAKHO_YEH ... JAHAN) |
| 16 | DELETE | SQL + Hinglish (NIKALO SE ... JAHAN) |
| 17 | Transactions | BEGIN/COMMIT, ROLLBACK, SAVEPOINT |
| 18 | Hinglish Txn | SHURU/PUKKA/WAPAS |
| 19 | NoSQL Docs | RAKHO, MANGWAO, DHUNDO, HATAO |
| 20 | Subqueries | Scalar subquery in WHERE |
| 21 | ALTER TABLE | ADD/DROP/RENAME column |
| 22 | Prepared Stmts | PREPARE/EXECUTE/DEALLOCATE |
| 23 | System Cmds | CHECKPOINT, USE, SHOW TABLES |
| 24 | Multi-Client | Both clients see same data |
| 25 | Client Crash | Server survives abrupt disconnect |
| 26 | Edge Cases | Empty table, dup PK, nonexistent table |
| 27 | Status Cmds | STATUS/HALAT/STHITI show security info |
| 28 | Help | CLI + Server both show updated help |
| 29 | CLUSTER/NODES | Returns cluster info (standalone mode) |
| 30 | File Execution | `-f test.sql` runs all statements |
