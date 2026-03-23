/*
 * control_ipc_win.c  --  Windows named pipe IPC implementation
 *
 * Uses Windows named pipes for local IPC:
 * - Server creates named pipe with CreateNamedPipe
 * - Client connects with CreateFile
 * - Synchronous read/write for simplicity
 */

#include "control_ipc.h"
#include <windows.h>
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

/* --------------------------------------------------------- error handling     */

const char *ipc_error_string(NcdIpcResult result) {
    switch (result) {
        case NCD_IPC_OK: return "Success";
        case NCD_IPC_ERROR_GENERIC: return "Generic error";
        case NCD_IPC_ERROR_INVALID: return "Invalid message";
        case NCD_IPC_ERROR_BUSY: return "Service busy";
        case NCD_IPC_ERROR_NOT_FOUND: return "Not found";
        default: return "Unknown error";
    }
}

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
        default:
            return NCD_IPC_ERROR_GENERIC;
    }
}

/* --------------------------------------------------------- naming             */

bool ipc_make_address(char *out_buf, size_t buf_size) {
    if (!out_buf || buf_size == 0) {
        return false;
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
            break;  /* Success */
        }
        
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PIPE_BUSY) {
            /* Pipe not ready yet, wait and retry */
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
    
    /* Set pipe to message mode */
    DWORD mode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(client->hPipe, &mode, NULL, NULL)) {
        CloseHandle(client->hPipe);
        free(client);
        return NULL;
    }
    
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

