/*
 * control_ipc_posix.c  --  POSIX Unix domain socket IPC implementation
 *
 * Uses Unix domain sockets for local IPC:
 * - Server creates socket with socket/bind/listen
 * - Client connects with socket/connect
 * - Synchronous read/write for simplicity
 *
 * All payload serialization, client API wrappers, and server response
 * helpers live in control_ipc_common.c. This file contains only the
 * platform-specific I/O primitives and connection management.
 */

#include "control_ipc.h"
#include "control_ipc_common.h"
#include "ncd.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <poll.h>
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

/* --------------------------------------------------------- debug macros       */

#define IPC_DBG(...) do { if (g_ipc_debug_mode) fprintf(stderr, __VA_ARGS__); } while(0)

/* --------------------------------------------------------- error mapping      */

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
    
    /* System mode: use fixed socket path (no UID) */
    if (ncd_is_system_mode()) {
        size_t len = strlen(NCD_SYSTEM_SOCKET_PATH);
        if (len >= buf_size) return false;
        memcpy(out_buf, NCD_SYSTEM_SOCKET_PATH, len + 1);
        return true;
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

/* ================================================================
 * ipc_send_receive  --  Platform-specific send + receive loop
 *
 * Builds the header, sends the message over the socket, then reads
 * and validates the response. Called by the common wrappers in
 * control_ipc_common.c.
 */

NcdIpcResult ipc_send_receive(NcdIpcClient *client,
                              NcdMessageType type,
                              const void *payload,
                              size_t payload_len,
                              void **out_response,
                              size_t *out_response_len) {
    if (!client || client->fd < 0) {
        IPC_DBG("IPC: send_receive - invalid client or fd\n");
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
    ssize_t sent = send(client->fd, msg_buf, msg_len, 0);
    if (sent < 0 || (size_t)sent != msg_len) {
        IPC_DBG("IPC: send failed, errno=%d\n", errno);
        return errno_to_ipc(errno);
    }
    
    IPC_DBG("IPC: Message sent, waiting for response...\n");
    
    /* Set receive timeout to prevent indefinite hangs */
    struct timeval tv;
    tv.tv_sec = 5;  /* 5 second timeout */
    tv.tv_usec = 0;
    setsockopt(client->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    /* Read response */
    uint8_t resp_buf[NCD_IPC_MAX_MSG_SIZE];
    ssize_t received = recv(client->fd, resp_buf, NCD_IPC_MAX_MSG_SIZE, 0);
    
    if (received < 0) {
        IPC_DBG("IPC: recv failed, errno=%d\n", errno);
        return errno_to_ipc(errno);
    }
    
    IPC_DBG("IPC: recv succeeded, received=%zd bytes\n", received);
    
    if ((size_t)received < sizeof(NcdIpcHeader)) {
        IPC_DBG("IPC: Response too small, received=%zd\n", received);
        return NCD_IPC_ERROR_INVALID;
    }
    
    NcdIpcHeader *resp_hdr = (NcdIpcHeader *)resp_buf;
    size_t hdr_size = sizeof(NcdIpcHeader);
    size_t available_payload = (size_t)received - hdr_size;
    
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
 * Thin wrapper around send() used by the common server-response
 * helpers in control_ipc_common.c.
 */

NcdIpcResult ipc_platform_conn_write(NcdIpcConnection *conn,
                                     const void *data,
                                     size_t len) {
    if (!conn || conn->fd < 0) {
        return NCD_IPC_ERROR_GENERIC;
    }
    
    ssize_t sent = send(conn->fd, data, len, 0);
    if (sent < 0) {
        return errno_to_ipc(errno);
    }
    
    return (sent == (ssize_t)len) ? NCD_IPC_OK : NCD_IPC_ERROR_GENERIC;
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
    
    /* System mode: ensure socket directory exists with world-readable perms */
    if (ncd_is_system_mode()) {
        mkdir(NCD_SYSTEM_SOCKET_DIR, 0755);
        /* Ignore EEXIST - directory may already exist */
    }
    
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
    
    /* Set permissions: user-only in normal mode, world-accessible in system mode */
    chmod(server->sock_path, ncd_is_system_mode() ? 0666 : 0600);
    
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

int ipc_server_get_fd(NcdIpcServer *server) {
    if (!server || server->fd < 0) return -1;
    return server->fd;
}

int ipc_connection_get_fd(NcdIpcConnection *conn) {
    if (!conn || conn->fd < 0) return -1;
    return conn->fd;
}

NcdIpcConnection *ipc_server_accept(NcdIpcServer *server, int timeout_ms) {
    if (!server || server->fd < 0) {
        return NULL;
    }
    
    /* Use poll() for timeout instead of SO_RCVTIMEO to avoid corrupting
     * the server socket's timeout for subsequent poll()-based operations. */
    if (timeout_ms > 0) {
        struct pollfd pfd;
        pfd.fd = server->fd;
        pfd.events = POLLIN;
        int pr = poll(&pfd, 1, timeout_ms);
        if (pr <= 0) return NULL;
    }
    
    /* Accept connection */
    struct sockaddr_un addr;
    socklen_t addr_len = sizeof(addr);
    int client_fd = accept(server->fd, (struct sockaddr *)&addr, &addr_len);
    
    if (client_fd < 0) {
        return NULL;
    }
    
    /* Set a short receive timeout so we don't block indefinitely if the
     * client connects but never sends data. The service loop handles one
     * message per connection and closes immediately, so a 200ms timeout
     * is more than enough for a local Unix socket. */
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 200000;  /* 200ms */
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
    
    /* Return sequence number if requested */
    if (out_sequence) {
        *out_sequence = hdr->sequence;
    }
    
    return hdr->type;
}

/* --------------------------------------------------------- utilities          */

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
