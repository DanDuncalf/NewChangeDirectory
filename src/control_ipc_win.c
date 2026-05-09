/*
 * control_ipc_win.c  --  Windows named pipe IPC implementation
 *
 * Uses Windows named pipes for local IPC:
 * - Server creates named pipe with CreateNamedPipe
 * - Client connects with CreateFile
 * - Synchronous read/write for simplicity
 *
 * All payload serialization, client API wrappers, and server response
 * helpers live in control_ipc_common.c. This file contains only the
 * platform-specific I/O primitives and connection management.
 */

#include "control_ipc.h"
#include "control_ipc_common.h"
#include "ncd.h"
#include <windows.h>
#include <sddl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------- constants          */

#define NCD_PIPE_NAME_FORMAT    "\\\\.\\pipe\\NCD_%s_CONTROL"
#define NCD_PIPE_BUFFER_SIZE    4096
#define NCD_PIPE_TIMEOUT_MS     5000

/* --------------------------------------------------------- types              */

struct NcdIpcClient {
    HANDLE hPipe;
    uint32_t sequence;
};

struct NcdIpcServer {
    HANDLE hPipe;
    char   pipe_name[256];
};

struct NcdIpcConnection {
    HANDLE hPipe;
    bool   is_connected;
};

/* --------------------------------------------------------- globals            */

static char g_pipe_name[256] = {0};

/* --------------------------------------------------------- error mapping      */

static NcdIpcResult win_error_to_ipc(DWORD err) {
    switch (err) {
        case ERROR_SUCCESS:
            return NCD_IPC_OK;
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
            return NCD_IPC_ERROR_NOT_FOUND;
        case ERROR_PIPE_BUSY:
        case ERROR_LOCK_VIOLATION:
            return NCD_IPC_ERROR_BUSY;
        case ERROR_INVALID_PARAMETER:
        case ERROR_BAD_FORMAT:
            return NCD_IPC_ERROR_INVALID;
        case ERROR_BROKEN_PIPE:
        case ERROR_PIPE_NOT_CONNECTED:
        case ERROR_NO_DATA:
            /* Pipe was closed by remote side - service may have shutdown */
            return NCD_IPC_ERROR_NOT_FOUND;
        default:
            return NCD_IPC_ERROR_GENERIC;
    }
}

/* --------------------------------------------------------- debug macros       */

#define IPC_DBG(...) do { if (g_ipc_debug_mode) fprintf(stderr, __VA_ARGS__); } while(0)

/* --------------------------------------------------------- naming             */

bool ipc_make_address(char *out_buf, size_t buf_size) {
    if (!out_buf || buf_size == 0) {
        return false;
    }
    
    /* System mode: use fixed pipe name (no SID) */
    if (ncd_is_system_mode()) {
        size_t len = strlen(NCD_SYSTEM_PIPE_NAME);
        if (len >= buf_size) return false;
        memcpy(out_buf, NCD_SYSTEM_PIPE_NAME, len + 1);
        return true;
    }
    
    /* Use session ID for scoping */
    DWORD session_id = WTSGetActiveConsoleSessionId();
    if (session_id == 0xFFFFFFFF) {
        session_id = 0;
    }
    
    /* Get user SID for per-user naming */
    HANDLE hToken = NULL;
    char sid_string[256] = {0};
    
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        DWORD dwLength = 0;
        GetTokenInformation(hToken, TokenUser, NULL, 0, &dwLength);
        
        if (dwLength > 0) {
            PTOKEN_USER pTokenUser = (PTOKEN_USER)malloc(dwLength);
            if (pTokenUser && GetTokenInformation(hToken, TokenUser, 
                                                  pTokenUser, dwLength, &dwLength)) {
                LPSTR sidStr = NULL;
                if (ConvertSidToStringSidA(pTokenUser->User.Sid, &sidStr)) {
                    strncpy(sid_string, sidStr, sizeof(sid_string) - 1);
                    LocalFree(sidStr);
                }
            }
            free(pTokenUser);
        }
        CloseHandle(hToken);
    }
    
    /* Fallback to session ID */
    if (sid_string[0] == '\0') {
        snprintf(sid_string, sizeof(sid_string), "S-%lu", session_id);
    }
    
    int written = snprintf(out_buf, buf_size, NCD_PIPE_NAME_FORMAT, sid_string);
    return (written > 0 && (size_t)written < buf_size);
}

/* --------------------------------------------------------- client API         */

