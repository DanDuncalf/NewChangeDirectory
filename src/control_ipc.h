/*
 * control_ipc.h  --  Control channel IPC for NCD state service
 *
 * This header defines the protocol used for:
 * - Liveness checks (ping)
 * - Generation discovery
 * - Mutation requests (heuristics, metadata updates)
 * - Rescan and flush requests
 *
 * The protocol is intentionally simple and synchronous.
 */

#ifndef NCD_CONTROL_IPC_H
#define NCD_CONTROL_IPC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* --------------------------------------------------------- protocol constants */

/* Magic number for message validation */
#define NCD_IPC_MAGIC       0x434E4950U  /* 'N' 'C' 'I' 'P' = NCD IPC */

/* Protocol version */
#define NCD_IPC_VERSION     3   /* Added encoding mode support */

/* Application version - must match between client and service */
#define NCD_APP_VERSION     "1.3"

/* Maximum message size */
#define NCD_IPC_MAX_MSG_SIZE 4096

/* Maximum path length for IPC status payloads (kept independent of NCD_MAX_PATH) */
#define NCD_IPC_MAX_PATH 512

/* Default timeout for operations (milliseconds) */
#define NCD_IPC_TIMEOUT_MS  5000

/* --------------------------------------------------------- message types      */

typedef enum {
    /* Client -> Server */
    NCD_MSG_PING = 1,               /* Liveness check */
    NCD_MSG_GET_STATE_INFO,         /* Request current generation + names */
    NCD_MSG_GET_VERSION,            /* Request service version info */
    NCD_MSG_SUBMIT_HEURISTIC,       /* Submit heuristic update */
    NCD_MSG_SUBMIT_METADATA,        /* Submit metadata change */
    NCD_MSG_REQUEST_RESCAN,         /* Request database rescan */
    NCD_MSG_REQUEST_FLUSH,          /* Request immediate persistence */
    NCD_MSG_REQUEST_SHUTDOWN,       /* Request graceful service shutdown */
    NCD_MSG_GET_DETAILED_STATUS,    /* Request detailed service status */
    
    /* Server -> Client */
    NCD_MSG_RESPONSE = 0x80,        /* Response to client request */
    NCD_MSG_ERROR,                  /* Error response */
    NCD_MSG_VERSION_MISMATCH,       /* Version mismatch error */
} NcdMessageType;

/* --------------------------------------------------------- result codes       */

typedef enum {
    NCD_IPC_OK = 0,                 /* Success */
    NCD_IPC_ERROR_GENERIC = -1,    /* Generic error */
    NCD_IPC_ERROR_INVALID = -2,    /* Invalid message */
    NCD_IPC_ERROR_BUSY = -3,       /* Service busy */
    NCD_IPC_ERROR_NOT_FOUND = -4,  /* Resource not found */
    NCD_IPC_ERROR_BUSY_LOADING = -5,  /* Service loading databases */
    NCD_IPC_ERROR_BUSY_SCANNING = -6, /* Service scanning filesystem */
    NCD_IPC_ERROR_NOT_READY = -7,     /* Service not initialized yet */
} NcdIpcResult;

/* --------------------------------------------------------- message headers    */

/*
 * NcdIpcHeader  --  Common header for all messages
 */
typedef struct {
    uint32_t magic;                 /* NCD_IPC_MAGIC */
    uint16_t version;               /* NCD_IPC_VERSION */
    uint16_t type;                  /* NcdMessageType */
    uint32_t sequence;              /* Sequence number for correlation */
    uint32_t payload_len;           /* Length of payload after header */
} NcdIpcHeader;                     /* 16 bytes */

/* --------------------------------------------------------- request payloads   */

/*
 * NcdPingPayload  --  PING request (no payload data needed)
 */

/*
 * NcdGetStateInfoPayload  --  GET_STATE_INFO request (no payload data needed)
 */

/*
 * NcdSubmitHeuristicPayload  --  SUBMIT_HEURISTIC request
 */
typedef struct {
    uint32_t search_len;            /* Length of search term */
    uint32_t target_len;            /* Length of target path */
    /* Followed by: search_term \0 target_path \0 */
} NcdSubmitHeuristicPayload;

