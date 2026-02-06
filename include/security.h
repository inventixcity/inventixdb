/**
 * InventixDB Security Module
 * 
 * Implements:
 * - bcrypt password hashing
 * - Role-Based Access Control (RBAC)
 * - Session management with tokens
 * - TLS encryption support
 * - AES encryption for data at rest
 */

#ifndef INVENTIX_SECURITY_H
#define INVENTIX_SECURITY_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// ============================================================================
// CONSTANTS
// ============================================================================

#define SECURITY_MAX_USERS          256
#define SECURITY_MAX_ROLES          32
#define SECURITY_MAX_SESSIONS       1024
#define SECURITY_MAX_PERMISSIONS    64

#define SECURITY_SALT_LENGTH        16
#define SECURITY_HASH_LENGTH        64
#define SECURITY_TOKEN_LENGTH       64
#define SECURITY_AES_KEY_LENGTH     32    // AES-256
#define SECURITY_AES_IV_LENGTH      16

#define SECURITY_BCRYPT_COST        12    // 2^12 iterations
#define SECURITY_SESSION_TIMEOUT    3600  // 1 hour default

#define SECURITY_USERNAME_MAX       64
#define SECURITY_PASSWORD_MAX       128
#define SECURITY_ROLE_NAME_MAX      32

// ============================================================================
// PERMISSIONS (Bit flags)
// ============================================================================

typedef enum {
    PERM_NONE           = 0,
    
    // Database operations
    PERM_CREATE_DB      = (1 << 0),
    PERM_DROP_DB        = (1 << 1),
    PERM_USE_DB         = (1 << 2),
    
    // Table operations
    PERM_CREATE_TABLE   = (1 << 3),
    PERM_DROP_TABLE     = (1 << 4),
    PERM_ALTER_TABLE    = (1 << 5),
    
    // Data operations
    PERM_SELECT         = (1 << 6),
    PERM_INSERT         = (1 << 7),
    PERM_UPDATE         = (1 << 8),
    PERM_DELETE         = (1 << 9),
    
    // User management
    PERM_CREATE_USER    = (1 << 10),
    PERM_DROP_USER      = (1 << 11),
    PERM_GRANT          = (1 << 12),
    PERM_REVOKE         = (1 << 13),
    
    // System operations
    PERM_SHUTDOWN       = (1 << 14),
    PERM_RELOAD_CONFIG  = (1 << 15),
    PERM_VIEW_STATS     = (1 << 16),
    PERM_BACKUP         = (1 << 17),
    PERM_RESTORE        = (1 << 18),
    
    // Transaction control
    PERM_TRANSACTION    = (1 << 19),
    
    // All permissions
    PERM_ALL            = 0xFFFFFFFF
} Permission;

// ============================================================================
// ROLE DEFINITIONS
// ============================================================================

typedef struct {
    char name[SECURITY_ROLE_NAME_MAX];
    uint32_t permissions;
    bool is_system_role;    // Cannot be deleted
    char description[128];
} Role;

// Predefined roles
#define ROLE_SUPERADMIN     "superadmin"
#define ROLE_ADMIN          "admin"
#define ROLE_DEVELOPER      "developer"
#define ROLE_ANALYST        "analyst"
#define ROLE_READONLY       "readonly"
#define ROLE_GUEST          "guest"

// ============================================================================
// USER STRUCTURE
// ============================================================================

typedef enum {
    USER_STATUS_ACTIVE,
    USER_STATUS_LOCKED,
    USER_STATUS_DISABLED,
    USER_STATUS_PENDING
} UserStatus;

typedef struct {
    uint32_t user_id;
    char username[SECURITY_USERNAME_MAX];
    char password_hash[SECURITY_HASH_LENGTH * 2 + 1];  // Hex encoded
    char salt[SECURITY_SALT_LENGTH * 2 + 1];           // Hex encoded
    
    char roles[8][SECURITY_ROLE_NAME_MAX];  // Up to 8 roles per user
    int role_count;
    
    UserStatus status;
    time_t created_at;
    time_t last_login;
    int failed_attempts;
    time_t lockout_until;
    
    char email[128];
    char full_name[128];
    
    bool is_superuser;
    bool must_change_password;
} User;