int ipc_client_init(void) {
    if (!ipc_make_address(g_pipe_name, sizeof(g_pipe_name))) {
        return -1;
    }
    return 0;
}

void ipc_client_cleanup(void) {
    /* Nothing to clean up */
}

NcdIpcClient *ipc_client_connect(void) {
    if (g_pipe_name[0] == '\0') {
        if (ipc_client_init() != 0) {
            return NULL;
        }
    }
    
    NcdIpcClient *client = (NcdIpcClient *)calloc(1, sizeof(NcdIpcClient));
    if (!client) {
        return NULL;
    }
    
    /* Try to connect to named pipe with retries */
    HANDLE hPipe = INVALID_HANDLE_VALUE;
    int retries = 50;  /* 50 * 20ms = 1 second total */
    
    while (retries > 0) {
        hPipe = CreateFile(
            g_pipe_name,            /* Pipe name */
            GENERIC_READ |          /* Read and write access */
            GENERIC_WRITE,
            0,                      /* No sharing */
            NULL,                   /* Default security attributes */
            OPEN_EXISTING,          /* Opens existing pipe */
            0,                      /* Default attributes */
            NULL                    /* No template file */
        );
        
        if (hPipe != INVALID_HANDLE_VALUE) {
            /* Set pipe to message mode */
            DWORD mode = PIPE_READMODE_MESSAGE;
            if (SetNamedPipeHandleState(hPipe, &mode, NULL, NULL)) {
                break;  /* Success */
            }
            /* Server closed the pipe between CreateFile and SetNamedPipeHandleState */
            CloseHandle(hPipe);
            hPipe = INVALID_HANDLE_VALUE;
            Sleep(20);
            retries--;
            continue;
        }
        
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PIPE_BUSY || err == ERROR_BAD_PIPE) {
            /* Pipe not ready yet or server closed it; wait and retry */
            Sleep(20);
            retries--;
            continue;
        }
        
        /* Other error, fail immediately */
        free(client);
        return NULL;
    }
    
    if (hPipe == INVALID_HANDLE_VALUE) {
        free(client);
        return NULL;
    }
    
    client->hPipe = hPipe;
    
    return client;
}

void ipc_client_disconnect(NcdIpcClient *client) {
    if (!client) {
        return;
    }
    
    if (client->hPipe != INVALID_HANDLE_VALUE) {
        CloseHandle(client->hPipe);
    }
    
    free(client);
}

/* ================================================================
 * ipc_send_receive  --  Platform-specific send + receive loop
 *
 * Builds the header, sends the message over the named pipe, then
 * reads and validates the response. Called by the common wrappers
 * in control_ipc_common.c.
 */