/* Metadata update types (for SUBMIT_METADATA) */
#define NCD_META_UPDATE_GROUP_ADD       1
#define NCD_META_UPDATE_GROUP_REMOVE    2
#define NCD_META_UPDATE_EXCLUSION_ADD   3
#define NCD_META_UPDATE_EXCLUSION_REMOVE 4
#define NCD_META_UPDATE_CONFIG          5
#define NCD_META_UPDATE_CLEAR_HISTORY   6
#define NCD_META_UPDATE_DIR_HISTORY_ADD 7
#define NCD_META_UPDATE_DIR_HISTORY_REMOVE 8  /* Remove by index */
#define NCD_META_UPDATE_DIR_HISTORY_SWAP   9  /* Swap first two entries (ping-pong) */
#define NCD_META_UPDATE_ENCODING_SWITCH 10 /* Encoding mode switch */

/*
 * NcdSubmitMetadataPayload  --  SUBMIT_METADATA request
 */
typedef struct {
    uint32_t update_type;           /* Type of update (see above) */
    uint32_t data_len;              /* Length of update data */
    /* Followed by: update_data (format depends on type) */
} NcdSubmitMetadataPayload;

/*
 * NcdRequestRescanPayload  --  REQUEST_RESCAN request
 */
typedef struct {
    uint32_t drive_mask;            /* Bitmask of drives to scan (bit 0 = A:) */
    uint8_t  scan_root_only;        /* Linux: scan only root */
    uint8_t  is_partial;            /* 1 = subdirectory scan (/r.), 0 = full rescan */
    uint8_t  reserved[2];
    /* For partial scan: path follows this structure (null-terminated string) */
} NcdRequestRescanPayload;

/*
 * NcdRequestFlushPayload  --  REQUEST_FLUSH request (no payload data needed)
 */

/* --------------------------------------------------------- response payloads  */

/*
 * NcdStateInfoPayload  --  Response to GET_STATE_INFO
 */
typedef struct {
    uint16_t protocol_version;      /* Service protocol version */
    uint16_t text_encoding;         /* NCD_TEXT_UTF8 or NCD_TEXT_UTF16LE */
    uint64_t meta_generation;       /* Metadata snapshot generation */
    uint64_t db_generation;         /* Database snapshot generation */
    uint32_t meta_size;             /* Metadata snapshot size */
    uint32_t db_size;               /* Database snapshot size */
    uint32_t meta_name_len;         /* Length of metadata shm name */
    uint32_t db_name_len;           /* Length of database shm name */
    /* Followed by: meta_name \0 db_name \0 */
} NcdStateInfoPayload;

/*
 * NcdDetailedStatusPayload  --  Response to GET_DETAILED_STATUS
 */
typedef struct {
    uint16_t protocol_version;      /* Service protocol version */
    uint16_t runtime_state;         /* ServiceRuntimeState value */
    int16_t  log_level;             /* Current logging level */
    uint8_t  reserved[2];           /* Padding for alignment */
    uint32_t pending_count;         /* Number of pending requests */
    uint32_t dirty_flags;           /* Current dirty flags */
    uint64_t meta_generation;       /* Metadata snapshot generation */
    uint64_t db_generation;         /* Database snapshot generation */
    uint32_t drive_count;           /* Number of drives in cache */
    uint32_t status_msg_len;        /* Length of status message string */
    uint32_t meta_path_len;         /* Length of metadata file path */
    uint32_t log_path_len;          /* Length of log file path */
    char     app_version[16];       /* Application version string */
    char     build_stamp[32];       /* Build timestamp */
    /* Followed by: status_msg \0 meta_path \0 log_path \0 drive_entries... */
} NcdDetailedStatusPayload;

/*
 * NcdDetailedStatusDriveHeader  --  Per-drive info in detailed status
 */
typedef struct {
    char     letter;                /* Drive letter */
    uint8_t  pad[3];                /* Padding for alignment */
    uint32_t dir_count;             /* Number of directories */
    uint32_t db_path_len;           /* Length of database file path */
    /* Followed by: db_path \0 */
} NcdDetailedStatusDriveHeader;

/*
 * NcdVersionInfoPayload  --  Response to GET_VERSION
 */
typedef struct {
    char     app_version[16];       /* Application version string (e.g., "1.3") */
    char     build_stamp[32];       /* Build timestamp (__DATE__ " " __TIME__) */
    uint16_t protocol_version;      /* IPC protocol version */
    uint16_t reserved;
} NcdVersionInfoPayload;

