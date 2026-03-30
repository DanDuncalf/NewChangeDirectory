# Shared Memory Pointers vs Offsets

## The Problem

Shared memory is mapped at **different virtual addresses** in different processes:

```
Physical Memory (same for both processes):
┌─────────────────────────────────────┐
│  Shared Memory Segment (at phys 1GB) │
│  [offset 0x00]: metadata             │
│  [offset 0x40]: groups array         │
└─────────────────────────────────────┘

Process A Virtual Address Space:
┌─────────────────────────────────────┐
│  Shared memory mapped at 0x7f000000 │
│  metadata   = 0x7f000000            │
│  groups     = 0x7f000040  ◄───┐     │
│  ...                          │     │
│  Pointer stored: 0x7f000040 ──┘     │  ← VALID in Process A
└─────────────────────────────────────┘

Process B Virtual Address Space:
┌─────────────────────────────────────┐
│  Shared memory mapped at 0x7f800000 │
│  metadata   = 0x7f800000            │
│  groups     = 0x7f800040            │
│  ...                                │
│  Pointer from A: 0x7f000040 ◄── CRASH! │  ← INVALID in Process B
└─────────────────────────────────────┘
```

## Two Solutions

### Solution 1: Offsets (Relative Pointers)

Store **offset from base** instead of absolute address:

```c
typedef struct {
    /* Instead of: NcdGroupEntry *groups;  ← pointer */
    uint32_t groups_offset;  /* ← offset from struct start */
    int count;
} ShmGroupDb;

/* Access: base + offset = valid pointer in THIS process */
#define SHM_GROUPS(db, base) \
    ((NcdGroupEntry *)((char *)(base) + (db)->groups_offset))
```

```
Physical Memory:
┌─────────────────────────────────────┐ 0x00
│  ShmMetadata                         │
│    groups_offset = 0x40              │
├─────────────────────────────────────┤ 0x40
│  NcdGroupEntry array                 │
└─────────────────────────────────────┘

Process A (base = 0x7f000000):
  groups = 0x7f000000 + 0x40 = 0x7f000040 ✓

Process B (base = 0x7f800000):
  groups = 0x7f800000 + 0x40 = 0x7f800040 ✓
```

### Solution 2: Self-Relative Pointers (Base + Offset)

Store **base address** and all pointers are relative to it:

```c
typedef struct {
    void *base_address;        /* Address where THIS struct is mapped */
    NcdGroupEntry *groups;     /* Actually: base + offset */
} ShmMetadata;

/* Convert relative pointer to absolute */
#define REL_PTR(base, ptr) ((void *)((char *)(base) + (size_t)(ptr)))
```

This is more complex and requires fixups on mapping.

## The Real Solution: Flexible Array Members

For arrays inside the struct, use **Flexible Array Members (FAM)**:

```c
typedef struct {
    int count;
    int capacity;
    /* No pointer needed! Array follows immediately */
    NcdGroupEntry entries[];  /* FAM - part of struct */
} ShmGroupDb;

/* Layout in memory: */
┌─────────────────────────────────────┐
│  ShmGroupDb                          │
│    count = 5                         │
│    capacity = 5                      │
│    entries[0] ←─── embedded!         │
│    entries[1]                        │
│    ...                               │
└─────────────────────────────────────┘

/* Access: */
ShmGroupDb *groups = ...;
for (int i = 0; i < groups->count; i++) {
    printf("%s\n", groups->entries[i].name);  // Direct access!
}
```

## Recommended Design

Combining both approaches:

```c
/* shm_types.h */

/* Section headers use offsets (can point anywhere in SHM) */
typedef struct {
    uint32_t section_type;
    uint32_t offset;      /* Offset from ShmMetadata start */
    uint32_t size;
} ShmSection;

/* Data structures use FAM where possible */
typedef struct {
    int count;
    int capacity;
    NcdGroupEntry entries[];  /* FAM - contiguous with header */
} ShmGroupSection;

/* Main header */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint64_t generation;
    uint64_t checksum;
    
    /* Embedded config (no pointer) */
    NcdConfig config;
    
    /* Section count */
    uint32_t section_count;
    
    /* Sections follow immediately (FAM) */
    ShmSection sections[];
    
    /* Then section data follows... */
} ShmMetadata;

/* Access: No macros needed for FAM! */
ShmMetadata *meta = map_shared_memory(...);

/* Access sections directly */
for (uint32_t i = 0; i < meta->section_count; i++) {
    ShmSection *sec = &meta->sections[i];
    
    if (sec->section_type == SHM_SECTION_GROUPS) {
        /* section offset points to ShmGroupSection */
        ShmGroupSection *groups = (ShmGroupSection *)((char *)meta + sec->offset);
        
        /* entries[] is embedded - direct access! */
        for (int j = 0; j < groups->count; j++) {
            printf("%s\n", groups->entries[j].name);
        }
    }
}
```

