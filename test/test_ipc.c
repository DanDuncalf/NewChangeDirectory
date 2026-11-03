/* test_ipc.c -- Tests for IPC layer (Tier 4) */
#include "test_framework.h"
#include "../src/control_ipc.h"
#include "../src/ncd.h"
#include "../src/platform.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#if NCD_PLATFORM_WINDOWS
#include <windows.h>
#else
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#if !NCD_PLATFORM_WINDOWS
typedef struct {
    char socket_path[256];
    uint8_t response[NCD_IPC_MAX_MSG_SIZE];
    size_t response_len;
} FakeIpcServerCtx;

static void *fake_ipc_server_thread(void *arg) {
    FakeIpcServerCtx *ctx = (FakeIpcServerCtx *)arg;
    int fd = -1;
    int conn = -1;

    unlink(ctx->socket_path);

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return NULL;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, ctx->socket_path, sizeof(addr.sun_path) - 1);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return NULL;
    }
    if (listen(fd, 1) != 0) {
        close(fd);
        unlink(ctx->socket_path);
        return NULL;
    }

    conn = accept(fd, NULL, NULL);
    if (conn >= 0) {
        uint8_t req_buf[128];
        (void)recv(conn, req_buf, sizeof(req_buf), 0);
        (void)send(conn, ctx->response, ctx->response_len, 0);
        close(conn);
    }

    close(fd);
    unlink(ctx->socket_path);
    return NULL;
}

static bool run_fake_state_info_call(const uint8_t *response, size_t response_len, NcdIpcResult *out_result) {
    if (!response || !out_result || response_len == 0 || response_len > NCD_IPC_MAX_MSG_SIZE) {
        return false;
    }

    char sock_path[256];
    if (!ipc_make_address(sock_path, sizeof(sock_path))) {
        return false;
    }

    FakeIpcServerCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    strncpy(ctx.socket_path, sock_path, sizeof(ctx.socket_path) - 1);
    memcpy(ctx.response, response, response_len);
    ctx.response_len = response_len;

    pthread_t tid;
    if (pthread_create(&tid, NULL, fake_ipc_server_thread, &ctx) != 0) {
        return false;
    }

    usleep(100000); /* Allow fake server to bind/listen */

    if (ipc_client_init() != 0) {
        pthread_join(tid, NULL);
        return false;
    }

    NcdIpcClient *client = ipc_client_connect();
    if (!client) {
        ipc_client_cleanup();
        pthread_join(tid, NULL);
        return false;
    }

    NcdIpcStateInfo info;
    *out_result = ipc_client_get_state_info(client, &info);

    ipc_client_disconnect(client);
    ipc_client_cleanup();
    pthread_join(tid, NULL);
    return true;
}
#endif

/* ------------------------------------------------------------------------
 * Scripted fake server: accepts one client, reads its request, then writes
 * a scripted list of chunks (each optionally preceded by a delay).  Used to
 * reproduce what the real service does after accepting a rescan: it pushes
 * RESCAN_COMPLETE on the same connection, which can land ahead of the reply
 * to the client's next request.  On POSIX the socket is a byte stream, so a
 * chunk may also be part of a message; on Windows each chunk is one pipe
 * message.
 * ------------------------------------------------------------------------ */
#define FAKE_MAX_CHUNKS 4

typedef struct {
    char address[256];
    uint8_t data[FAKE_MAX_CHUNKS][NCD_IPC_MAX_MSG_SIZE];
    size_t len[FAKE_MAX_CHUNKS];
    int delay_ms[FAKE_MAX_CHUNKS];
    int count;
    volatile int ready;
} ScriptedServerCtx;

static size_t make_frame(uint8_t *buf, uint16_t type, uint32_t seq,
                         const void *payload, uint32_t payload_len) {
    NcdIpcHeader *hdr = (NcdIpcHeader *)buf;
    hdr->magic = NCD_IPC_MAGIC;
    hdr->version = NCD_IPC_VERSION;
    hdr->type = type;
    hdr->sequence = seq;
    hdr->payload_len = payload_len;
    if (payload_len) memcpy(buf + sizeof(NcdIpcHeader), payload, payload_len);
    return sizeof(NcdIpcHeader) + payload_len;
}

static void script_add(ScriptedServerCtx *ctx, const uint8_t *bytes, size_t len, int delay_ms) {
    int i = ctx->count++;
    memcpy(ctx->data[i], bytes, len);
    ctx->len[i] = len;
    ctx->delay_ms[i] = delay_ms;
}

