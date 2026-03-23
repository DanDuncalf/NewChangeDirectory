# NCD Service Lazy Loading Implementation Plan

## Problem Statement

Currently, `NCDService.exe` fails to start with error "Failed to publish initial snapshots" because it attempts to load all databases synchronously during startup. This causes:
- Service startup timeout/failure with large databases
- No feedback to clients about loading state
- Requests fail instead of being handled gracefully

## Solution Overview

Implement **lazy loading** with **request queueing**:
1. Service starts immediately without loading databases
2. Returns `BUSY_LOADING` or `BUSY_SCANNING` status for data-dependent requests
3. Queues all requests during loading/rescan operations
4. Background thread loads databases asynchronously
5. Processes queued requests once loading completes

## Service States

```
┌─────────────┐     start()      ┌─────────────┐
│  STOPPED    │ ───────────────> │  STARTING   │
└─────────────┘                  └──────┬──────┘
                                        │
                    load_complete()     │ load_databases_async()
                                        ▼
┌─────────────┐     start()      ┌─────────────┐
│   READY     │ <─────────────── │   LOADING   │
└──────┬──────┘                  └─────────────┘
       │
       │ rescan_requested()
       ▼
┌─────────────┐
│  SCANNING   │──> Returns BUSY_SCANNING, queues requests
└─────────────┘
```

### State Definitions

| State | Description | Request Handling |
|-------|-------------|------------------|
| `STOPPED` | Service not running | N/A |
| `STARTING` | Initializing structures | Queue all requests |
| `LOADING` | Loading databases from disk | Return BUSY_LOADING, queue requests |
| `READY` | Fully operational | Process requests immediately |
| `SCANNING` | Performing rescan | Return BUSY_SCANNING, queue requests |

## Architecture Changes

### 1. Service State Machine (service_state.h/c)

Add to existing `ServiceState`:

```c
typedef enum {
    SERVICE_STATE_STOPPED = 0,
    SERVICE_STATE_STARTING,
    SERVICE_STATE_LOADING,
    SERVICE_STATE_READY,
    SERVICE_STATE_SCANNING
} ServiceRuntimeState;

typedef struct PendingRequest {
    IpcMessageType type;
    IpcRequestPayload payload;
    IpcClientHandle *client;  // For async response
    struct PendingRequest *next;
} PendingRequest;

typedef struct {
    // ... existing fields ...
    
    // State machine
    ServiceRuntimeState runtime_state;
    Mutex state_mutex;
    CondVar state_cond;  // Signaled when state changes to READY
    
    // Request queue (locked by state_mutex)
    PendingRequest *pending_head;
    PendingRequest *pending_tail;
    int pending_count;
    
    // Background loader thread
    Thread loader_thread;
    bool loader_stop_requested;
    
    // Status message for clients
    char status_message[256];
} ServiceState;
```

### 2. Response Types (control_ipc.h)

Add new response status codes:

```c
typedef enum {
    IPC_STATUS_OK = 0,
    IPC_STATUS_ERROR,
    IPC_STATUS_BUSY_LOADING,    // NEW: Database loading in progress
    IPC_STATUS_BUSY_SCANNING,   // NEW: Scan operation in progress
    IPC_STATUS_NOT_READY        // NEW: Service not fully initialized
} IpcResponseStatus;

typedef struct {
    IpcResponseStatus status;
    char message[256];          // Human-readable status message
    uint64_t generation_meta;
    uint64_t generation_db;
    ServiceRuntimeState service_state;  // NEW: Current service state
} IpcStateInfoResponse;
```

### 3. Request Classification

Categorize requests by data dependency:

| Category | Requests | Behavior During Loading |
|----------|----------|------------------------|
| **Immediate** | PING, GET_STATE_INFO, FLUSH | Process immediately |
| **Data-Required** | QUERY, TREE, LS, CHECK (path) | Return BUSY_LOADING |
| **Mutation** | SUBMIT_HEURISTIC, UPDATE_METADATA, ADD_GROUP, etc. | Queue for later processing |
| **Control** | REQUEST_RESCAN | Queue, start rescan when ready |

### 4. Background Loader Thread