NcdIpcResult ipc_send_receive(NcdIpcClient *client,
                              NcdMessageType type,
                              const void *payload,
                              size_t payload_len,
                              void **out_response,
                              size_t *out_response_len) {
    if (!client || client->hPipe == INVALID_HANDLE_VALUE) {
        IPC_DBG("IPC: send_receive - invalid client or pipe\n");
        return NCD_IPC_ERROR_GENERIC;
    }
    
    /* Build message */
    uint8_t msg_buf[NCD_IPC_MAX_MSG_SIZE];
    if (sizeof(NcdIpcHeader) + payload_len > NCD_IPC_MAX_MSG_SIZE) {
        IPC_DBG("IPC: send_receive - message too large\n");
        return NCD_IPC_ERROR_INVALID;
    }
    
    NcdIpcHeader *hdr = (NcdIpcHeader *)msg_buf;
    hdr->magic = NCD_IPC_MAGIC;
    hdr->version = NCD_IPC_VERSION;
    hdr->type = (uint16_t)type;
    hdr->sequence = ++client->sequence;
    hdr->payload_len = (uint32_t)payload_len;
    
    if (payload_len > 0) {
        memcpy(msg_buf + sizeof(NcdIpcHeader), payload, payload_len);
    }
    
    size_t msg_len = sizeof(NcdIpcHeader) + payload_len;
    
    IPC_DBG("IPC: Sending msg type=%d seq=%u len=%zu\n", type, hdr->sequence, msg_len);
    
    /* Send message */
    DWORD written;
    if (!WriteFile(client->hPipe, msg_buf, (DWORD)msg_len, &written, NULL)) {
        DWORD err = GetLastError();
        IPC_DBG("IPC: WriteFile failed, error=%lu\n", err);
        return win_error_to_ipc(err);
    }
    
    if (written != msg_len) {
        IPC_DBG("IPC: WriteFile incomplete, written=%lu expected=%zu\n", written, msg_len);
        return NCD_IPC_ERROR_GENERIC;
    }
    
    IPC_DBG("IPC: Message sent, waiting for response...\n");
    
    /* Read response with timeout to handle service shutdown scenario */
    uint8_t resp_buf[NCD_IPC_MAX_MSG_SIZE];
    DWORD read = 0;
    BOOL read_result;
    
    /* Use PeekNamedPipe first to check if data is available without blocking */
    DWORD bytes_available = 0;
    
    /* Wait up to 5 seconds for response (prevents indefinite hang on service shutdown) */
    int timeout_ms = 5000;
    int waited_ms = 0;
    
    while (waited_ms < timeout_ms) {
        if (PeekNamedPipe(client->hPipe, NULL, 0, NULL, &bytes_available, NULL)) {
            if (bytes_available >= sizeof(NcdIpcHeader)) {
                /* Data is available, read it */
                break;
            }
        } else {
            DWORD err = GetLastError();
            if (err == ERROR_BROKEN_PIPE || err == ERROR_PIPE_NOT_CONNECTED) {
                IPC_DBG("IPC: Pipe broken, service may have shutdown\n");
                return NCD_IPC_OK;  /* Service shutdown is expected for REQUEST_SHUTDOWN */
            }
        }
        Sleep(10);
        waited_ms += 10;
    }
    
    if (waited_ms >= timeout_ms) {
        IPC_DBG("IPC: Read timeout waiting for response\n");
        return NCD_IPC_ERROR_GENERIC;
    }
    
    read_result = ReadFile(client->hPipe, resp_buf, NCD_IPC_MAX_MSG_SIZE, &read, NULL);
    if (!read_result) {
        DWORD err = GetLastError();
        IPC_DBG("IPC: ReadFile failed, error=%lu\n", err);
        if (err == ERROR_BROKEN_PIPE || err == ERROR_PIPE_NOT_CONNECTED) {
            /* Service closed pipe - this is expected after REQUEST_SHUTDOWN */
            if (type == NCD_MSG_REQUEST_SHUTDOWN) {
                return NCD_IPC_OK;
            }
        }
        return win_error_to_ipc(err);
    }
    
    IPC_DBG("IPC: ReadFile succeeded, read=%lu bytes\n", read);
    
    if (read < sizeof(NcdIpcHeader)) {
        IPC_DBG("IPC: Response too small, read=%lu\n", read);
        return NCD_IPC_ERROR_INVALID;
    }
    
    NcdIpcHeader *resp_hdr = (NcdIpcHeader *)resp_buf;
    size_t hdr_size = sizeof(NcdIpcHeader);
    size_t available_payload = (size_t)read - hdr_size;
    
    IPC_DBG("IPC: Response header: magic=%X version=%d type=%d seq=%u payload=%u\n",
            resp_hdr->magic, resp_hdr->version, resp_hdr->type, resp_hdr->sequence, resp_hdr->payload_len);
    
    /* Validate response */
    if (resp_hdr->magic != NCD_IPC_MAGIC ||
        resp_hdr->version != NCD_IPC_VERSION) {
        IPC_DBG("IPC: Invalid response header magic/version\n");
        return NCD_IPC_ERROR_INVALID;
    }
    
    if (resp_hdr->type == NCD_MSG_ERROR) {
        if (resp_hdr->payload_len >= sizeof(NcdErrorPayload)) {
            NcdErrorPayload *err = (NcdErrorPayload *)(resp_buf + sizeof(NcdIpcHeader));
            IPC_DBG("IPC: Received error response, code=%d\n", err->error_code);
            return (NcdIpcResult)err->error_code;
        }
        IPC_DBG("IPC: Received error response (no payload)\n");
        return NCD_IPC_ERROR_GENERIC;
    }
    
    if (resp_hdr->type != NCD_MSG_RESPONSE) {
        IPC_DBG("IPC: Unexpected response type=%d (expected RESPONSE=%d)\n", resp_hdr->type, NCD_MSG_RESPONSE);
        return NCD_IPC_ERROR_INVALID;
    }
    
    /* Check sequence number matches */
    if (resp_hdr->sequence != hdr->sequence) {
        IPC_DBG("IPC: Sequence mismatch, sent=%u received=%u\n", hdr->sequence, resp_hdr->sequence);
        return NCD_IPC_ERROR_INVALID;
    }
    if (resp_hdr->payload_len > (uint32_t)(NCD_IPC_MAX_MSG_SIZE - hdr_size)) {
        IPC_DBG("IPC: Payload length too large: %u\n", resp_hdr->payload_len);
        return NCD_IPC_ERROR_INVALID;
    }
    if ((size_t)resp_hdr->payload_len > available_payload) {
        IPC_DBG("IPC: Payload length exceeds received bytes: payload=%u available=%zu\n",
                resp_hdr->payload_len, available_payload);
        return NCD_IPC_ERROR_INVALID;
    }
    
    /* Return response payload */
    if (out_response && out_response_len) {
        size_t resp_payload_len = resp_hdr->payload_len;
        if (resp_payload_len > 0) {
            void *resp_payload = malloc(resp_payload_len);
            if (!resp_payload) {
                return NCD_IPC_ERROR_GENERIC;
            }
            memcpy(resp_payload, resp_buf + sizeof(NcdIpcHeader), resp_payload_len);
            *out_response = resp_payload;
            *out_response_len = resp_payload_len;
        } else {
            *out_response = NULL;
            *out_response_len = 0;
        }
    }
    
    IPC_DBG("IPC: Request completed successfully\n");
    return NCD_IPC_OK;
}