// ============================================================================
// SESSION STRUCTURE
// ============================================================================

typedef struct {
    char token[SECURITY_TOKEN_LENGTH + 1];
    uint32_t user_id;
    char username[SECURITY_USERNAME_MAX];
    uint32_t permissions;       // Cached permissions
    
    time_t created_at;
    time_t last_activity;
    time_t expires_at;
    
    char client_ip[64];
    int client_port;
    char current_db[64];
    
    bool is_authenticated;
    bool is_superuser;
} Session;

// ============================================================================
// SECURITY CONTEXT
// ============================================================================

typedef struct {
    // Users
    User users[SECURITY_MAX_USERS];
    int user_count;
    
    // Roles
    Role roles[SECURITY_MAX_ROLES];
    int role_count;
    
    // Active sessions
    Session sessions[SECURITY_MAX_SESSIONS];
    int session_count;
    
    // Configuration
    int bcrypt_cost;
    int session_timeout;
    int max_failed_attempts;
    int lockout_duration;
    int password_min_length;
    bool require_uppercase;
    bool require_lowercase;
    bool require_digit;
    bool require_special;
    
    // Encryption keys (for TLS)
    char server_cert_path[256];
    char server_key_path[256];
    char ca_cert_path[256];
    bool tls_enabled;
    
    // AES key for data at rest
    uint8_t aes_key[SECURITY_AES_KEY_LENGTH];
    bool encryption_enabled;
    
    // Lock for thread safety
    void *lock;
    
    bool initialized;
} SecurityContext;

// Global security context
extern SecurityContext *g_security;

// ============================================================================
// BCRYPT PASSWORD HASHING
// ============================================================================

/**
 * Hash a password using bcrypt-like algorithm
 * @param password Plain text password
 * @param salt Output: generated salt (hex encoded)
 * @param hash Output: password hash (hex encoded)
 * @return 0 on success, -1 on error
 */
int security_hash_password(const char *password, char *salt, char *hash);

/**
 * Verify a password against stored hash
 * @param password Plain text password to verify
 * @param salt Stored salt (hex encoded)
 * @param hash Stored hash (hex encoded)
 * @return true if password matches, false otherwise
 */
bool security_verify_password(const char *password, const char *salt, const char *hash);

/**
 * Validate password strength
 * @param password Password to validate
 * @param error_msg Buffer for error message
 * @return true if password meets requirements
 */
bool security_validate_password(const char *password, char *error_msg);

// ============================================================================
// USER MANAGEMENT
// ============================================================================

/**
 * Initialize security system
 */
int security_init(void);

/**
 * Shutdown security system
 */
void security_shutdown(void);

/**
 * Create a new user
 * @param username Username
 * @param password Plain text password
 * @param role Initial role
 * @param error_msg Buffer for error message
 * @return User ID on success, -1 on error
 */
int security_create_user(const char *username, const char *password, 
                         const char *role, char *error_msg);

/**
 * Delete a user
 * @param username Username to delete
 * @return 0 on success, -1 on error
 */
int security_delete_user(const char *username);

/**
 * Find user by username
 * @param username Username to find
 * @return Pointer to user or NULL
 */
User* security_find_user(const char *username);

/**
 * Update user password
 * @param username Username
 * @param new_password New plain text password
 * @return 0 on success, -1 on error
 */
int security_change_password(const char *username, const char *new_password);

/**
 * Lock user account
 */
int security_lock_user(const char *username);

/**
 * Unlock user account
 */
int security_unlock_user(const char *username);

// ============================================================================
// ROLE MANAGEMENT
// ============================================================================

/**
 * Initialize predefined roles
 */
void security_init_roles(void);

/**
 * Create a custom role
 * @param name Role name
 * @param permissions Permission bitmask
 * @param description Role description
 * @return 0 on success, -1 on error
 */
int security_create_role(const char *name, uint32_t permissions, const char *description);

/**
 * Delete a role
 */
int security_delete_role(const char *name);

/**
 * Find role by name
 */
Role* security_find_role(const char *name);

/**
 * Grant role to user
 */
int security_grant_role(const char *username, const char *role);

/**
 * Revoke role from user
 */
int security_revoke_role(const char *username, const char *role);

/**
 * Get user's combined permissions (from all roles)
 */