/* Helper: Send message and wait for response */
static NcdIpcResult send_receive(NcdIpcClient *client,
                                  NcdMessageType type,
                                  const void *payload,
                                  size_t payload_len,
                                  void **out_response,
                                  size_t *out_response_len) {
    if (!client || client->hPipe == INVALID_HANDLE_VALUE) {
        return NCD_IPC_ERROR_GENERIC;
    }
    
    /* Build message */
    uint8_t msg_buf[NCD_IPC_MAX_MSG_SIZE];
    if (sizeof(NcdIpcHeader) + payload_len > NCD_IPC_MAX_MSG_SIZE) {
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
    
    /* Send message */
    DWORD written;
    if (!WriteFile(client->hPipe, msg_buf, (DWORD)msg_len, &written, NULL)) {
        return win_error_to_ipc(GetLastError());
    }
    
    if (written != msg_len) {
        return NCD_IPC_ERROR_GENERIC;
    }
    
    /* Read response */
    uint8_t resp_buf[NCD_IPC_MAX_MSG_SIZE];
    DWORD read;
    if (!ReadFile(client->hPipe, resp_buf, NCD_IPC_MAX_MSG_SIZE, &read, NULL)) {
        return win_error_to_ipc(GetLastError());
    }
    
    if (read < sizeof(NcdIpcHeader)) {
        return NCD_IPC_ERROR_INVALID;
    }
    
    NcdIpcHeader *resp_hdr = (NcdIpcHeader *)resp_buf;
    
    /* Validate response */
    if (resp_hdr->magic != NCD_IPC_MAGIC ||
        resp_hdr->version != NCD_IPC_VERSION) {
        return NCD_IPC_ERROR_INVALID;
    }
    
    if (resp_hdr->type == NCD_MSG_ERROR) {
        if (resp_hdr->payload_len >= sizeof(NcdErrorPayload)) {
            NcdErrorPayload *err = (NcdErrorPayload *)(resp_buf + sizeof(NcdIpcHeader));
            return (NcdIpcResult)err->error_code;
        }
        return NCD_IPC_ERROR_GENERIC;
    }
    
    if (resp_hdr->type != NCD_MSG_RESPONSE) {
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
    
    return NCD_IPC_OK;
}

NcdIpcResult ipc_client_ping(NcdIpcClient *client) {
    return send_receive(client, NCD_MSG_PING, NULL, 0, NULL, NULL);
}

NcdIpcResult ipc_client_get_state_info(NcdIpcClient *client, NcdIpcStateInfo *info) {
    if (!info) {
        return NCD_IPC_ERROR_INVALID;
    }
    
    void *response = NULL;
    size_t response_len = 0;
    
    NcdIpcResult result = send_receive(client, NCD_MSG_GET_STATE_INFO,
                                        NULL, 0, &response, &response_len);
    
    if (result != NCD_IPC_OK) {
        return result;
    }
    
    if (!response || response_len < sizeof(NcdStateInfoPayload)) {
        free(response);
        return NCD_IPC_ERROR_INVALID;
    }
    
    NcdStateInfoPayload *payload = (NcdStateInfoPayload *)response;
    
    info->protocol_version = payload->protocol_version;
    info->meta_generation = payload->meta_generation;
    info->db_generation = payload->db_generation;
    info->meta_size = payload->meta_size;
    info->db_size = payload->db_size;
    
    /* Extract names from trailing data */
    char *data = (char *)response + sizeof(NcdStateInfoPayload);
    size_t remaining = response_len - sizeof(NcdStateInfoPayload);
    
    info->meta_name[0] = '\0';
    info->db_name[0] = '\0';
    
    if (payload->meta_name_len > 0 && payload->meta_name_len < remaining) {
        memcpy(info->meta_name, data, payload->meta_name_len);
        info->meta_name[payload->meta_name_len] = '\0';
        data += payload->meta_name_len;
        remaining -= payload->meta_name_len;
    }
    
    if (payload->db_name_len > 0 && payload->db_name_len <= remaining) {
        memcpy(info->db_name, data, payload->db_name_len);
        info->db_name[payload->db_name_len] = '\0';
    }
    
    free(response);
    return NCD_IPC_OK;
}

NcdIpcResult ipc_client_submit_heuristic(NcdIpcClient *client,
                                          const char *search,
                                          const char *target) {
    if (!search || !target) {
        return NCD_IPC_ERROR_INVALID;
    }
    
    size_t search_len = strlen(search) + 1;
    size_t target_len = strlen(target) + 1;
    
    if (search_len + target_len > NCD_IPC_MAX_MSG_SIZE - sizeof(NcdIpcHeader) - sizeof(NcdSubmitHeuristicPayload)) {
        return NCD_IPC_ERROR_INVALID;
    }
    
    uint8_t payload_buf[NCD_IPC_MAX_MSG_SIZE];
    NcdSubmitHeuristicPayload *payload = (NcdSubmitHeuristicPayload *)payload_buf;
    payload->search_len = (uint32_t)search_len;
    payload->target_len = (uint32_t)target_len;
    
    memcpy(payload_buf + sizeof(NcdSubmitHeuristicPayload), search, search_len);
    memcpy(payload_buf + sizeof(NcdSubmitHeuristicPayload) + search_len, target, target_len);
    
    size_t payload_size = sizeof(NcdSubmitHeuristicPayload) + search_len + target_len;
    
    return send_receive(client, NCD_MSG_SUBMIT_HEURISTIC,
                        payload_buf, payload_size, NULL, NULL);
}

NcdIpcResult ipc_client_submit_metadata(NcdIpcClient *client,
                                         int update_type,
                                         const void *data,
                                         size_t data_len) {
    if (data_len > NCD_IPC_MAX_MSG_SIZE - sizeof(NcdIpcHeader) - sizeof(NcdSubmitMetadataPayload)) {
        return NCD_IPC_ERROR_INVALID;
    }
    
    uint8_t payload_buf[NCD_IPC_MAX_MSG_SIZE];
    NcdSubmitMetadataPayload *payload = (NcdSubmitMetadataPayload *)payload_buf;
    payload->update_type = (uint32_t)update_type;
    payload->data_len = (uint32_t)data_len;
    
    if (data_len > 0 && data) {
        memcpy(payload_buf + sizeof(NcdSubmitMetadataPayload), data, data_len);
    }
    
    size_t payload_size = sizeof(NcdSubmitMetadataPayload) + data_len;
    
    return send_receive(client, NCD_MSG_SUBMIT_METADATA,
                        payload_buf, payload_size, NULL, NULL);
}

NcdIpcResult ipc_client_request_rescan(NcdIpcClient *client,
                                        const bool drive_mask[26],
                                        bool scan_root_only) {
    NcdRequestRescanPayload payload;
    
    payload.drive_mask = 0;
    for (int i = 0; i < 26; i++) {
        if (drive_mask[i]) {
            payload.drive_mask |= (1U << i);
        }
    }
    payload.scan_root_only = scan_root_only ? 1 : 0;
    memset(payload.reserved, 0, sizeof(payload.reserved));
    
    return send_receive(client, NCD_MSG_REQUEST_RESCAN,
                        &payload, sizeof(payload), NULL, NULL);
}

NcdIpcResult ipc_client_request_flush(NcdIpcClient *client) {
    return send_receive(client, NCD_MSG_REQUEST_FLUSH, NULL, 0, NULL, NULL);
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
        NULL                        /* Default security attributes */
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

int ipc_server_receive(NcdIpcConnection *conn, void **out_payload, size_t *out_len) {
    if (!conn || !out_payload || !out_len) {
        return 0;
    }
    
    *out_payload = NULL;
    *out_len = 0;
    
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
    
    return hdr->type;
}

NcdIpcResult ipc_server_send_response(NcdIpcConnection *conn,
                                       uint32_t sequence,
                                       const void *payload,
                                       size_t payload_len) {
    if (!conn || conn->hPipe == INVALID_HANDLE_VALUE) {
        return NCD_IPC_ERROR_GENERIC;
    }
    
    if (sizeof(NcdIpcHeader) + payload_len > NCD_IPC_MAX_MSG_SIZE) {
        return NCD_IPC_ERROR_INVALID;
    }
    
    uint8_t msg_buf[NCD_IPC_MAX_MSG_SIZE];
    NcdIpcHeader *hdr = (NcdIpcHeader *)msg_buf;
    hdr->magic = NCD_IPC_MAGIC;
    hdr->version = NCD_IPC_VERSION;
    hdr->type = NCD_MSG_RESPONSE;
    hdr->sequence = sequence;
    hdr->payload_len = (uint32_t)payload_len;
    
    if (payload_len > 0) {
        memcpy(msg_buf + sizeof(NcdIpcHeader), payload, payload_len);
    }
    
    size_t msg_len = sizeof(NcdIpcHeader) + payload_len;
    DWORD written;
    
    if (!WriteFile(conn->hPipe, msg_buf, (DWORD)msg_len, &written, NULL)) {
        return win_error_to_ipc(GetLastError());
    }
    
    return (written == msg_len) ? NCD_IPC_OK : NCD_IPC_ERROR_GENERIC;
}

NcdIpcResult ipc_server_send_error(NcdIpcConnection *conn,
                                    uint32_t sequence,
                                    NcdIpcResult error_code,
                                    const char *message) {
    if (!conn || conn->hPipe == INVALID_HANDLE_VALUE) {
        return NCD_IPC_ERROR_GENERIC;
    }
    
    size_t msg_len = strlen(message) + 1;
    if (sizeof(NcdIpcHeader) + sizeof(NcdErrorPayload) + msg_len > NCD_IPC_MAX_MSG_SIZE) {
        return NCD_IPC_ERROR_INVALID;
    }
    
    uint8_t msg_buf[NCD_IPC_MAX_MSG_SIZE];
    NcdIpcHeader *hdr = (NcdIpcHeader *)msg_buf;
    hdr->magic = NCD_IPC_MAGIC;
    hdr->version = NCD_IPC_VERSION;
    hdr->type = NCD_MSG_ERROR;
    hdr->sequence = sequence;
    hdr->payload_len = sizeof(NcdErrorPayload) + (uint32_t)msg_len;
    
    NcdErrorPayload *err = (NcdErrorPayload *)(msg_buf + sizeof(NcdIpcHeader));
    err->error_code = error_code;
    err->message_len = (uint32_t)msg_len;
    memcpy(msg_buf + sizeof(NcdIpcHeader) + sizeof(NcdErrorPayload), message, msg_len);
    
    size_t total_len = sizeof(NcdIpcHeader) + sizeof(NcdErrorPayload) + msg_len;
    DWORD written;
    
    if (!WriteFile(conn->hPipe, msg_buf, (DWORD)total_len, &written, NULL)) {
        return win_error_to_ipc(GetLastError());
    }
    
    return (written == total_len) ? NCD_IPC_OK : NCD_IPC_ERROR_GENERIC;
}

/* --------------------------------------------------------- utilities          */

void ipc_free_message(void *payload) {
    free(payload);
}

bool ipc_service_exists(void) {
    if (g_pipe_name[0] == '\0') {
        if (ipc_client_init() != 0) {
            return false;
        }
    }
    
    /* Try to connect with minimal timeout */
    HANDLE hPipe = CreateFile(
        g_pipe_name,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );
    
    if (hPipe == INVALID_HANDLE_VALUE) {
        return false;
    }
    
    CloseHandle(hPipe);
    return true;
}