/* ================================================================
 * ipc_platform_conn_write  --  Write to server-side connection
 *
 * Thin wrapper around WriteFile used by the common server-response
 * helpers in control_ipc_common.c.
 */

NcdIpcResult ipc_platform_conn_write(NcdIpcConnection *conn,
                                     const void *data,
                                     size_t len) {
    if (!conn || conn->hPipe == INVALID_HANDLE_VALUE) {
        return NCD_IPC_ERROR_GENERIC;
    }
    
    DWORD written;
    if (!WriteFile(conn->hPipe, data, (DWORD)len, &written, NULL)) {
        return win_error_to_ipc(GetLastError());
    }
    
    return (written == len) ? NCD_IPC_OK : NCD_IPC_ERROR_GENERIC;
}

/* --------------------------------------------------------- server API         */

NcdIpcServer *ipc_server_init(void) {
    if (g_pipe_name[0] == '\0') {
        if (ipc_client_init() != 0) {
            return NULL;
        }
    }
    
    NcdIpcServer *server = (NcdIpcServer *)calloc(1, sizeof(NcdIpcServer));
    if (!server) {
        return NULL;
    }
    
    strncpy(server->pipe_name, g_pipe_name, sizeof(server->pipe_name) - 1);
    server->pipe_name[sizeof(server->pipe_name) - 1] = '\0';
    
    return server;
}

void ipc_server_cleanup(NcdIpcServer *server) {
    if (!server) {
        return;
    }
    
    if (server->hPipe != INVALID_HANDLE_VALUE) {
        CloseHandle(server->hPipe);
    }
    
    free(server);
}

NcdIpcConnection *ipc_server_accept(NcdIpcServer *server, int timeout_ms) {
    if (!server) {
        return NULL;
    }
    
    /* Build security attributes for pipe creation */
    SECURITY_ATTRIBUTES sa;
    SECURITY_DESCRIPTOR sd;
    LPSECURITY_ATTRIBUTES psa = NULL;
    
    if (ncd_is_system_mode()) {
        /* System mode: NULL DACL allows all users to connect */
        InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
        SetSecurityDescriptorDacl(&sd, TRUE, NULL, FALSE);
        sa.nLength = sizeof(SECURITY_ATTRIBUTES);
        sa.lpSecurityDescriptor = &sd;
        sa.bInheritHandle = FALSE;
        psa = &sa;
    }
    
    /* Create named pipe */
    HANDLE hPipe = CreateNamedPipe(
        server->pipe_name,          /* Pipe name */
        PIPE_ACCESS_DUPLEX,         /* Read/write access */
        PIPE_TYPE_MESSAGE |         /* Message-type pipe */
        PIPE_READMODE_MESSAGE |     /* Message-read mode */
        PIPE_WAIT,                  /* Blocking mode */
        PIPE_UNLIMITED_INSTANCES,   /* Max instances */
        NCD_PIPE_BUFFER_SIZE,       /* Output buffer size */
        NCD_PIPE_BUFFER_SIZE,       /* Input buffer size */
        (timeout_ms > 0) ? timeout_ms : NCD_PIPE_TIMEOUT_MS, /* Timeout */
        psa                         /* Security attributes */
    );
    
    if (hPipe == INVALID_HANDLE_VALUE) {
        return NULL;
    }
    
    /* Wait for client connection */
    BOOL connected = ConnectNamedPipe(hPipe, NULL);
    if (!connected && GetLastError() != ERROR_PIPE_CONNECTED) {
        CloseHandle(hPipe);
        return NULL;
    }
    
    NcdIpcConnection *conn = (NcdIpcConnection *)calloc(1, sizeof(NcdIpcConnection));
    if (!conn) {
        CloseHandle(hPipe);
        return NULL;
    }
    
    conn->hPipe = hPipe;
    conn->is_connected = true;
    
    return conn;
}