#if NCD_PLATFORM_WINDOWS
static DWORD WINAPI scripted_server_thread(LPVOID arg) {
    ScriptedServerCtx *ctx = (ScriptedServerCtx *)arg;
    HANDLE pipe = CreateNamedPipeA(ctx->address, PIPE_ACCESS_DUPLEX,
                                   PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                                   1, NCD_IPC_MAX_MSG_SIZE, NCD_IPC_MAX_MSG_SIZE, 0, NULL);
    if (pipe == INVALID_HANDLE_VALUE) { ctx->ready = -1; return 0; }
    ctx->ready = 1;
    BOOL ok = ConnectNamedPipe(pipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED;
    if (ok) {
        uint8_t req[NCD_IPC_MAX_MSG_SIZE];
        DWORD n = 0;
        (void)ReadFile(pipe, req, sizeof(req), &n, NULL);
        for (int i = 0; i < ctx->count; i++) {
            if (ctx->delay_ms[i] > 0) Sleep((DWORD)ctx->delay_ms[i]);
            DWORD w = 0;
            (void)WriteFile(pipe, ctx->data[i], (DWORD)ctx->len[i], &w, NULL);
        }
        FlushFileBuffers(pipe);
        Sleep(200);   /* let the client read before the pipe goes away */
        DisconnectNamedPipe(pipe);
    }
    CloseHandle(pipe);
    return 0;
}
#else
static void *scripted_server_thread(void *arg) {
    ScriptedServerCtx *ctx = (ScriptedServerCtx *)arg;
    unlink(ctx->address);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { ctx->ready = -1; return NULL; }
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, ctx->address, sizeof(addr.sun_path) - 1);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(fd, 1) != 0) {
        close(fd);
        ctx->ready = -1;
        return NULL;
    }
    ctx->ready = 1;
    int conn = accept(fd, NULL, NULL);
    if (conn >= 0) {
        uint8_t req[NCD_IPC_MAX_MSG_SIZE];
        (void)recv(conn, req, sizeof(req), 0);
        for (int i = 0; i < ctx->count; i++) {
            if (ctx->delay_ms[i] > 0) usleep((useconds_t)ctx->delay_ms[i] * 1000);
            (void)send(conn, ctx->data[i], ctx->len[i], 0);
        }
        usleep(200000);
        close(conn);
    }
    close(fd);
    unlink(ctx->address);
    return NULL;
}
#endif

/* Runs ipc_client_ping() against the scripted server.  Returns false if the
 * fake server could not be set up. */
static bool run_scripted_ping(ScriptedServerCtx *ctx, NcdIpcResult *out_result) {
    if (!ipc_make_address(ctx->address, sizeof(ctx->address))) return false;
    ctx->ready = 0;
#if NCD_PLATFORM_WINDOWS
    HANDLE th = CreateThread(NULL, 0, scripted_server_thread, ctx, 0, NULL);
    if (!th) return false;
#else
    pthread_t th;
    if (pthread_create(&th, NULL, scripted_server_thread, ctx) != 0) return false;
#endif
    for (int i = 0; i < 200 && ctx->ready == 0; i++) platform_sleep_ms(10);

    bool ok = false;
    if (ctx->ready == 1 && ipc_client_init() == 0) {
        NcdIpcClient *client = ipc_client_connect();
        if (client) {
            *out_result = ipc_client_ping(client);
            ipc_client_disconnect(client);
            ok = true;
        }
        ipc_client_cleanup();
    }
#if NCD_PLATFORM_WINDOWS
    WaitForSingleObject(th, 10000);
    CloseHandle(th);
#else
    pthread_join(th, NULL);
#endif
    return ok;
}

/* ================================================================ Tier 4: IPC Tests */

/* Regression: after accepting a rescan the service pushes RESCAN_COMPLETE on
 * the connection.  If it is queued ahead of the reply to the next request,
 * the client used to take it as the reply and fail that request. */
TEST(ipc_reply_skips_queued_rescan_complete) {
    if (ipc_service_exists()) {
        SKIP_TEST("Service is running; fake server test requires free IPC path");
    }
    static ScriptedServerCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    uint8_t frame[NCD_IPC_MAX_MSG_SIZE];
    size_t n = make_frame(frame, NCD_MSG_RESCAN_COMPLETE, 0, NULL, 0);
    script_add(&ctx, frame, n, 0);
    n = make_frame(frame, NCD_MSG_RESPONSE, 1, NULL, 0);
    script_add(&ctx, frame, n, 50);

    NcdIpcResult result = NCD_IPC_ERROR_GENERIC;
    ASSERT_TRUE(run_scripted_ping(&ctx, &result));
    ASSERT_EQ_INT(NCD_IPC_OK, result);
    return 0;
}

