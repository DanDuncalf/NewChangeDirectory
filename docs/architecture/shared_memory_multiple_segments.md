# Multiple Shared Memory Segments vs Single Segment with Offsets

## The Question

Why not use multiple shared memory pieces instead of offsets within a single block?

**Short answer:** You can! It's a valid trade-off.

## Two Architectures

### Architecture 1: Single Shared Memory Block with Offsets

```
┌─────────────────────────────────────────────────────────────────┐
│ One Shared Memory Object: "/ncd_metadata"                        │
├─────────────────────────────────────────────────────────────────┤
│ Header │ Config │ Groups │ Heuristics │ Exclusions │ History    │
│        │        │ offset │ offset     │ offset     │            │
└─────────────────────────────────────────────────────────────────┘
         ▲
         │ map()
         ▼
┌─────────────────────────────────────────────────────────────────┐
│ Client Address Space                                             │
│ [Single pointer to base] ──> Points to entire block              │
└─────────────────────────────────────────────────────────────────┘
```

**Access:**
```c
ShmMetadata *meta = map_single_shm("/ncd_metadata");
ShmGroupSection *groups = (char *)meta + meta->groups_offset;
```

**Pros:**
- One SHM handle to manage
- One IPC call to get name
- Atomic update (replace entire block at once)
- Fewer kernel resources

**Cons:**
- Offsets needed for internal structure
- Must map entire thing even if only need part
- Harder to resize individual sections

---

### Architecture 2: Multiple Shared Memory Segments

```
┌─────────────────────────────┐
│ SHM: "/ncd_config"          │  ──┐
│ [NcdConfig]                 │    │
└─────────────────────────────┘    │
                                  │ Array of
┌─────────────────────────────┐   │ handles passed
│ SHM: "/ncd_groups"          │   │ via IPC
│ [ShmGroupSection]           │  ──┤
└─────────────────────────────┘   │
                                  │
┌─────────────────────────────┐   │
│ SHM: "/ncd_heuristics"      │  ──┘
│ [ShmHeuristicsSection]      │
└─────────────────────────────┘

┌─────────────────────────────┐
│ SHM: "/ncd_exclusions"      │
│ [ShmExclusionSection]       │
└─────────────────────────────┘

┌─────────────────────────────┐
│ SHM: "/ncd_database"        │
│ [ShmDatabase]               │
└─────────────────────────────┘
```

**Client:**
```c
ShmHandle *handles[5];
map_multiple_shm(handles, names);  /* Array of handles */

NcdConfig *config = handles[0].addr;           /* Direct pointer! */
ShmGroupSection *groups = handles[1].addr;     /* Direct pointer! */
ShmDatabase *db = handles[4].addr;             /* Direct pointer! */
```

