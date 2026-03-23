/*
 * control_ipc_posix.c  --  POSIX Unix domain socket IPC implementation
 *
 * Uses Unix domain sockets for local IPC:
 * - Server creates socket with socket/bind/listen
 * - Client connects with socket/connect
 * - Synchronous read/write for simplicity
 */

#include "control_ipc.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------- constants          */

#define NCD_SOCKET_NAME_FORMAT  "ncd_%s_control.sock"
#define NCD_SOCKET_TIMEOUT_SEC  5

/* --------------------------------------------------------- types              */

struct NcdIpcClient {
    int      fd;
    uint32_t sequence;
};

struct NcdIpcServer {
    int  fd;
    char sock_path[256];
};

struct NcdIpcConnection {
    int  fd;
    bool is_connected;
};

/* --------------------------------------------------------- globals            */

static char g_sock_path[256] = {0};

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

static NcdIpcResult errno_to_ipc(int err) {
    switch (err) {
        case 0:
            return NCD_IPC_OK;
        case ENOENT:
        case ECONNREFUSED:
            return NCD_IPC_ERROR_NOT_FOUND;
        case EBUSY:
        case EAGAIN:
            return NCD_IPC_ERROR_BUSY;
        case EINVAL:
        case EPROTO:
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
    
    /* Use XDG_RUNTIME_DIR if available, else /tmp */
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    const char *base_dir = runtime_dir ? runtime_dir : "/tmp";
    
    /* Get user ID */
    uid_t uid = getuid();
    
    /* Format: <runtime_dir>/ncd_<uid>_control.sock */
    int written = snprintf(out_buf, buf_size, "%s/ncd_%d_control.sock",
                          base_dir, (int)uid);
    
    return (written > 0 && (size_t)written < buf_size);
}

/* --------------------------------------------------------- client API         */

int ipc_client_init(void) {
    if (!ipc_make_address(g_sock_path, sizeof(g_sock_path))) {
        return -1;
    }
    return 0;
}

void ipc_client_cleanup(void) {
    /* Nothing to clean up */
}

NcdIpcClient *ipc_client_connect(void) {
    if (g_sock_path[0] == '\0') {
        if (ipc_client_init() != 0) {
            return NULL;
        }
    }
    
    NcdIpcClient *client = (NcdIpcClient *)calloc(1, sizeof(NcdIpcClient));
    if (!client) {
        return NULL;
    }
    
    /* Create socket */
    client->fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (client->fd < 0) {
        free(client);
        return NULL;
    }
    
    /* Set timeout */
    struct timeval tv;
    tv.tv_sec = NCD_SOCKET_TIMEOUT_SEC;
    tv.tv_usec = 0;
    setsockopt(client->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(client->fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    
    /* Connect to server */
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, g_sock_path, sizeof(addr.sun_path) - 1);
    
    if (connect(client->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(client->fd);
        free(client);
        return NULL;
    }
    
    return client;
}

void ipc_client_disconnect(NcdIpcClient *client) {
    if (!client) {
        return;
    }
    
    if (client->fd >= 0) {
        close(client->fd);
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
    if (!client || client->fd < 0) {
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
    ssize_t sent = send(client->fd, msg_buf, msg_len, 0);
    if (sent < 0 || (size_t)sent != msg_len) {
        return errno_to_ipc(errno);
    }
    
    /* Read response */
    uint8_t resp_buf[NCD_IPC_MAX_MSG_SIZE];
    ssize_t received = recv(client->fd, resp_buf, NCD_IPC_MAX_MSG_SIZE, 0);
    
    if (received < 0) {
        return errno_to_ipc(errno);
    }
    
    if ((size_t)received < sizeof(NcdIpcHeader)) {
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
    
    ipc_result = ipc_client_request_shutdown(client);
    if (ipc_result == NCD_IPC_OK) {
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
    if (g_sock_path[0] == '\0') {
        if (ipc_client_init() != 0) {
            return NULL;
        }
    }
    
    NcdIpcServer *server = (NcdIpcServer *)calloc(1, sizeof(NcdIpcServer));
    if (!server) {
        return NULL;
    }
    
    strncpy(server->sock_path, g_sock_path, sizeof(server->sock_path) - 1);
    server->sock_path[sizeof(server->sock_path) - 1] = '\0';
    
    /* Remove old socket if exists */
    unlink(server->sock_path);
    
    /* Create socket */
    server->fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server->fd < 0) {
        free(server);
        return NULL;
    }
    
    /* Bind to address */
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, server->sock_path, sizeof(addr.sun_path) - 1);
    
    if (bind(server->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(server->fd);
        free(server);
        return NULL;
    }
    
    /* Listen for connections */
    if (listen(server->fd, 5) < 0) {
        close(server->fd);
        unlink(server->sock_path);
        free(server);
        return NULL;
    }
    
    /* Set permissions (user only) */
    chmod(server->sock_path, 0600);
    
    return server;
}

void ipc_server_cleanup(NcdIpcServer *server) {
    if (!server) {
        return;
    }
    
    if (server->fd >= 0) {
        close(server->fd);
    }
    
    if (server->sock_path[0] != '\0') {
        unlink(server->sock_path);
    }
    
    free(server);
}

NcdIpcConnection *ipc_server_accept(NcdIpcServer *server, int timeout_ms) {
    if (!server || server->fd < 0) {
        return NULL;
    }
    
    /* Set timeout if specified */
    if (timeout_ms > 0) {
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        setsockopt(server->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }
    
    /* Accept connection */
    struct sockaddr_un addr;
    socklen_t addr_len = sizeof(addr);
    int client_fd = accept(server->fd, (struct sockaddr *)&addr, &addr_len);
    
    if (client_fd < 0) {
        return NULL;
    }
    
    /* Set socket timeout */
    struct timeval tv;
    tv.tv_sec = NCD_SOCKET_TIMEOUT_SEC;
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    
    NcdIpcConnection *conn = (NcdIpcConnection *)calloc(1, sizeof(NcdIpcConnection));
    if (!conn) {
        close(client_fd);
        return NULL;
    }
    
    conn->fd = client_fd;
    conn->is_connected = true;
    
    return conn;
}

void ipc_server_close_connection(NcdIpcConnection *conn) {
    if (!conn) {
        return;
    }
    
    if (conn->fd >= 0) {
        close(conn->fd);
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
    ssize_t received = recv(conn->fd, msg_buf, NCD_IPC_MAX_MSG_SIZE, 0);
    
    if (received < 0) {
        return 0;
    }
    
    if ((size_t)received < sizeof(NcdIpcHeader)) {
        return 0;
    }
    
    NcdIpcHeader *hdr = (NcdIpcHeader *)msg_buf;
    
    /* Validate header */
    if (hdr->magic != NCD_IPC_MAGIC || hdr->version != NCD_IPC_VERSION) {
        return 0;
    }
    
    /* Copy payload */
    if (hdr->payload_len > 0) {
        if (sizeof(NcdIpcHeader) + hdr->payload_len > (size_t)received) {
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
    if (!conn || conn->fd < 0) {
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
    ssize_t sent = send(conn->fd, msg_buf, msg_len, 0);
    
    if (sent < 0) {
        return errno_to_ipc(errno);
    }
    
    return (sent == (ssize_t)msg_len) ? NCD_IPC_OK : NCD_IPC_ERROR_GENERIC;
}

NcdIpcResult ipc_server_send_error(NcdIpcConnection *conn,
                                    uint32_t sequence,
                                    NcdIpcResult error_code,
                                    const char *message) {
    if (!conn || conn->fd < 0) {
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
    ssize_t sent = send(conn->fd, msg_buf, total_len, 0);
    
    if (sent < 0) {
        return errno_to_ipc(errno);
    }
    
    return (sent == (ssize_t)total_len) ? NCD_IPC_OK : NCD_IPC_ERROR_GENERIC;
}

NcdIpcResult ipc_server_send_version_mismatch(NcdIpcConnection *conn,
                                               uint32_t sequence,
                                               const char *client_version,
                                               const char *client_build,
                                               const char *service_version,
                                               const char *service_build,
                                               const char *message) {
    if (!conn || conn->fd < 0) {
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
    ssize_t sent = send(conn->fd, msg_buf, total_len, 0);
    
    if (sent < 0) {
        return errno_to_ipc(errno);
    }
    
    return (sent == (ssize_t)total_len) ? NCD_IPC_OK : NCD_IPC_ERROR_GENERIC;
}

/* --------------------------------------------------------- utilities          */

void ipc_free_message(void *payload) {
    free(payload);
}

bool ipc_service_exists(void) {
    if (g_sock_path[0] == '\0') {
        if (ipc_client_init() != 0) {
            return false;
        }
    }
    
    /* Try to connect with short timeout */
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }
    
    /* Set non-blocking and short timeout */
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, g_sock_path, sizeof(addr.sun_path) - 1);
    
    int result = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    close(fd);
    
    return (result == 0);
}