/* Progress notifications are skipped the same way, including several in a row. */
TEST(ipc_reply_skips_queued_rescan_progress) {
    if (ipc_service_exists()) {
        SKIP_TEST("Service is running; fake server test requires free IPC path");
    }
    static ScriptedServerCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    NcdRescanProgressPayload prog;
    memset(&prog, 0, sizeof(prog));
    uint8_t frame[NCD_IPC_MAX_MSG_SIZE];
    size_t n = make_frame(frame, NCD_MSG_RESCAN_PROGRESS, 0, &prog, (uint32_t)sizeof(prog));
    script_add(&ctx, frame, n, 0);
    script_add(&ctx, frame, n, 0);
    n = make_frame(frame, NCD_MSG_RESCAN_COMPLETE, 0, NULL, 0);
    script_add(&ctx, frame, n, 0);
    n = make_frame(frame, NCD_MSG_RESPONSE, 1, NULL, 0);
    script_add(&ctx, frame, n, 0);

    NcdIpcResult result = NCD_IPC_ERROR_GENERIC;
    ASSERT_TRUE(run_scripted_ping(&ctx, &result));
    ASSERT_EQ_INT(NCD_IPC_OK, result);
    return 0;
}

#if !NCD_PLATFORM_WINDOWS
/* Stream framing: a notification and the reply arriving in ONE send() must
 * still be read as two messages. */
TEST(ipc_reply_framed_when_coalesced_with_notification) {
    if (ipc_service_exists()) {
        SKIP_TEST("Service is running; fake server test requires free IPC path");
    }
    static ScriptedServerCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    uint8_t both[NCD_IPC_MAX_MSG_SIZE];
    size_t a = make_frame(both, NCD_MSG_RESCAN_COMPLETE, 0, NULL, 0);
    size_t b = make_frame(both + a, NCD_MSG_RESPONSE, 1, NULL, 0);
    script_add(&ctx, both, a + b, 0);

    NcdIpcResult result = NCD_IPC_ERROR_GENERIC;
    ASSERT_TRUE(run_scripted_ping(&ctx, &result));
    ASSERT_EQ_INT(NCD_IPC_OK, result);
    return 0;
}

/* Stream framing: a reply split across two send()s (header delivered in two
 * pieces with a pause between) must be reassembled, not rejected as short. */
TEST(ipc_reply_reassembled_when_split_across_sends) {
    if (ipc_service_exists()) {
        SKIP_TEST("Service is running; fake server test requires free IPC path");
    }
    static ScriptedServerCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    uint8_t frame[NCD_IPC_MAX_MSG_SIZE];
    size_t n = make_frame(frame, NCD_MSG_RESPONSE, 1, NULL, 0);
    script_add(&ctx, frame, 6, 0);
    script_add(&ctx, frame + 6, n - 6, 100);

    NcdIpcResult result = NCD_IPC_ERROR_GENERIC;
    ASSERT_TRUE(run_scripted_ping(&ctx, &result));
    ASSERT_EQ_INT(NCD_IPC_OK, result);
    return 0;
}
#endif

TEST(ipc_client_init_succeeds) {
    int result = ipc_client_init();
    
    ASSERT_EQ_INT(0, result);
    
    ipc_client_cleanup();
    return 0;
}

TEST(ipc_client_connect_fails_when_no_service) {
    int result = ipc_client_init();
    ASSERT_EQ_INT(0, result);

    if (ipc_service_exists()) {
SKIP_TEST("Service is running, connect-fail test expects no service");
    }
    
    /* Try to connect when no service is running */
    NcdIpcClient *client = ipc_client_connect();
    
    /* Should fail since service is not running */
    ASSERT_NULL(client);
    
    ipc_client_cleanup();
    return 0;
}

TEST(ipc_make_address_returns_valid_path) {
    char path[256];
    bool result = ipc_make_address(path, sizeof(path));
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(strlen(path) > 0);
    
    /* Path should be platform-specific:
     * Windows: named pipe path like \\.\pipe\...
     * Linux: socket path like /tmp/... or /run/...
     */
#if NCD_PLATFORM_WINDOWS
    ASSERT_TRUE(strstr(path, "pipe") != NULL || path[0] == '\\');
#else
    ASSERT_TRUE(path[0] == '/');
#endif
    
    return 0;
}