uint32_t security_get_user_permissions(const char *username);

/**
 * Check if user has specific permission
 */
bool security_user_has_permission(const char *username, Permission perm);

// ============================================================================
// SESSION MANAGEMENT
// ============================================================================

/**
 * Authenticate user and create session
 * @param username Username
 * @param password Plain text password
 * @param client_ip Client IP address
 * @param client_port Client port
 * @param session Output: created session
 * @param error_msg Buffer for error message
 * @return 0 on success, -1 on error
 */
int security_login(const char *username, const char *password,
                   const char *client_ip, int client_port,
                   Session **session, char *error_msg);

/**
 * End a session
 * @param token Session token
 */
void security_logout(const char *token);

/**
 * Find session by token
 */
Session* security_find_session(const char *token);

/**
 * Validate session is still active
 */
bool security_validate_session(const char *token);

/**
 * Update session activity timestamp
 */
void security_touch_session(const char *token);

/**
 * Cleanup expired sessions
 */
int security_cleanup_sessions(void);

/**
 * Check if session has permission
 */
bool security_session_has_permission(Session *session, Permission perm);

// ============================================================================
// ENCRYPTION (TLS & AES)
// ============================================================================

/**
 * Initialize TLS context for server
 * @param cert_path Path to server certificate
 * @param key_path Path to server private key
 * @return 0 on success, -1 on error
 */
int security_init_tls(const char *cert_path, const char *key_path);

/**
 * Generate random bytes
 * @param buffer Output buffer
 * @param length Number of bytes to generate
 */
void security_random_bytes(uint8_t *buffer, size_t length);

/**
 * Generate random token
 * @param token Output buffer (must be at least SECURITY_TOKEN_LENGTH + 1)
 */
void security_generate_token(char *token);

/**
 * AES-256 encrypt data
 * @param plaintext Input data
 * @param plaintext_len Length of input
 * @param key Encryption key (32 bytes)
 * @param iv Initialization vector (16 bytes)
 * @param ciphertext Output buffer
 * @return Length of ciphertext or -1 on error
 */
int security_aes_encrypt(const uint8_t *plaintext, size_t plaintext_len,
                         const uint8_t *key, const uint8_t *iv,
                         uint8_t *ciphertext);

/**
 * AES-256 decrypt data
 */
int security_aes_decrypt(const uint8_t *ciphertext, size_t ciphertext_len,
                         const uint8_t *key, const uint8_t *iv,
                         uint8_t *plaintext);

// ============================================================================
// AUTHORIZATION HELPERS
// ============================================================================

/**
 * Check permission for SQL command type
 * @param session Active session
 * @param cmd_type Command type string (SELECT, INSERT, etc.)
 * @return true if allowed
 */
bool security_authorize_command(Session *session, const char *cmd_type);

/**
 * Check permission for database access
 */
bool security_authorize_database(Session *session, const char *database);

/**
 * Check permission for table access
 */
bool security_authorize_table(Session *session, const char *table, Permission required);

// ============================================================================
// AUDIT LOGGING
// ============================================================================

/**
 * Log security event
 */
void security_audit_log(const char *event_type, const char *username,
                        const char *client_ip, const char *details);

// ============================================================================
// PERSISTENCE
// ============================================================================

/**
 * Save security data to file
 */
int security_save(const char *filepath);

/**
 * Load security data from file
 */
int security_load(const char *filepath);

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

/**
 * Get permission name from flag
 */
const char* security_permission_name(Permission perm);

/**
 * Get role permissions as string
 */
void security_permissions_to_string(uint32_t perms, char *buffer, size_t size);

/**
 * Parse permission string to flags
 */
uint32_t security_parse_permissions(const char *perm_string);

// ============================================================================
// HINGLISH ALIASES
// ============================================================================

// Hinglish aliases for security functions
#define suraksha_shuru          security_init
#define suraksha_band           security_shutdown
#define upyogkarta_banao        security_create_user
#define upyogkarta_hatao        security_delete_user
#define upyogkarta_khojo        security_find_user
#define password_badlo          security_change_password
#define login_karo              security_login
#define logout_karo             security_logout
#define anumati_check           security_authorize_command

#endif // INVENTIX_SECURITY_H