/*
 * NcdVersionMismatchPayload  --  Sent when versions don't match
 */
typedef struct {
    char     client_version[16];    /* Client's version */
    char     client_build[32];      /* Client's build stamp */
    char     service_version[16];   /* Service's version */
    char     service_build[32];     /* Service's build stamp */
    uint32_t message_len;           /* Length of error message */
    /* Followed by: message \0 */
} NcdVersionMismatchPayload;

/*
 * NcdErrorPayload  --  Error response
 */
typedef struct {
    int32_t  error_code;            /* NcdIpcResult */
    uint32_t message_len;           /* Length of error message */
    /* Followed by: error_message \0 */
} NcdErrorPayload;

/* --------------------------------------------------------- opaque handles     */

typedef struct NcdIpcClient NcdIpcClient;
typedef struct NcdIpcServer NcdIpcServer;

/* --------------------------------------------------------- client API         */

/*
 * ipc_client_init  --  Initialize IPC client subsystem
 */
int ipc_client_init(void);

/*
 * ipc_client_cleanup  --  Cleanup IPC client subsystem
 */
void ipc_client_cleanup(void);

/*
 * ipc_client_connect  --  Connect to the service
 *
 * Returns client handle on success, NULL on failure.
 * The handle must be freed with ipc_client_disconnect().
 */
NcdIpcClient *ipc_client_connect(void);

/*
 * ipc_client_disconnect  --  Disconnect from service
 */
void ipc_client_disconnect(NcdIpcClient *client);

/*
 * ipc_client_ping  --  Check if service is alive
 *
 * Returns NCD_IPC_OK if service responds.
 */
NcdIpcResult ipc_client_ping(NcdIpcClient *client);

/*
 * ipc_client_get_state_info  --  Get current state information
 *
 * Fills info structure with current generations and shared memory names.
 */
typedef struct {
    uint16_t protocol_version;
    uint16_t text_encoding;
    uint64_t meta_generation;
    uint64_t db_generation;
    uint32_t meta_size;
    uint32_t db_size;
    char     meta_name[256];
    char     db_name[256];
} NcdIpcStateInfo;

NcdIpcResult ipc_client_get_state_info(NcdIpcClient *client, NcdIpcStateInfo *info);

/*
 * ipc_client_get_detailed_status  --  Get detailed service status
 *
 * Fills info structure with runtime state, log level, cache contents,
 * file paths, and other diagnostic information.
 */
#define NCD_IPC_MAX_DETAILED_DRIVES 16

typedef struct {
    uint16_t protocol_version;
    uint16_t runtime_state;
    int16_t  log_level;
    uint32_t pending_count;
    uint32_t dirty_flags;
    uint64_t meta_generation;
    uint64_t db_generation;
    uint32_t drive_count;
    char     status_message[256];
    char     meta_path[NCD_IPC_MAX_PATH];
    char     log_path[NCD_IPC_MAX_PATH];
    char     app_version[16];
    char     build_stamp[32];
    struct {
        char     letter;
        uint32_t dir_count;
        char     db_path[NCD_IPC_MAX_PATH];
    } drives[NCD_IPC_MAX_DETAILED_DRIVES];
} NcdIpcDetailedStatus;

NcdIpcResult ipc_client_get_detailed_status(NcdIpcClient *client, NcdIpcDetailedStatus *info);

/*
 * ipc_client_submit_heuristic  --  Submit heuristic update
 */
NcdIpcResult ipc_client_submit_heuristic(NcdIpcClient *client,
                                          const char *search,
                                          const char *target);

/*
 * ipc_client_submit_metadata  --  Submit metadata change
 */
NcdIpcResult ipc_client_submit_metadata(NcdIpcClient *client,
                                         int update_type,
                                         const void *data,
                                         size_t data_len);

/*
 * ipc_client_request_rescan  --  Request database rescan
 */
NcdIpcResult ipc_client_request_rescan(NcdIpcClient *client,
                                        const bool drive_mask[26],
                                        bool scan_root_only);

/*
 * ipc_client_request_flush  --  Request immediate persistence
 */
NcdIpcResult ipc_client_request_flush(NcdIpcClient *client);