```c
typedef struct {
    ServiceState *state;
    // Additional context for loader
} LoaderContext;

static THREAD_FUNC_RETURN_TYPE loader_thread_func(void *arg) {
    LoaderContext *ctx = (LoaderContext *)arg;
    ServiceState *state = ctx->state;
    
    // Update state
    service_state_set_runtime_state(state, SERVICE_STATE_LOADING);
    service_state_set_status_message(state, "Loading databases...");
    
    // Load metadata
    service_state_set_status_message(state, "Loading metadata...");
    if (!load_metadata_async(state)) {
        service_state_set_status_message(state, "Metadata load failed");
        // Continue anyway - service can still function
    }
    
    // Load databases (per-drive, incremental)
    int total_drives = get_drive_count();
    for (int i = 0; i < total_drives && !state->loader_stop_requested; i++) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Loading drive %d of %d...", i + 1, total_drives);
        service_state_set_status_message(state, msg);
        
        load_drive_database_async(state, i);
    }
    
    // Publish initial snapshots
    service_state_set_status_message(state, "Publishing snapshots...");
    service_publish_snapshots(state);
    
    // Transition to READY
    service_state_set_runtime_state(state, SERVICE_STATE_READY);
    service_state_set_status_message(state, "Ready");
    
    // Process any queued requests
    service_process_pending_requests(state);
    
    // Cleanup
    free(ctx);
    return 0;
}
```

## Implementation Steps

### Phase 1: State Machine Foundation

**Files to modify:**
- `src/service_state.h` - Add runtime state enum and pending request queue
- `src/service_state.c` - Implement state transitions and queue management

**Tasks:**
1. Add `ServiceRuntimeState` enum
2. Add `PendingRequest` linked list structure
3. Add mutex/condition variable for state synchronization
4. Implement `service_state_set_runtime_state()` with condition signaling
5. Implement `service_state_get_runtime_state()`
6. Implement `service_state_set_status_message()`
7. Implement `service_state_get_status_message()`
8. Add `service_state_wait_for_ready()` with timeout

### Phase 2: Request Queue Management

**Files to modify:**
- `src/service_state.c`
- `src/service_main.c`

**Tasks:**
1. Implement `service_state_enqueue_request()` - Add to pending queue
2. Implement `service_state_dequeue_request()` - Remove from queue
3. Implement `service_state_clear_pending_queue()` - Cleanup on shutdown
4. Implement `service_process_pending_requests()` - Process all queued requests
5. Add queue size limit (e.g., 1000 requests) with overflow handling

### Phase 3: Background Loader Thread

**Files to modify:**
- `src/service_main.c` - Loader thread creation and management
- `src/service_state.c` - Async loading functions

**Tasks:**
1. Add `loader_thread_func()` with proper state transitions
2. Modify `service_main()` to start loader thread instead of blocking load
3. Add graceful shutdown handling for loader thread
4. Implement progress reporting through status messages
5. Add `loader_stop_requested` flag for clean termination

### Phase 4: Request Classification and Handling

**Files to modify:**
- `src/service_main.c` - Request handler dispatch
- `src/control_ipc.h` - Add new response types

**Tasks:**
1. Create `is_request_data_dependent(IpcMessageType type)` function
2. Create `is_request_mutation(IpcMessageType type)` function
3. Modify request handler to check service state first
4. Implement BUSY_LOADING response for data-dependent requests during LOADING
5. Implement BUSY_SCANNING response during SCANNING
6. Queue mutation requests during LOADING/SCANNING
7. Process immediate requests (PING, etc.) regardless of state

### Phase 5: IPC Response Updates

**Files to modify:**
- `src/control_ipc.h` - Response structures
- `src/control_ipc_win.c` - Windows IPC response handling
- `src/control_ipc_posix.c` - POSIX IPC response handling

**Tasks:**
1. Update `IpcStateInfoResponse` to include `service_state` field
2. Update response serialization/deserialization
3. Ensure backward compatibility (new clients can talk to old service)

### Phase 6: Rescan Integration

**Files to modify:**
- `src/service_state.c` - Rescan handling
- `src/service_main.c` - Rescan request handler