**Pros:**
- **No offsets!** Regular pointers work within each segment
- Can map only what you need
- Independent resize/replace of each section
- Simpler data structures
- Better memory efficiency (don't map unused sections)

**Cons:**
- Multiple SHM handles to manage
- More IPC calls to get all names
- More kernel resources (multiple SHM objects)
- Slightly more complex handle array management
- Non-atomic updates (sections can get out of sync)

---

## Recommended Hybrid Approach

Use **2-3 segments** instead of many:

```c
/* Segment 0: Metadata (all config, groups, heuristics, etc.) */
/* Uses FAM within segment - no offsets between sections */
typedef struct {
    /* Header */
    uint32_t magic;
    uint32_t version;
    uint64_t generation;
    uint64_t checksum;
    
    /* Config (embedded) */
    NcdConfig config;
    
    /* Dir history (embedded) */
    NcdDirHistory dir_history;
    
    /* Groups section (FAM - immediately follows) */
    int groups_count;
    NcdGroupEntry groups_entries[];
    
    /* Heuristics follows groups... */
    /* But how to find it? Need offset OR padding! */
} ShmMetadataV2;
```

**Problem:** Variable-length FAM arrays make it hard to stack multiple sections.

**Solution:** Use **2 segments**:
1. **Metadata segment**: All metadata in one chunk with minimal offsets
2. **Database segment**: The directory database

```c
/* Segment 1: Metadata - uses only ONE offset per sub-section */
typedef struct {
    /* Header */
    uint32_t magic;
    uint32_t version;
    uint64_t generation;
    
    /* Embedded data */
    NcdConfig config;
    NcdDirHistory dir_history;
    
    /* Single offset table */
    uint32_t groups_off;       /* Offset to groups data */
    uint32_t heuristics_off;   /* Offset to heuristics data */
    uint32_t exclusions_off;   /* Offset to exclusions data */
    
    /* Each section uses FAM */
} ShmMetadata;

/* Segment 2: Database - simple, can cast directly */
typedef struct {
    /* Similar to current ShmDatabase */
    uint32_t magic;
    uint32_t drive_count;
    /* ... */
    /* Drive data follows... */
} ShmDatabase;
```

## Comparison: Offsets vs Multiple SHM

| Aspect | Single SHM + Offsets | Multiple SHM |
|--------|---------------------|--------------|
| **Handles to manage** | 1 | N (one per segment) |
| **IPC calls** | 1 name | N names (or one array) |
| **Kernel resources** | 1 SHM object | N SHM objects |
| **Internal access** | `base + offset` | Direct pointer |
| **Partial mapping** | No (all or nothing) | Yes (map subset) |
| **Atomic update** | Yes (replace whole) | Harder (coordinated) |
| **Code complexity** | Offset math | Handle management |
| **Cache efficiency** | Whole thing cached | Granular caching |

## Recommended Design for NCD

**Use 2 Shared Memory Segments:**

```c
/* Client-side state view */
typedef struct {
    /* Common header */
    NcdStateSourceInfo info;
    
    /* Service mode */
    struct {
        void *ipc_client;
        
        /* Two mapped segments */
        ShmHandle *meta_shm;
        ShmHandle *db_shm;
        
        ShmMetadata *meta;      /* Direct pointer - no copy! */
        ShmDatabase *db;        /* Direct pointer - no copy! */
    } service;
} NcdStateView;
```

**Service publishes:**
```c
/* Two separate SHM objects */
ShmHandle *meta_shm = shm_create("ncd_metadata", meta_size);
ShmHandle *db_shm = shm_create("ncd_database", db_size);

/* Write directly */
ShmMetadata *meta = shm_map(meta_shm);
meta->magic = NCD_SHM_META_MAGIC;
meta->config = service->metadata.cfg;
/* ... copy groups, heuristics, etc. at offsets ... */

ShmDatabase *db = shm_map(db_shm);
/* ... copy database ... */
```

**Client maps:**
```c
/* Get names via IPC */
NcdIpcShmInfo shm_info;
ipc_client_get_shm_info(client, &shm_info);
/* Returns: meta_name="ncd_metadata", db_name="ncd_database" */

/* Map both */
ShmHandle *meta_shm, *db_shm;
shm_open_existing(shm_info.meta_name, &meta_shm);
shm_open_existing(shm_info.db_name, &db_shm);

ShmMetadata *meta = shm_map(meta_shm);
ShmDatabase *db = shm_map(db_shm);

/* Use directly - no offsets needed between segments! */
/* Within each segment, FAM or simple offsets are used */
```

## Why 2 Segments is Better Than N Segments

**Too granular (5+ segments):**
```c
ShmHandle *handles[5];  /* config, groups, heuristics, exclusions, history */
/* Complex handle management */
/* Risk of handle exhaustion (OS limits) */
```

**Just right (2 segments):**
```c
ShmHandle *meta_shm;  /* All metadata */
ShmHandle *db_shm;    /* Database */

/* Simple, clean */
/* Metadata internally uses FAM + minimal offsets */
/* Database is already offset-based (blob format) */
```

## Updated Refactoring Plan

Instead of 45KB `NcdStateView` with embedded copies:

**Current:**
```c
NcdStateView {
    info;
    union {
        local { NcdMetadata *metadata; NcdDatabase *database; }  // 24 bytes
        service { 
            NcdMetadata metadata_copy;  // 41KB copy!
            NcdDatabase database_view;  // 4KB copy!
            ...
        }  // 45KB!
    }
}
```

**Target (2 SHM segments):**
```c
NcdStateView {
    info;
    union {
        local { NcdMetadata *metadata; NcdDatabase *database; }  // 24 bytes
        service {
            void *ipc;
            ShmHandle *meta_shm;   // Handle only
            ShmHandle *db_shm;     // Handle only
            ShmMetadata *meta;     // Mapped pointer - no copy!
            ShmDatabase *db;       // Mapped pointer - no copy!
        }  // ~48 bytes
    }
}
```

**Result:**
- Client: ~48 bytes instead of 45KB
- Zero copy - direct pointer access
- Only 2 SHM handles to manage
- Metadata internally uses FAM (no offsets between sections)
- Each section within metadata uses single offset from base

## Summary

**Multiple SHM segments is a valid approach!** For NCD specifically:

- **2 segments** is the sweet spot:
  - 1 for metadata (config, groups, heuristics, exclusions, history)
  - 1 for database
  
- **Benefits:**
  - No massive embedded copies
  - Direct pointer access within each segment
  - Can update metadata independently from database
  - Simple handle management
  
- **Trade-off:**
  - Need minimal offsets within metadata segment (FAM for arrays)
  - 2 IPC calls to get SHM names (or return array)

This eliminates the 45KB structure completely while keeping the code simple.