TEST(ipc_error_string_for_all_result_codes) {
    const char *str;
    
    str = ipc_error_string(NCD_IPC_OK);
    ASSERT_NOT_NULL(str);
    ASSERT_TRUE(strlen(str) > 0);
    
    str = ipc_error_string(NCD_IPC_ERROR_GENERIC);
    ASSERT_NOT_NULL(str);
    ASSERT_TRUE(strlen(str) > 0);
    
    str = ipc_error_string(NCD_IPC_ERROR_INVALID);
    ASSERT_NOT_NULL(str);
    ASSERT_TRUE(strlen(str) > 0);
    
    str = ipc_error_string(NCD_IPC_ERROR_BUSY);
    ASSERT_NOT_NULL(str);
    ASSERT_TRUE(strlen(str) > 0);
    
    str = ipc_error_string(NCD_IPC_ERROR_NOT_FOUND);
    ASSERT_NOT_NULL(str);
    ASSERT_TRUE(strlen(str) > 0);
    
    str = ipc_error_string(NCD_IPC_ERROR_BUSY_LOADING);
    ASSERT_NOT_NULL(str);
    ASSERT_TRUE(strlen(str) > 0);
    
    str = ipc_error_string(NCD_IPC_ERROR_BUSY_SCANNING);
    ASSERT_NOT_NULL(str);
    ASSERT_TRUE(strlen(str) > 0);
    
    str = ipc_error_string(NCD_IPC_ERROR_NOT_READY);
    ASSERT_NOT_NULL(str);
    ASSERT_TRUE(strlen(str) > 0);
    
    return 0;
}

TEST(ipc_client_cleanup_doesnt_crash) {
    /* Should not crash even if called multiple times */
    ipc_client_init();
    ipc_client_cleanup();
    ipc_client_cleanup();
    
    return 0;
}

TEST(ipc_client_disconnect_handles_null) {
    /* Should not crash with NULL */
    ipc_client_disconnect(NULL);
    
    return 0;
}

TEST(ipc_client_ping_fails_without_connection) {
    int result = ipc_client_init();
    ASSERT_EQ_INT(0, result);
    
    /* Ping without connection should fail */
    NcdIpcResult ping_result = ipc_client_ping(NULL);
    ASSERT_TRUE(ping_result != NCD_IPC_OK);
    
    ipc_client_cleanup();
    return 0;
}

TEST(ipc_client_get_version_fails_without_connection) {
    int result = ipc_client_init();
    ASSERT_EQ_INT(0, result);
    
    NcdIpcVersionInfo info;
    NcdIpcResult ver_result = ipc_client_get_version(NULL, &info);
    ASSERT_TRUE(ver_result != NCD_IPC_OK);
    
    ipc_client_cleanup();
    return 0;
}

TEST(ipc_service_exists_returns_false_when_stopped) {
    int result = ipc_client_init();
    ASSERT_EQ_INT(0, result);
    
    /* When no service is running, should return false */
    bool exists = ipc_service_exists();
    /* This may return true or false depending on whether a service
     * is actually running - we just verify it doesn't crash */
    (void)exists;
    
    ipc_client_cleanup();
    return 0;
}

TEST(ipc_header_size_correct) {
    /* Verify header structure size */
    ASSERT_EQ_INT(16, sizeof(NcdIpcHeader));
    return 0;
}

TEST(ipc_payload_structures_sizes) {
    /* Verify payload structures are reasonable sizes */
    ASSERT_TRUE(sizeof(NcdSubmitHeuristicPayload) <= 256);
    ASSERT_TRUE(sizeof(NcdSubmitMetadataPayload) <= 256);
    ASSERT_TRUE(sizeof(NcdRequestRescanPayload) <= 256);
    ASSERT_TRUE(sizeof(NcdStateInfoPayload) <= 256);
    ASSERT_TRUE(sizeof(NcdVersionInfoPayload) <= 128);
    ASSERT_TRUE(sizeof(NcdErrorPayload) <= 256);
    return 0;
}