**Tasks:**
1. Add `SERVICE_STATE_SCANNING` state
2. Modify rescan handler to transition to SCANNING state
3. Queue all incoming requests during scan
4. Publish new snapshot after scan completes
5. Process queued requests after scan
6. Return BUSY_SCANNING for data-dependent requests during scan

### Phase 7: Client Integration

**Files to modify:**
- `src/state_backend_service.c` - Service client handling

**Tasks:**
1. Check service state in response handling
2. Implement retry logic for BUSY_LOADING/BUSY_SCANNING
3. Add maximum retry count and timeout
4. Display status message to user when waiting

### Phase 8: Testing

**New test file:** `test/test_service_lazy_load.c`

**Test cases:**
1. Service starts and reports LOADING state
2. Data-dependent requests return BUSY_LOADING during startup
3. Mutation requests are queued and processed after loading
4. Service transitions to READY after loading completes
5. Rescan transitions to SCANNING and queues requests
6. Client receives BUSY_SCANNING during rescan
7. Pending requests processed after rescan completes
8. Service handles shutdown gracefully during loading
9. Queue overflow handled gracefully
10. Status messages are meaningful and updated

## API Additions

### service_state.h

```c
// Runtime state management
void service_state_set_runtime_state(ServiceState *state, ServiceRuntimeState new_state);
ServiceRuntimeState service_state_get_runtime_state(const ServiceState *state);
bool service_state_wait_for_ready(ServiceState *state, int timeout_ms);

// Status message
void service_state_set_status_message(ServiceState *state, const char *message);
const char *service_state_get_status_message(const ServiceState *state);

// Request queue
bool service_state_enqueue_request(ServiceState *state, IpcMessageType type, 
                                    const IpcRequestPayload *payload,
                                    IpcClientHandle *client);
void service_process_pending_requests(ServiceState *state);
void service_state_clear_pending_queue(ServiceState *state);

// Loader thread
int service_state_start_loader(ServiceState *state);
void service_state_stop_loader(ServiceState *state);
```

## Performance Considerations

1. **Queue Size Limit**: Cap pending requests at 1000 to prevent memory exhaustion
2. **Loader Priority**: Run loader at lower priority to avoid blocking IPC handling
3. **Incremental Loading**: Load one drive at a time, publish intermediate snapshots
4. **Progress Updates**: Update status message every 100ms max to avoid spam

## Error Handling

| Scenario | Behavior |
|----------|----------|
| Database file missing | Log warning, continue with empty DB for that drive |
| Corrupted database | Log error, skip that drive, mark for rescan |
| Queue full | Return IPC_STATUS_ERROR with "Queue full" message |
| Loader thread fails | Transition to ERROR state, log critical error |
| Shutdown during loading | Signal loader to stop, wait up to 5 seconds |

## Backward Compatibility

- Old clients receiving new status codes will see them as generic errors
- New clients can detect old services (no state field in response)
- Service still creates shared memory snapshots (no change to format)

## Success Criteria

1. Service starts within 100ms regardless of database size
2. First query during startup returns BUSY_LOADING within 50ms
3. Service transitions to READY within 5 seconds for typical DB sizes (<100MB)
4. All queued mutation requests processed after loading
5. No requests lost during state transitions
6. Rescan operations queue requests properly
7. Service shuts down cleanly even during loading

## Implementation Order

1. ✅ Create this plan file
2. Add state machine structures to service_state.h
3. Implement state transition functions
4. Add request queue management
5. Create background loader thread
6. Modify request handling for state awareness
7. Update IPC responses
8. Integrate with rescan
9. Add client retry logic
10. Write tests

## Estimated Effort

- Phase 1 (State Machine): 2-3 hours
- Phase 2 (Queue Management): 2-3 hours
- Phase 3 (Loader Thread): 3-4 hours
- Phase 4 (Request Classification): 2-3 hours
- Phase 5 (IPC Updates): 1-2 hours
- Phase 6 (Rescan Integration): 2-3 hours
- Phase 7 (Client Integration): 2-3 hours
- Phase 8 (Testing): 3-4 hours

**Total: ~17-25 hours**