void ipc_server_close_connection(NcdIpcConnection *conn) {
    if (!conn) {
        return;
    }
    
    if (conn->hPipe != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(conn->hPipe);
        DisconnectNamedPipe(conn->hPipe);
        CloseHandle(conn->hPipe);
    }
    
    free(conn);
}

int ipc_server_receive(NcdIpcConnection *conn, void **out_payload, size_t *out_len, uint32_t *out_sequence) {
    if (!conn || !out_payload || !out_len) {
        return 0;
    }
    
    *out_payload = NULL;
    *out_len = 0;
    if (out_sequence) {
        *out_sequence = 0;
    }
    
    uint8_t msg_buf[NCD_IPC_MAX_MSG_SIZE];
    DWORD read;
    
    if (!ReadFile(conn->hPipe, msg_buf, NCD_IPC_MAX_MSG_SIZE, &read, NULL)) {
        return 0;
    }
    
    if (read < sizeof(NcdIpcHeader)) {
        return 0;
    }
    
    NcdIpcHeader *hdr = (NcdIpcHeader *)msg_buf;
    
    /* Validate header */
    if (hdr->magic != NCD_IPC_MAGIC || hdr->version != NCD_IPC_VERSION) {
        return 0;
    }
    
    /* Copy payload */
    if (hdr->payload_len > 0) {
        if (sizeof(NcdIpcHeader) + hdr->payload_len > read) {
            return 0;
        }
        
        void *payload = malloc(hdr->payload_len);
        if (!payload) {
            return 0;
        }
        
        memcpy(payload, msg_buf + sizeof(NcdIpcHeader), hdr->payload_len);
        *out_payload = payload;
        *out_len = hdr->payload_len;
    }
    
    /* Return sequence number if requested */
    if (out_sequence) {
        *out_sequence = hdr->sequence;
    }
    
    return hdr->type;
}

/* --------------------------------------------------------- utilities          */

bool ipc_service_exists(void) {
    if (g_pipe_name[0] == '\0') {
        if (ipc_client_init() != 0) {
            return false;
        }
    }
    
    /* Try to connect with retries to handle transient unavailability
     * when the service is between pipe instances. */
    int retries = 5;  /* 5 * 20ms = 100ms total */
    while (retries > 0) {
        HANDLE hPipe = CreateFile(
            g_pipe_name,
            GENERIC_READ | GENERIC_WRITE,
            0,
            NULL,
            OPEN_EXISTING,
            0,
            NULL
        );
        
        if (hPipe != INVALID_HANDLE_VALUE) {
            CloseHandle(hPipe);
            return true;
        }
        
        DWORD err = GetLastError();
        /* ERROR_PIPE_BUSY (231) means the pipe exists but all instances are busy.
         * This indicates the service IS running, just handling other clients. */
        if (err == ERROR_PIPE_BUSY) {
            return true;
        }
        
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_BAD_PIPE) {
            /* Service may be recreating the pipe; wait briefly and retry */
            Sleep(20);
            retries--;
            continue;
        }
        
        break;
    }
    
    /* Fallback: check the service mutex. The pipe may be temporarily
     * unavailable while the service is between connections, but the
     * mutex remains owned until the process exits. An abandoned mutex
     * means the service crashed and should not be treated as running. */
    const char *mutex_name = ncd_is_system_mode() 
        ? "Global\\NCDService_Instance_7D3F9A2E"
        : "NCDService_Instance_7D3F9A2E";
    HANDLE hMutex = OpenMutexA(SYNCHRONIZE, FALSE, mutex_name);
    if (hMutex) {
        DWORD waitResult = WaitForSingleObject(hMutex, 0);
        CloseHandle(hMutex);
        if (waitResult == WAIT_TIMEOUT) {
            /* Mutex is owned by a live service process */
            return true;
        }
        /* WAIT_ABANDONED_0 (service crashed) or WAIT_OBJECT_0 (unowned) */
    }
    
    return false;
}