#if !NCD_PLATFORM_WINDOWS
TEST(ipc_rejects_truncated_payload_length_in_response) {
    if (ipc_service_exists()) {
SKIP_TEST("Service is running; malformed fake server test requires free IPC path");
    }

    uint8_t resp[64];
    memset(resp, 0, sizeof(resp));

    NcdIpcHeader *hdr = (NcdIpcHeader *)resp;
    hdr->magic = NCD_IPC_MAGIC;
    hdr->version = NCD_IPC_VERSION;
    hdr->type = NCD_MSG_RESPONSE;
    hdr->sequence = 1;
    hdr->payload_len = 128; /* Declared larger than bytes actually sent */

    size_t resp_len = sizeof(NcdIpcHeader) + 8;

    NcdIpcResult result = NCD_IPC_OK;
    ASSERT_TRUE(run_fake_state_info_call(resp, resp_len, &result));
    ASSERT_EQ_INT(NCD_IPC_ERROR_INVALID, result);
    return 0;
}

TEST(ipc_rejects_oversized_state_info_name_lengths) {
    if (ipc_service_exists()) {
SKIP_TEST("Service is running; malformed fake server test requires free IPC path");
    }

    uint8_t resp[1024];
    memset(resp, 0, sizeof(resp));

    NcdIpcHeader *hdr = (NcdIpcHeader *)resp;
    NcdStateInfoPayload *payload = (NcdStateInfoPayload *)(resp + sizeof(NcdIpcHeader));
    char *names = (char *)(resp + sizeof(NcdIpcHeader) + sizeof(NcdStateInfoPayload));

    hdr->magic = NCD_IPC_MAGIC;
    hdr->version = NCD_IPC_VERSION;
    hdr->type = NCD_MSG_RESPONSE;
    hdr->sequence = 1;

    payload->protocol_version = NCD_IPC_VERSION;
    payload->meta_generation = 1;
    payload->db_generation = 1;
    payload->meta_size = 128;
    payload->db_size = 128;
    payload->meta_name_len = 300; /* larger than NcdIpcStateInfo.meta_name[256] */
    payload->db_name_len = 3;

    memset(names, 'A', payload->meta_name_len);
    names[payload->meta_name_len - 1] = '\0';
    memcpy(names + payload->meta_name_len, "d\0", payload->db_name_len);

    hdr->payload_len = (uint32_t)(sizeof(NcdStateInfoPayload) +
                                  payload->meta_name_len + payload->db_name_len);
    size_t resp_len = sizeof(NcdIpcHeader) + hdr->payload_len;

    NcdIpcResult result = NCD_IPC_OK;
    ASSERT_TRUE(run_fake_state_info_call(resp, resp_len, &result));
    ASSERT_EQ_INT(NCD_IPC_ERROR_INVALID, result);
    return 0;
}
#endif

/* ================================================================ Test Suite */

void suite_ipc(void) {
    printf("\n=== IPC Tests ===\n\n");
    
    /* Ensure no service is running from previous tests */
    if (ipc_service_exists()) {
        ipc_client_init();
        NcdIpcClient *client = ipc_client_connect();
        if (client) {
            ipc_client_request_shutdown(client);
            ipc_client_disconnect(client);
        }
        for (int i = 0; i < 30; i++) {
            if (!ipc_service_exists()) break;
            platform_sleep_ms(100);
        }
        ipc_client_cleanup();
#if NCD_PLATFORM_WINDOWS
        system("taskkill /F /IM NCDService.exe >nul 2>nul");
        platform_sleep_ms(300);
#endif
    }
    
    RUN_TEST(ipc_client_init_succeeds);
    RUN_TEST(ipc_client_connect_fails_when_no_service);
    RUN_TEST(ipc_make_address_returns_valid_path);
    RUN_TEST(ipc_error_string_for_all_result_codes);
    RUN_TEST(ipc_client_cleanup_doesnt_crash);
    RUN_TEST(ipc_client_disconnect_handles_null);
    RUN_TEST(ipc_client_ping_fails_without_connection);
    RUN_TEST(ipc_client_get_version_fails_without_connection);
    RUN_TEST(ipc_service_exists_returns_false_when_stopped);
    RUN_TEST(ipc_header_size_correct);
    RUN_TEST(ipc_payload_structures_sizes);
    RUN_TEST(ipc_reply_skips_queued_rescan_complete);
    RUN_TEST(ipc_reply_skips_queued_rescan_progress);
#if !NCD_PLATFORM_WINDOWS
    RUN_TEST(ipc_reply_framed_when_coalesced_with_notification);
    RUN_TEST(ipc_reply_reassembled_when_split_across_sends);
    RUN_TEST(ipc_rejects_truncated_payload_length_in_response);
    RUN_TEST(ipc_rejects_oversized_state_info_name_lengths);
#endif
}

TEST_MAIN(
    RUN_SUITE(ipc);
)
