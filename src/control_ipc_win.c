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

/* --------------------------------------------------------- error handling     */

const char *ipc_error_string(NcdIpcResult result) {
    switch (result) {
        case NCD_IPC_OK: return "Success";
        case NCD_IPC_ERROR_GENERIC: return "Generic error";
        case NCD_IPC_ERROR_INVALID: return "Invalid message";
        case NCD_IPC_ERROR_BUSY: return "Service busy";
        case NCD_IPC_ERROR_NOT_FOUND: return "Not found";
        case NCD_IPC_ERROR_BUSY_LOADING: return "Service loading databases";
        case NCD_IPC_ERROR_BUSY_SCANNING: return "Service scanning filesystem";
        case NCD_IPC_ERROR_NOT_READY: return "Service not ready";
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
        case ERROR_BROKEN_PIPE:
        case ERROR_PIPE_NOT_CONNECTED:
        case ERROR_NO_DATA:
            /* Pipe was closed by remote side - service may have shutdown */
            return NCD_IPC_ERROR_NOT_FOUND;
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

/* Debug logging for IPC (enabled via /agdb flag from client) */
static int g_ipc_debug_mode = 0;
void ipc_set_debug_mode(int enable) { g_ipc_debug_mode = enable; }
#define IPC_DBG(...) do { if (g_ipc_debug_mode) fprintf(stderr, __VA_ARGS__); } while(0)

/* Helper: Send message and wait for response with timeout */
static NcdIpcResult send_receive(NcdIpcClient *client,
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
    DWORD total_bytes = 0;
    DWORD bytes_left = 0;
    
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
    info->text_encoding = payload->text_encoding;
    info->meta_generation = payload->meta_generation;
    info->db_generation = payload->db_generation;
    info->meta_size = payload->meta_size;
    info->db_size = payload->db_size;
    
    /* Extract names from trailing data */
    char *data = (char *)response + sizeof(NcdStateInfoPayload);
    size_t remaining = response_len - sizeof(NcdStateInfoPayload);
    
    info->meta_name[0] = '\0';
    info->db_name[0] = '\0';

    if (payload->meta_name_len > sizeof(info->meta_name) - 1 ||
        payload->db_name_len > sizeof(info->db_name) - 1) {
        free(response);
        return NCD_IPC_ERROR_INVALID;
    }

    if ((size_t)payload->meta_name_len + (size_t)payload->db_name_len > remaining) {
        free(response);
        return NCD_IPC_ERROR_INVALID;
    }
    
    if (payload->meta_name_len > 0) {
        if (payload->meta_name_len > remaining) {
            free(response);
            return NCD_IPC_ERROR_INVALID;
        }
        if (data[payload->meta_name_len - 1] != '\0') {
            free(response);
            return NCD_IPC_ERROR_INVALID;
        }
        memcpy(info->meta_name, data, payload->meta_name_len);
        info->meta_name[payload->meta_name_len] = '\0';
        data += payload->meta_name_len;
        remaining -= payload->meta_name_len;
    }
    
    if (payload->db_name_len > 0) {
        if (payload->db_name_len > remaining) {
            free(response);
            return NCD_IPC_ERROR_INVALID;
        }
        if (data[payload->db_name_len - 1] != '\0') {
            free(response);
            return NCD_IPC_ERROR_INVALID;
        }
        memcpy(info->db_name, data, payload->db_name_len);
        info->db_name[payload->db_name_len] = '\0';
    }
    
    free(response);
    return NCD_IPC_OK;
}

NcdIpcResult ipc_client_get_detailed_status(NcdIpcClient *client, NcdIpcDetailedStatus *info) {
    if (!info) {
        return NCD_IPC_ERROR_INVALID;
    }
    
    void *response = NULL;
    size_t response_len = 0;
    
    NcdIpcResult result = send_receive(client, NCD_MSG_GET_DETAILED_STATUS,
                                        NULL, 0, &response, &response_len);
    
    if (result != NCD_IPC_OK) {
        return result;
    }
    
    if (!response || response_len < sizeof(NcdDetailedStatusPayload)) {
        free(response);
        return NCD_IPC_ERROR_INVALID;
    }
    
    NcdDetailedStatusPayload *payload = (NcdDetailedStatusPayload *)response;
    
    info->protocol_version = payload->protocol_version;
    info->runtime_state = payload->runtime_state;
    info->log_level = payload->log_level;
    info->pending_count = payload->pending_count;
    info->dirty_flags = payload->dirty_flags;
    info->meta_generation = payload->meta_generation;
    info->db_generation = payload->db_generation;
    info->drive_count = payload->drive_count;
    if (info->drive_count > NCD_IPC_MAX_DETAILED_DRIVES) {
        info->drive_count = NCD_IPC_MAX_DETAILED_DRIVES;
    }
    
    memcpy(info->app_version, payload->app_version, sizeof(info->app_version));
    info->app_version[sizeof(info->app_version) - 1] = '\0';
    memcpy(info->build_stamp, payload->build_stamp, sizeof(info->build_stamp));
    info->build_stamp[sizeof(info->build_stamp) - 1] = '\0';
    
    info->status_message[0] = '\0';
    info->meta_path[0] = '\0';
    info->log_path[0] = '\0';
    for (uint32_t i = 0; i < NCD_IPC_MAX_DETAILED_DRIVES; i++) {
        info->drives[i].letter = 0;
        info->drives[i].dir_count = 0;
        info->drives[i].db_path[0] = '\0';
    }
    
    char *data = (char *)response + sizeof(NcdDetailedStatusPayload);
    size_t remaining = response_len - sizeof(NcdDetailedStatusPayload);
    
    /* Parse status message */
    if (payload->status_msg_len > 0 && payload->status_msg_len <= remaining) {
        if (payload->status_msg_len <= sizeof(info->status_message)) {
            memcpy(info->status_message, data, payload->status_msg_len);
            info->status_message[payload->status_msg_len - 1] = '\0';
        }
        data += payload->status_msg_len;
        remaining -= payload->status_msg_len;
    }
    
    /* Parse metadata path */
    if (payload->meta_path_len > 0 && payload->meta_path_len <= remaining) {
        if (payload->meta_path_len <= sizeof(info->meta_path)) {
            memcpy(info->meta_path, data, payload->meta_path_len);
            info->meta_path[payload->meta_path_len - 1] = '\0';
        }
        data += payload->meta_path_len;
        remaining -= payload->meta_path_len;
    }
    
    /* Parse log path */
    if (payload->log_path_len > 0 && payload->log_path_len <= remaining) {
        if (payload->log_path_len <= sizeof(info->log_path)) {
            memcpy(info->log_path, data, payload->log_path_len);
            info->log_path[payload->log_path_len - 1] = '\0';
        }
        data += payload->log_path_len;
        remaining -= payload->log_path_len;
    }
    
    /* Parse drive infos */
    for (uint32_t i = 0; i < info->drive_count; i++) {
        if (remaining < sizeof(NcdDetailedStatusDriveHeader)) {
            break;
        }
        NcdDetailedStatusDriveHeader *drv = (NcdDetailedStatusDriveHeader *)data;
        info->drives[i].letter = drv->letter;
        info->drives[i].dir_count = drv->dir_count;
        data += sizeof(NcdDetailedStatusDriveHeader);
        remaining -= sizeof(NcdDetailedStatusDriveHeader);
        
        if (drv->db_path_len > 0 && drv->db_path_len <= remaining &&
            drv->db_path_len <= sizeof(info->drives[i].db_path)) {
            memcpy(info->drives[i].db_path, data, drv->db_path_len);
            info->drives[i].db_path[drv->db_path_len - 1] = '\0';
            data += drv->db_path_len;
            remaining -= drv->db_path_len;
        } else if (drv->db_path_len > 0 && drv->db_path_len <= remaining) {
            data += drv->db_path_len;
            remaining -= drv->db_path_len;
        }
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

NcdIpcResult ipc_client_get_version(NcdIpcClient *client, NcdIpcVersionInfo *info) {
    if (!info) {
        return NCD_IPC_ERROR_INVALID;
    }
    
    void *response = NULL;
    size_t response_len = 0;
    
    NcdIpcResult result = send_receive(client, NCD_MSG_GET_VERSION,
                                        NULL, 0, &response, &response_len);
    
    if (result != NCD_IPC_OK) {
        return result;
    }
    
    if (!response || response_len < sizeof(NcdVersionInfoPayload)) {
        free(response);
        return NCD_IPC_ERROR_INVALID;
    }
    
    NcdVersionInfoPayload *payload = (NcdVersionInfoPayload *)response;
    
    strncpy(info->app_version, payload->app_version, sizeof(info->app_version) - 1);
    info->app_version[sizeof(info->app_version) - 1] = '\0';
    
    strncpy(info->build_stamp, payload->build_stamp, sizeof(info->build_stamp) - 1);
    info->build_stamp[sizeof(info->build_stamp) - 1] = '\0';
    
    info->protocol_version = payload->protocol_version;
    
    free(response);
    return NCD_IPC_OK;
}

NcdIpcResult ipc_client_check_version(NcdIpcClient *client,
                                       const char *client_version,
                                       const char *client_build,
                                       NcdIpcVersionCheckResult *result) {
    if (!client_version || !client_build || !result) {
        return NCD_IPC_ERROR_INVALID;
    }
    
    memset(result, 0, sizeof(*result));
    
    /* Get service version */
    NcdIpcVersionInfo service_info;
    NcdIpcResult ipc_result = ipc_client_get_version(client, &service_info);
    
    if (ipc_result != NCD_IPC_OK) {
        snprintf(result->message, sizeof(result->message),
                 "Failed to get service version: %s", ipc_error_string(ipc_result));
        return ipc_result;
    }
    
    /* Store versions in result */
    strncpy(result->client_version, client_version, sizeof(result->client_version) - 1);
    strncpy(result->client_build, client_build, sizeof(result->client_build) - 1);
    strncpy(result->service_version, service_info.app_version, sizeof(result->service_version) - 1);
    strncpy(result->service_build, service_info.build_stamp, sizeof(result->service_build) - 1);
    
    /* Compare versions */
    if (strcmp(client_version, service_info.app_version) == 0) {
        result->versions_match = true;
        result->service_was_stopped = false;
        snprintf(result->message, sizeof(result->message),
                 "Versions match: %s", client_version);
        return NCD_IPC_OK;
    }
    
    /* Versions don't match - request service shutdown */
    result->versions_match = false;
    
    /* 
     * Send shutdown request - pipe may be closed by service after response.
     * This is expected behavior: service sends response then exits immediately.
     */
    ipc_result = ipc_client_request_shutdown(client);
    
    /* 
     * Consider shutdown successful if we got OK or if pipe was closed (service exited).
     * ERROR_PIPE_NOT_CONNECTED or ERROR_BROKEN_PIPE means service shut down.
     */
    if (ipc_result == NCD_IPC_OK || ipc_result == NCD_IPC_ERROR_NOT_FOUND) {
        result->service_was_stopped = true;
        snprintf(result->message, sizeof(result->message),
                 "Version mismatch detected. Client: %s (%s), Service: %s (%s). "
                 "Service has been gracefully stopped.",
                 client_version, client_build,
                 service_info.app_version, service_info.build_stamp);
    } else {
        result->service_was_stopped = false;
        snprintf(result->message, sizeof(result->message),
                 "Version mismatch detected. Client: %s (%s), Service: %s (%s). "
                 "Failed to stop service: %s",
                 client_version, client_build,
                 service_info.app_version, service_info.build_stamp,
                 ipc_error_string(ipc_result));
    }
    
    return NCD_IPC_ERROR_GENERIC;
}

NcdIpcResult ipc_client_request_shutdown(NcdIpcClient *client) {
    return send_receive(client, NCD_MSG_REQUEST_SHUTDOWN, NULL, 0, NULL, NULL);
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

NcdIpcResult ipc_server_send_version_mismatch(NcdIpcConnection *conn,
                                               uint32_t sequence,
                                               const char *client_version,
                                               const char *client_build,
                                               const char *service_version,
                                               const char *service_build,
                                               const char *message) {
    if (!conn || conn->hPipe == INVALID_HANDLE_VALUE) {
        return NCD_IPC_ERROR_GENERIC;
    }
    
    size_t msg_len = strlen(message) + 1;
    size_t payload_size = sizeof(NcdVersionMismatchPayload) + msg_len;
    
    if (sizeof(NcdIpcHeader) + payload_size > NCD_IPC_MAX_MSG_SIZE) {
        return NCD_IPC_ERROR_INVALID;
    }
    
    uint8_t msg_buf[NCD_IPC_MAX_MSG_SIZE];
    NcdIpcHeader *hdr = (NcdIpcHeader *)msg_buf;
    hdr->magic = NCD_IPC_MAGIC;
    hdr->version = NCD_IPC_VERSION;
    hdr->type = NCD_MSG_VERSION_MISMATCH;
    hdr->sequence = sequence;
    hdr->payload_len = (uint32_t)payload_size;
    
    NcdVersionMismatchPayload *vm = (NcdVersionMismatchPayload *)(msg_buf + sizeof(NcdIpcHeader));
    strncpy(vm->client_version, client_version, sizeof(vm->client_version) - 1);
    vm->client_version[sizeof(vm->client_version) - 1] = '\0';
    
    strncpy(vm->client_build, client_build, sizeof(vm->client_build) - 1);
    vm->client_build[sizeof(vm->client_build) - 1] = '\0';
    
    strncpy(vm->service_version, service_version, sizeof(vm->service_version) - 1);
    vm->service_version[sizeof(vm->service_version) - 1] = '\0';
    
    strncpy(vm->service_build, service_build, sizeof(vm->service_build) - 1);
    vm->service_build[sizeof(vm->service_build) - 1] = '\0';
    
    vm->message_len = (uint32_t)msg_len;
    memcpy(msg_buf + sizeof(NcdIpcHeader) + sizeof(NcdVersionMismatchPayload), message, msg_len);
    
    size_t total_len = sizeof(NcdIpcHeader) + payload_size;
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
        DWORD err = GetLastError();
        /* ERROR_PIPE_BUSY (231) means the pipe exists but all instances are busy.
         * This indicates the service IS running, just handling other clients. */
        if (err == ERROR_PIPE_BUSY) {
            return true;
        }
        return false;
    }
    
    CloseHandle(hPipe);
    return true;
}
