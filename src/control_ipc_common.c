/*
 * control_ipc_common.c  --  Platform-independent IPC protocol logic
 *
 * Contains all payload-serialization/deserialization, error mapping,
 * client API wrappers, and server response functions that are identical
 * across Windows and POSIX platforms.
 *
 * Platform-specific I/O primitives (ipc_platform_conn_write, etc.) and
 * the core ipc_send_receive() are implemented in each platform file.
 */

#include "control_ipc_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------- debug mode         */

int g_ipc_debug_mode = 0;

void ipc_set_debug_mode(int enable) {
    g_ipc_debug_mode = enable;
}

/* --------------------------------------------------------- error handling     */

const char *ipc_error_string(NcdIpcResult result) {
    switch (result) {
        case NCD_IPC_OK:               return "Success";
        case NCD_IPC_ERROR_GENERIC:    return "Generic error";
        case NCD_IPC_ERROR_INVALID:    return "Invalid message";
        case NCD_IPC_ERROR_BUSY:       return "Service busy";
        case NCD_IPC_ERROR_NOT_FOUND:  return "Not found";
        case NCD_IPC_ERROR_BUSY_LOADING:  return "Service loading databases";
        case NCD_IPC_ERROR_BUSY_SCANNING: return "Service scanning filesystem";
        case NCD_IPC_ERROR_NOT_READY:  return "Service not ready";
        case NCD_IPC_ERROR_SHUTTING_DOWN: return "Service shutting down";
        default: return "Unknown error";
    }
}

/* --------------------------------------------------------- client API         */

NcdIpcResult ipc_client_ping(NcdIpcClient *client) {
    return ipc_send_receive(client, NCD_MSG_PING, NULL, 0, NULL, NULL);
}

NcdIpcResult ipc_client_get_state_info(NcdIpcClient *client, NcdIpcStateInfo *info) {
    if (!info) {
        return NCD_IPC_ERROR_INVALID;
    }
    
    void *response = NULL;
    size_t response_len = 0;
    
    NcdIpcResult result = ipc_send_receive(client, NCD_MSG_GET_STATE_INFO,
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
    
    NcdIpcResult result = ipc_send_receive(client, NCD_MSG_GET_DETAILED_STATUS,
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
    
    return ipc_send_receive(client, NCD_MSG_SUBMIT_HEURISTIC,
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
    
    return ipc_send_receive(client, NCD_MSG_SUBMIT_METADATA,
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
    
    return ipc_send_receive(client, NCD_MSG_REQUEST_RESCAN,
                            &payload, sizeof(payload), NULL, NULL);
}

NcdIpcResult ipc_client_request_flush(NcdIpcClient *client) {
    return ipc_send_receive(client, NCD_MSG_REQUEST_FLUSH, NULL, 0, NULL, NULL);
}

NcdIpcResult ipc_client_get_version(NcdIpcClient *client, NcdIpcVersionInfo *info) {
    if (!info) {
        return NCD_IPC_ERROR_INVALID;
    }
    
    void *response = NULL;
    size_t response_len = 0;
    
    NcdIpcResult result = ipc_send_receive(client, NCD_MSG_GET_VERSION,
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
     * P1.1a: Unified behavior - treat both NCD_IPC_OK and NCD_IPC_ERROR_NOT_FOUND
     * as success. NOT_FOUND means the service pipe/socket was closed after the
     * shutdown response, which is expected (service exited). Previously POSIX
     * did not handle this case, causing spurious failures.
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
    return ipc_send_receive(client, NCD_MSG_REQUEST_SHUTDOWN, NULL, 0, NULL, NULL);
}

/* --------------------------------------------------------- server API         */

NcdIpcResult ipc_server_send_response(NcdIpcConnection *conn,
                                      uint32_t sequence,
                                      const void *payload,
                                      size_t payload_len) {
    if (!conn) {
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
    
    return ipc_platform_conn_write(conn, msg_buf, msg_len);
}

NcdIpcResult ipc_server_send_error(NcdIpcConnection *conn,
                                   uint32_t sequence,
                                   NcdIpcResult error_code,
                                   const char *message) {
    if (!conn) {
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
    
    return ipc_platform_conn_write(conn, msg_buf, total_len);
}

NcdIpcResult ipc_server_send_version_mismatch(NcdIpcConnection *conn,
                                              uint32_t sequence,
                                              const char *client_version,
                                              const char *client_build,
                                              const char *service_version,
                                              const char *service_build,
                                              const char *message) {
    if (!conn) {
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
    
    return ipc_platform_conn_write(conn, msg_buf, total_len);
}

/* --------------------------------------------------------- utilities          */

void ipc_free_message(void *payload) {
    free(payload);
}