## Memory Layout

```
┌──────────────────────────────────────────────────────────────┐
│ ShmMetadata (header)                                         │
│   magic, version, generation, checksum                       │
│   config (embedded NcdConfig)                                │
│   section_count = 3                                          │
├──────────────────────────────────────────────────────────────┤
│ ShmSection[0] (groups)                                       │
│   type = SHM_SECTION_GROUPS                                  │
│   offset = 128  ────────────┐                                │
│   size = 1024               │                                │
├─────────────────────────────┼────────────────────────────────┤
│ ShmSection[1] (heuristics)  │                                │
│   type = SHM_SECTION_HEUR   │                                │
│   offset = 1152 ────────────┼───┐                            │
│   size = 2048               │   │                            │
├─────────────────────────────┤   │                            │
│ ShmSection[2] (exclusions)  │   │                            │
│   type = SHM_SECTION_EXCL   │   │                            │
│   offset = 3200 ────────┐   │   │                            │
│   size = 512            │   │   │                            │
├─────────────────────────┼───┼───┼────────────────────────────┤
│ Padding to alignment    │   │   │                            │
├─────────────────────────┘   │   │                            │
│ ShmGroupSection (at 128) ◄──┘   │                            │
│   count, capacity               │                            │
│   entries[0]                    │                            │
│   entries[1]                    │                            │
│   ...                           │                            │
├─────────────────────────────────┘                            │
│ ShmHeuristicsSection (at 1152) ◄─────────────────────────────┘
│   count, capacity
│   entries[] (FAM)
│   hash_buckets[] (FAM)
├──────────────────────────────────────────────────────────────┤
│ ShmExclusionSection (at 3200) ◄──────────────────────────────┐
│   count, capacity                                            │
│   entries[] (FAM)                                            │
└──────────────────────────────────────────────────────────────┘
```

## Key Points

1. **No absolute pointers** - They break across process boundaries
2. **Use offsets** - Relative to known base address
3. **Use FAM** - For contiguous arrays (no pointer/indirection)
4. **Access is simple** - `base + offset` or direct field access
5. **Works everywhere** - Same code works regardless of where memory is mapped

## Comparison: Pointer vs Offset vs FAM

| Approach | Example | Pros | Cons |
|----------|---------|------|------|
| Pointer | `NcdGroupEntry *groups;` | Simple access | **Broken in SHM** |
| Offset | `uint32_t groups_off;` | Works in SHM | Needs macro/function |
| FAM | `NcdGroupEntry entries[];` | Works in SHM, simple access | Fixed position, needs section offset |

## Recommended Hybrid Approach

```c
/* Top-level structure uses offsets to find sections */
typedef struct {
    /* Header */
    uint32_t magic;
    ...
    
    /* Offsets to sections (sections can be anywhere) */
    uint32_t groups_off;
    uint32_t heuristics_off;
    uint32_t exclusions_off;
    
    /* Embedded data (fixed position) */
    NcdConfig config;
    NcdDirHistory dir_history;
} ShmMetadata;

/* Sections use FAM for their arrays */
typedef struct {
    int count;
    int capacity;
    NcdGroupEntry entries[];  /* ← FAM, contiguous */
} ShmGroupSection;

/* Access patterns */
ShmMetadata *meta = ...;

/* Section access (offset-based) */
ShmGroupSection *groups = (ShmGroupSection *)((char *)meta + meta->groups_off);

/* Array access (FAM - direct!) */
for (int i = 0; i < groups->count; i++) {
    groups->entries[i];  // ← Just works!
}
```

This gives us:
- ✅ **No hardcoded offsets** - All calculated at runtime
- ✅ **Structs everywhere** - Clean type-safe access
- ✅ **Works in shared memory** - No absolute pointers
- ✅ **Simple access** - Direct field access for FAM, one addition for offsets