/*
 * ipc_client_get_version  --  Get service version information
 *
 * Fills info structure with service version and build info.
 * Returns NCD_IPC_OK on success, or NCD_IPC_ERROR_* on failure.
 */
typedef struct {
    char     app_version[16];
    char     build_stamp[32];
    uint16_t protocol_version;
} NcdIpcVersionInfo;

NcdIpcResult ipc_client_get_version(NcdIpcClient *client, NcdIpcVersionInfo *info);

/*
 * ipc_client_check_version  --  Check version compatibility and shutdown if mismatched
 *
 * Compares client version with service version. If they don't match,
 * requests graceful service shutdown and returns version mismatch info.
 * Returns NCD_IPC_OK if versions match.
 */
typedef struct {
    bool     versions_match;
    bool     service_was_stopped;
    char     client_version[16];
    char     client_build[32];
    char     service_version[16];
    char     service_build[32];
    char     message[256];
} NcdIpcVersionCheckResult;

NcdIpcResult ipc_client_check_version(NcdIpcClient *client,
                                       const char *client_version,
                                       const char *client_build,
                                       NcdIpcVersionCheckResult *result);

/*
 * ipc_client_request_shutdown  --  Request graceful service shutdown
 *
 * Asks the service to exit cleanly. The service will flush any
 * pending changes before exiting.
 */
NcdIpcResult ipc_client_request_shutdown(NcdIpcClient *client);

/* --------------------------------------------------------- server API         */

/*
 * ipc_server_init  --  Initialize IPC server
 *
 * Returns server handle on success, NULL on failure.
 */
NcdIpcServer *ipc_server_init(void);

/*
 * ipc_server_cleanup  --  Cleanup IPC server
 */
void ipc_server_cleanup(NcdIpcServer *server);

/*
 * ipc_server_accept  --  Accept a client connection
 *
 * Blocks until a client connects or timeout.
 * Returns client connection handle or NULL.
 */
typedef struct NcdIpcConnection NcdIpcConnection;

NcdIpcConnection *ipc_server_accept(NcdIpcServer *server, int timeout_ms);

/*
 * ipc_server_close_connection  --  Close a client connection
 */
void ipc_server_close_connection(NcdIpcConnection *conn);

/*
 * ipc_server_receive  --  Receive a message from client
 *
 * Returns message type or 0 on error/timeout.
 * Caller must free returned payload with ipc_free_message().
 * If out_sequence is not NULL, the sequence number from the header is stored there.
 */
int ipc_server_receive(NcdIpcConnection *conn, void **out_payload, size_t *out_len, uint32_t *out_sequence);

/*
 * ipc_server_send_response  --  Send response to client
 */
NcdIpcResult ipc_server_send_response(NcdIpcConnection *conn,
                                       uint32_t sequence,
                                       const void *payload,
                                       size_t payload_len);

/*
 * ipc_server_send_error  --  Send error response to client
 */
NcdIpcResult ipc_server_send_error(NcdIpcConnection *conn,
                                    uint32_t sequence,
                                    NcdIpcResult error_code,
                                    const char *message);

/*
 * ipc_server_send_version_mismatch  --  Send version mismatch response
 *
 * Sent by service when it detects a version mismatch with the client.
 */
NcdIpcResult ipc_server_send_version_mismatch(NcdIpcConnection *conn,
                                               uint32_t sequence,
                                               const char *client_version,
                                               const char *client_build,
                                               const char *service_version,
                                               const char *service_build,
                                               const char *message);

/* --------------------------------------------------------- utilities          */

/*
 * ipc_free_message  --  Free a message payload
 */
void ipc_free_message(void *payload);

/*
 * ipc_error_string  --  Get human-readable error string
 */
const char *ipc_error_string(NcdIpcResult result);

/*
 * ipc_make_address  --  Get platform-specific IPC address/path
 */
bool ipc_make_address(char *out_buf, size_t buf_size);

/*
 * ipc_service_exists  --  Check if service is running
 *
 * Quick check without full connect handshake.
 */
bool ipc_service_exists(void);

/*
 * ipc_set_debug_mode  --  Enable/disable IPC debug logging
 */
void ipc_set_debug_mode(int enable);

#ifdef __cplusplus
}
#endif

#endif /* NCD_CONTROL_IPC_H */
