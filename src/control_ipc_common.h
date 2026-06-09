/*
 * control_ipc_common.h  --  Platform hooks for shared IPC protocol logic
 *
 * Declares the platform-specific functions that control_ipc_common.c
 * depends on. Each platform file (control_ipc_win.c, control_ipc_posix.c)
 * must implement these two functions.
 */

#ifndef NCD_CONTROL_IPC_COMMON_H
#define NCD_CONTROL_IPC_COMMON_H

#include "control_ipc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------- debug mode         */

extern int g_ipc_debug_mode;

/* --------------------------------------------------------- platform hooks     */

/*
 * ipc_send_receive  --  Send a request and wait for response
 *
 * Platform-specific implementation that handles the actual I/O:
 * building the header, sending the message, receiving the response,
 * and validating the response header.
 *
 * Returns NCD_IPC_OK on success, error code on failure.
 * If out_response is non-NULL, a heap-allocated copy of the response
 * payload is returned (caller must free with ipc_free_message).
 */
NcdIpcResult ipc_send_receive(NcdIpcClient *client,
                              NcdMessageType type,
                              const void *payload,
                              size_t payload_len,
                              void **out_response,
                              size_t *out_response_len);

/*
 * ipc_platform_conn_write  --  Write raw data to a server-side connection
 *
 * Thin wrapper around the platform's write primitive (WriteFile on Windows,
 * send on POSIX). Maps platform errors to NcdIpcResult.
 */
NcdIpcResult ipc_platform_conn_write(NcdIpcConnection *conn,
                                     const void *data,
                                     size_t len);

/*
 * ipc_platform_recv_with_timeout  --  Receive a message with timeout
 *
 * Platform-specific implementation for non-blocking receive with timeout.
 * Used for polling progress updates from the service.
 *
 * Parameters:
 *   client   - The IPC client handle
 *   timeout_ms - Timeout in milliseconds (0 = non-blocking)
 *   out_type - Set to the received message type
 *   out_payload - Set to heap-allocated copy of payload (caller must free)
 *   out_len  - Set to payload length
 *
 * Returns NCD_IPC_OK on success, NCD_IPC_ERROR_NOT_FOUND on timeout,
 * or other error codes on failure.
 */
NcdIpcResult ipc_platform_recv_with_timeout(NcdIpcClient *client,
                                            int timeout_ms,
                                            NcdMessageType *out_type,
                                            void **out_payload,
                                            size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* NCD_CONTROL_IPC_COMMON_H */
