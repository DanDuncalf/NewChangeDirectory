/*
 * service_publish.c  --  Snapshot publication implementation
 */

#include "service_publish.h"
#include "shared_state.h"
#include "shm_platform.h"
#include "shm_types.h"
#include "database.h"
#include "platform.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* --------------------------------------------------------- internal state     */

struct SnapshotPublisher {
    /* Shared memory handles */
    ShmHandle *meta_shm;
    ShmHandle *db_shm;
    
    /* Mapped addresses (for unmapping) */
    void *meta_addr;
    void *db_addr;
    
    /* Sizes */
    size_t meta_size;
    size_t db_size;
    
    /* Published generation numbers */
    uint64_t meta_generation;
    uint64_t db_generation;
    
    /* Names */
    char meta_name[256];
    char db_name[256];
};

/* --------------------------------------------------------- helper functions   */

static size_t compute_string_pool_size(const NcdMetadata *meta) {
    size_t size = 0;
    
    /* Groups */
    for (int i = 0; i < meta->groups.count; i++) {
        size += strlen(meta->groups.groups[i].name) + 1;
        size += strlen(meta->groups.groups[i].path) + 1;
    }
    
    /* Heuristics */
    for (int i = 0; i < meta->heuristics.count; i++) {
        size += strlen(meta->heuristics.entries[i].search) + 1;
        size += strlen(meta->heuristics.entries[i].target) + 1;
    }
    
    /* Exclusions */
    for (int i = 0; i < meta->exclusions.count; i++) {
        size += strlen(meta->exclusions.entries[i].pattern) + 1;
    }
    
    /* Dir history */
    for (int i = 0; i < meta->dir_history.count; i++) {
        size += strlen(meta->dir_history.entries[i].path) + 1;
    }
    
    return size;
}

static size_t compute_metadata_snapshot_size(const NcdMetadata *meta) {
    size_t size = sizeof(ShmSnapshotHdr);
    
    /* Section descriptors */
    size += 5 * sizeof(ShmSectionDesc);  /* config, groups, heuristics, exclusions, dir_history */
    
    /* Align to 8 bytes */
    size = (size + 7) & ~7;
    
    /* Config section */
    size += sizeof(ShmConfigSection);
    
    /* Groups section */
    size += sizeof(ShmGroupsSection);
    size += meta->groups.count * sizeof(ShmGroupEntry);
    
    /* Heuristics section */
    size += sizeof(ShmHeuristicsSection);
    size += meta->heuristics.count * sizeof(ShmHeurEntry);
    
    /* Exclusions section */
    size += sizeof(ShmExclusionsSection);
    size += meta->exclusions.count * sizeof(ShmExclusionEntry);
    
    /* Dir history section */
    size += sizeof(ShmDirHistorySection);
    size += meta->dir_history.count * sizeof(ShmDirHistoryEntry);
    
    /* String pool */
    size += compute_string_pool_size(meta);
    
    return size;
}

static void copy_ansi_to_shm(const char *src, NcdShmChar *dst, size_t dst_count) {
    if (!dst || dst_count == 0) {
        return;
    }
    dst[0] = 0;
    if (!src) {
        return;
    }
#if NCD_PLATFORM_WINDOWS
    size_t i = 0;
    while (src[i] && i + 1 < dst_count) {
        dst[i] = (NcdShmChar)(unsigned char)src[i];
        i++;
    }
    dst[i] = 0;
#else
    platform_strncpy_s(dst, dst_count, src);
#endif
}

static void derive_mount_point(const DriveData *drv, NcdShmChar *out, size_t out_count) {
    if (!drv || !out || out_count == 0) {
        return;
    }
    out[0] = 0;

    if (drv->mount_point[0]) {
        shm_strncpy(out, drv->mount_point, out_count - 1);
        out[out_count - 1] = 0;
        return;
    }

    if (drv->label[0]) {
        copy_ansi_to_shm(drv->label, out, out_count);
        if (out[0]) {
            return;
        }
    }

    {
        char mount_path[MAX_PATH] = {0};
        if (drv->letter && platform_build_mount_path(drv->letter, mount_path, sizeof(mount_path))) {
            copy_ansi_to_shm(mount_path, out, out_count);
        }
    }
}

static void derive_volume_label(const DriveData *drv, NcdShmChar *out, size_t out_count) {
    if (!drv || !out || out_count == 0) {
        return;
    }
    out[0] = 0;

    if (drv->volume_label[0]) {
        shm_strncpy(out, drv->volume_label, out_count - 1);
        out[out_count - 1] = 0;
        return;
    }

    if (drv->label[0]) {
        copy_ansi_to_shm(drv->label, out, out_count);
        return;
    }

    derive_mount_point(drv, out, out_count);
}

static size_t compute_database_snapshot_size(const NcdDatabase *db) {
    size_t size = sizeof(ShmDatabaseHeader);
    size += (size_t)db->drive_count * sizeof(ShmMountEntry);
    size = SHM_ALIGN8(size);

    for (int i = 0; i < db->drive_count; i++) {
        const DriveData *drv = &db->drives[i];
        NcdShmChar mount_point[NCD_MAX_PATH] = {0};
        NcdShmChar volume_label[64] = {0};
        size_t mount_bytes;
        size_t label_bytes;

        derive_mount_point(drv, mount_point, NCD_ARRAY_SIZE(mount_point));
        derive_volume_label(drv, volume_label, NCD_ARRAY_SIZE(volume_label));

        mount_bytes = (shm_strlen(mount_point) + 1) * sizeof(NcdShmChar);
        label_bytes = (shm_strlen(volume_label) + 1) * sizeof(NcdShmChar);

        size += (size_t)drv->dir_count * sizeof(ShmDirEntry);
        size = SHM_ALIGN8(size);
        size += drv->name_pool_len;
        size = SHM_ALIGN8(size);
        size += mount_bytes;
        size = SHM_ALIGN8(size);
        size += label_bytes;
        size = SHM_ALIGN8(size);
    }

    return size;
}

/* --------------------------------------------------------- snapshot building  */

static bool build_metadata_snapshot(uint8_t *buf, size_t buf_size,
                                     const NcdMetadata *meta,
                                     uint64_t generation) {
    if (!buf || !meta || buf_size < sizeof(ShmSnapshotHdr)) {
        return false;
    }
    
    memset(buf, 0, buf_size);
    
    /* Header */
    ShmSnapshotHdr *hdr = (ShmSnapshotHdr *)buf;
    hdr->magic = NCD_SHM_META_MAGIC;
    hdr->version = NCD_SHM_VERSION;
    hdr->flags = NCD_SHM_FLAG_COMPLETE | NCD_SHM_FLAG_READONLY;
    hdr->total_size = (uint32_t)buf_size;
    hdr->section_count = 5;  /* config, groups, heuristics, exclusions, dir_history */
    hdr->generation = generation;
    
    size_t offset = sizeof(ShmSnapshotHdr);
    
    /* Section descriptors */
    ShmSectionDesc *sections = (ShmSectionDesc *)(buf + offset);
    offset += 5 * sizeof(ShmSectionDesc);
    
    /* Align to 8 bytes */
    offset = (offset + 7) & ~7;
    hdr->header_size = (uint32_t)offset;
    
    /* Config section */
    sections[0].type = NCD_SHM_SECTION_CONFIG;
    sections[0].offset = (uint32_t)offset;
    sections[0].size = sizeof(ShmConfigSection);
    
    ShmConfigSection *cfg = (ShmConfigSection *)(buf + offset);
    cfg->magic = meta->cfg.magic;
    cfg->version = meta->cfg.version;
    cfg->default_show_hidden = meta->cfg.default_show_hidden;
    cfg->default_show_system = meta->cfg.default_show_system;
    cfg->default_fuzzy_match = meta->cfg.default_fuzzy_match;
    cfg->default_timeout = (int8_t)meta->cfg.default_timeout;
    cfg->has_defaults = meta->cfg.has_defaults;
    cfg->service_retry_count = meta->cfg.service_retry_count;
    cfg->text_encoding = meta->cfg.text_encoding ? meta->cfg.text_encoding : NCD_TEXT_UTF8;
    offset += sizeof(ShmConfigSection);
    
    /* String pool area */
    size_t pool_offset = offset;
    size_t pool_used = 0;
    
    /* Calculate pool start for groups/heuristics/exclusions/history */
    size_t data_offset = offset;
    data_offset += sizeof(ShmGroupsSection) + meta->groups.count * sizeof(ShmGroupEntry);
    data_offset += sizeof(ShmHeuristicsSection) + meta->heuristics.count * sizeof(ShmHeurEntry);
    data_offset += sizeof(ShmExclusionsSection) + meta->exclusions.count * sizeof(ShmExclusionEntry);
    data_offset += sizeof(ShmDirHistorySection) + meta->dir_history.count * sizeof(ShmDirHistoryEntry);
    
    pool_offset = data_offset;
    
    /* Groups section */
    sections[1].type = NCD_SHM_SECTION_GROUPS;
    sections[1].offset = (uint32_t)offset;
    sections[1].size = sizeof(ShmGroupsSection) + meta->groups.count * sizeof(ShmGroupEntry);
    
    ShmGroupsSection *groups = (ShmGroupsSection *)(buf + offset);
    groups->entry_count = (uint32_t)meta->groups.count;
    groups->entries_off = (uint32_t)(offset + sizeof(ShmGroupsSection));
    groups->pool_off = (uint32_t)pool_offset;
    offset += sizeof(ShmGroupsSection);
    
    ShmGroupEntry *group_entries = (ShmGroupEntry *)(buf + offset);
    for (int i = 0; i < meta->groups.count; i++) {
        group_entries[i].name_off = (uint32_t)(pool_offset + pool_used);
        strcpy((char *)(buf + pool_offset + pool_used), meta->groups.groups[i].name);
        pool_used += strlen(meta->groups.groups[i].name) + 1;
        
        group_entries[i].path_off = (uint32_t)(pool_offset + pool_used);
        strcpy((char *)(buf + pool_offset + pool_used), meta->groups.groups[i].path);
        pool_used += strlen(meta->groups.groups[i].path) + 1;
        
        group_entries[i].created = meta->groups.groups[i].created;
    }
    offset += meta->groups.count * sizeof(ShmGroupEntry);
    groups->pool_size = (uint32_t)pool_used;
    
    /* Heuristics section */
    sections[2].type = NCD_SHM_SECTION_HEURISTICS;
    sections[2].offset = (uint32_t)offset;
    sections[2].size = sizeof(ShmHeuristicsSection) + meta->heuristics.count * sizeof(ShmHeurEntry);
    
    ShmHeuristicsSection *heur = (ShmHeuristicsSection *)(buf + offset);
    heur->entry_count = (uint32_t)meta->heuristics.count;
    heur->entries_off = (uint32_t)(offset + sizeof(ShmHeuristicsSection));
    heur->pool_off = (uint32_t)(pool_offset + pool_used);
    offset += sizeof(ShmHeuristicsSection);
    
    ShmHeurEntry *heur_entries = (ShmHeurEntry *)(buf + offset);
    for (int i = 0; i < meta->heuristics.count; i++) {
        heur_entries[i].search_off = (uint32_t)(pool_offset + pool_used);
        strcpy((char *)(buf + pool_offset + pool_used), meta->heuristics.entries[i].search);
        pool_used += strlen(meta->heuristics.entries[i].search) + 1;
        
        heur_entries[i].target_off = (uint32_t)(pool_offset + pool_used);
        strcpy((char *)(buf + pool_offset + pool_used), meta->heuristics.entries[i].target);
        pool_used += strlen(meta->heuristics.entries[i].target) + 1;
        
        heur_entries[i].frequency = meta->heuristics.entries[i].frequency;
        heur_entries[i].last_used = (int32_t)(meta->heuristics.entries[i].last_used / 86400);  /* Days since epoch */
    }
    offset += meta->heuristics.count * sizeof(ShmHeurEntry);
    heur->pool_size = (uint32_t)(pool_used - (heur->pool_off - pool_offset));
    
    /* Exclusions section */
    sections[3].type = NCD_SHM_SECTION_EXCLUSIONS;
    sections[3].offset = (uint32_t)offset;
    sections[3].size = sizeof(ShmExclusionsSection) + meta->exclusions.count * sizeof(ShmExclusionEntry);
    
    ShmExclusionsSection *excl = (ShmExclusionsSection *)(buf + offset);
    excl->entry_count = (uint32_t)meta->exclusions.count;
    excl->entries_off = (uint32_t)(offset + sizeof(ShmExclusionsSection));
    excl->pool_off = (uint32_t)(pool_offset + pool_used);
    offset += sizeof(ShmExclusionsSection);
    
    ShmExclusionEntry *excl_entries = (ShmExclusionEntry *)(buf + offset);
    for (int i = 0; i < meta->exclusions.count; i++) {
        excl_entries[i].pattern_off = (uint32_t)(pool_offset + pool_used);
        strcpy((char *)(buf + pool_offset + pool_used), meta->exclusions.entries[i].pattern);
        pool_used += strlen(meta->exclusions.entries[i].pattern) + 1;
        
        excl_entries[i].drive = meta->exclusions.entries[i].drive;
        excl_entries[i].match_from_root = meta->exclusions.entries[i].match_from_root;
        excl_entries[i].has_parent_match = meta->exclusions.entries[i].has_parent_match;
    }
    offset += meta->exclusions.count * sizeof(ShmExclusionEntry);
    excl->pool_size = (uint32_t)(pool_used - (excl->pool_off - pool_offset));
    
    /* Dir history section */
    sections[4].type = NCD_SHM_SECTION_DIR_HISTORY;
    sections[4].offset = (uint32_t)offset;
    sections[4].size = sizeof(ShmDirHistorySection) + meta->dir_history.count * sizeof(ShmDirHistoryEntry);
    
    ShmDirHistorySection *dh = (ShmDirHistorySection *)(buf + offset);
    dh->entry_count = (uint32_t)meta->dir_history.count;
    dh->entries_off = (uint32_t)(offset + sizeof(ShmDirHistorySection));
    dh->pool_off = (uint32_t)(pool_offset + pool_used);
    offset += sizeof(ShmDirHistorySection);
    
    ShmDirHistoryEntry *dh_entries = (ShmDirHistoryEntry *)(buf + offset);
    for (int i = 0; i < meta->dir_history.count; i++) {
        dh_entries[i].path_off = (uint32_t)(pool_offset + pool_used);
        strcpy((char *)(buf + pool_offset + pool_used), meta->dir_history.entries[i].path);
        pool_used += strlen(meta->dir_history.entries[i].path) + 1;
        
        dh_entries[i].drive = meta->dir_history.entries[i].drive;
        dh_entries[i].timestamp = meta->dir_history.entries[i].timestamp;
    }
    offset += meta->dir_history.count * sizeof(ShmDirHistoryEntry);
    dh->pool_size = (uint32_t)(pool_used - (dh->pool_off - pool_offset));
    
    /* Compute and store checksum */
    hdr->checksum = shm_compute_checksum(buf, buf_size);
    
    return true;
}

static bool build_database_snapshot(uint8_t *buf, size_t buf_size,
                                     const NcdDatabase *db,
                                     uint64_t generation) {
    if (!buf || !db || buf_size < sizeof(ShmDatabaseHeader)) {
        return false;
    }
    
    memset(buf, 0, buf_size);
    
    /* Header (v2 format) */
    ShmDatabaseHeader *hdr = (ShmDatabaseHeader *)buf;
    hdr->magic = NCD_SHM_DB_MAGIC;
    hdr->version = NCD_SHM_TYPES_VERSION;
    hdr->text_encoding = NCD_SHM_TEXT_ENCODING;
    hdr->last_scan = db->last_scan;
    hdr->show_hidden = db->default_show_hidden ? 1 : 0;
    hdr->show_system = db->default_show_system ? 1 : 0;
    hdr->total_size = (uint32_t)buf_size;
    hdr->mount_count = (uint32_t)db->drive_count;
    hdr->generation = generation;
    
    size_t offset = sizeof(ShmDatabaseHeader);
    if (offset + (size_t)db->drive_count * sizeof(ShmMountEntry) > buf_size) {
        return false;
    }
    ShmMountEntry *mounts = (ShmMountEntry *)(buf + offset);
    offset += (size_t)db->drive_count * sizeof(ShmMountEntry);
    offset = SHM_ALIGN8(offset);
    hdr->header_size = (uint32_t)offset;

    for (int i = 0; i < db->drive_count; i++) {
        const DriveData *drv = &db->drives[i];
        NcdShmChar mount_point[NCD_MAX_PATH] = {0};
        NcdShmChar volume_label[64] = {0};
        size_t mount_bytes;
        size_t label_bytes;

        derive_mount_point(drv, mount_point, NCD_ARRAY_SIZE(mount_point));
        derive_volume_label(drv, volume_label, NCD_ARRAY_SIZE(volume_label));

        mount_bytes = (shm_strlen(mount_point) + 1) * sizeof(NcdShmChar);
        label_bytes = (shm_strlen(volume_label) + 1) * sizeof(NcdShmChar);

        mounts[i].dir_count = (uint32_t)drv->dir_count;
        mounts[i].type = (uint32_t)drv->type;
        mounts[i].reserved = 0;

        mounts[i].dirs_offset = (uint32_t)offset;
        if (offset + (size_t)drv->dir_count * sizeof(ShmDirEntry) > buf_size) {
            return false;
        }
        if (drv->dir_count > 0 && drv->dirs) {
            for (int j = 0; j < drv->dir_count; j++) {
                ShmDirEntry *dst = (ShmDirEntry *)(buf + mounts[i].dirs_offset) + j;
                dst->parent = drv->dirs[j].parent;
                dst->name_off = drv->dirs[j].name_off;
                dst->is_hidden = drv->dirs[j].is_hidden;
                dst->is_system = drv->dirs[j].is_system;
                dst->pad[0] = 0;
                dst->pad[1] = 0;
            }
        } else if (drv->dir_count > 0) {
            return false;
        }
        offset += (size_t)drv->dir_count * sizeof(ShmDirEntry);
        offset = SHM_ALIGN8(offset);

        mounts[i].pool_offset = (uint32_t)offset;
        mounts[i].pool_size = (uint32_t)drv->name_pool_len;
        if (offset + drv->name_pool_len > buf_size) {
            return false;
        }
        if (drv->name_pool_len > 0) {
            if (!drv->name_pool) {
                return false;
            }
            memcpy(buf + mounts[i].pool_offset, drv->name_pool, drv->name_pool_len);
        }
        offset += drv->name_pool_len;
        offset = SHM_ALIGN8(offset);

        mounts[i].mount_point_off = (uint32_t)offset;
        mounts[i].mount_point_len = (uint32_t)mount_bytes;
        if (offset + mount_bytes > buf_size) {
            return false;
        }
        memcpy(buf + mounts[i].mount_point_off, mount_point, mount_bytes);
        offset += mount_bytes;
        offset = SHM_ALIGN8(offset);

        mounts[i].volume_label_off = (uint32_t)offset;
        mounts[i].volume_label_len = (uint32_t)label_bytes;
        if (offset + label_bytes > buf_size) {
            return false;
        }
        memcpy(buf + mounts[i].volume_label_off, volume_label, label_bytes);
        offset += label_bytes;
        offset = SHM_ALIGN8(offset);
    }

    if (offset > buf_size) {
        return false;
    }
    
    /* Compute and store checksum */
    hdr->total_size = (uint32_t)offset;
    hdr->checksum = shm_compute_checksum(buf, buf_size);
    
    return true;
}

/* --------------------------------------------------------- lifecycle          */

SnapshotPublisher *snapshot_publisher_init(void) {
    SnapshotPublisher *pub = (SnapshotPublisher *)calloc(1, sizeof(SnapshotPublisher));
    if (!pub) {
        return NULL;
    }
    
    /* Initialize platform */
    if (shm_platform_init() != SHM_OK) {
        free(pub);
        return NULL;
    }
    
    /* Get shared memory names */
    if (!shm_make_meta_name(pub->meta_name, sizeof(pub->meta_name))) {
        shm_platform_cleanup();
        free(pub);
        return NULL;
    }
    
    if (!shm_make_db_name(pub->db_name, sizeof(pub->db_name))) {
        shm_platform_cleanup();
        free(pub);
        return NULL;
    }
    
    /* Try to unlink any old shared memory objects */
    shm_remove(pub->meta_name);
    shm_remove(pub->db_name);
    
    return pub;
}

void snapshot_publisher_cleanup(SnapshotPublisher *pub) {
    if (!pub) {
        return;
    }
    
    /* Unmap */
    if (pub->meta_addr) {
        shm_unmap(pub->meta_addr, pub->meta_size);
    }
    if (pub->db_addr) {
        shm_unmap(pub->db_addr, pub->db_size);
    }
    
    /* Close handles */
    if (pub->meta_shm) {
        shm_close(pub->meta_shm);
    }
    if (pub->db_shm) {
        shm_close(pub->db_shm);
    }
    
    /* Unlink */
    if (pub->meta_name[0]) {
        shm_remove(pub->meta_name);
    }
    if (pub->db_name[0]) {
        shm_remove(pub->db_name);
    }
    
    shm_platform_cleanup();
    free(pub);
}

/* --------------------------------------------------------- publication        */

bool snapshot_publisher_publish_meta(SnapshotPublisher *pub,
                                       const ServiceState *state) {
    if (!pub || !state) {
        return false;
    }
    
    const NcdMetadata *meta = service_state_get_metadata(state);
    if (!meta) {
        return false;
    }
    
    /* Compute required size */
    size_t new_size = compute_metadata_snapshot_size(meta);
    
    /* Allocate temp buffer */
    uint8_t *temp_buf = (uint8_t *)malloc(new_size);
    if (!temp_buf) {
        return false;
    }
    
    /* Build snapshot */
    uint64_t new_gen = service_state_get_meta_generation(state) + 1;
    if (!build_metadata_snapshot(temp_buf, new_size, meta, new_gen)) {
        free(temp_buf);
        return false;
    }
    
    /* Create/recreate shared memory */
    if (pub->meta_addr) {
        shm_unmap(pub->meta_addr, pub->meta_size);
        pub->meta_addr = NULL;
    }
    if (pub->meta_shm) {
        shm_close(pub->meta_shm);
        shm_remove(pub->meta_name);
        pub->meta_shm = NULL;
    }
    
    /* Round up to page size */
    size_t shm_size = shm_round_up_size(new_size);
    
    /* Create shared memory */
    ShmHandle *shm;
    ShmResult result = shm_create(pub->meta_name, shm_size, &shm);
    if (result != SHM_OK) {
        fprintf(stderr, "NCD Service: shm_create failed for metadata: %s (size=%zu)\n", shm_error_string(result), shm_size);
        free(temp_buf);
        return false;
    }
    
    /* Map it */
    void *addr;
    size_t mapped_size;
    result = shm_map(shm, SHM_ACCESS_WRITE, &addr, &mapped_size);
    if (result != SHM_OK) {
        shm_close(shm);
        shm_remove(pub->meta_name);
        free(temp_buf);
        return false;
    }
    
    /* Copy data */
    memcpy(addr, temp_buf, new_size);
    
    /* Unmap and remap read-only */
    shm_unmap(addr, mapped_size);
    
    if (shm_map(shm, SHM_ACCESS_READ, &addr, &mapped_size) != SHM_OK) {
        shm_close(shm);
        shm_remove(pub->meta_name);
        free(temp_buf);
        return false;
    }
    
    /* Update publisher state */
    pub->meta_shm = shm;
    pub->meta_addr = addr;
    pub->meta_size = mapped_size;
    pub->meta_generation = new_gen;
    
    free(temp_buf);
    return true;
}

bool snapshot_publisher_publish_db(SnapshotPublisher *pub,
                                     const ServiceState *state) {
    if (!pub || !state) {
        return false;
    }
    
    const NcdDatabase *db = service_state_get_database(state);
    if (!db) {
        return false;
    }
    
    /* Compute required size */
    size_t new_size = compute_database_snapshot_size(db);
    
    /* Allocate temp buffer */
    uint8_t *temp_buf = (uint8_t *)malloc(new_size);
    if (!temp_buf) {
        return false;
    }
    
    /* Build snapshot */
    uint64_t new_gen = service_state_get_db_generation(state) + 1;
    if (!build_database_snapshot(temp_buf, new_size, db, new_gen)) {
        free(temp_buf);
        return false;
    }
    
    /* Create/recreate shared memory */
    if (pub->db_addr) {
        shm_unmap(pub->db_addr, pub->db_size);
        pub->db_addr = NULL;
    }
    if (pub->db_shm) {
        shm_close(pub->db_shm);
        shm_remove(pub->db_name);
        pub->db_shm = NULL;
    }
    
    /* Round up to page size */
    size_t shm_size = shm_round_up_size(new_size);
    
    /* Create shared memory */
    ShmHandle *shm;
    ShmResult result = shm_create(pub->db_name, shm_size, &shm);
    if (result != SHM_OK) {
        fprintf(stderr, "NCD Service: shm_create failed for database: %s (size=%zu)\n", shm_error_string(result), shm_size);
        free(temp_buf);
        return false;
    }
    
    /* Map it */
    void *addr;
    size_t mapped_size;
    if (shm_map(shm, SHM_ACCESS_WRITE, &addr, &mapped_size) != SHM_OK) {
        shm_close(shm);
        shm_remove(pub->db_name);
        free(temp_buf);
        return false;
    }
    
    /* Copy data */
    memcpy(addr, temp_buf, new_size);
    
    /* Unmap and remap read-only */
    shm_unmap(addr, mapped_size);
    
    if (shm_map(shm, SHM_ACCESS_READ, &addr, &mapped_size) != SHM_OK) {
        shm_close(shm);
        shm_remove(pub->db_name);
        free(temp_buf);
        return false;
    }
    
    /* Update publisher state */
    pub->db_shm = shm;
    pub->db_addr = addr;
    pub->db_size = mapped_size;
    pub->db_generation = new_gen;
    
    free(temp_buf);
    return true;
}

bool snapshot_publisher_publish_all(SnapshotPublisher *pub,
                                      const ServiceState *state) {
    bool meta_ok = snapshot_publisher_publish_meta(pub, state);
    bool db_ok = snapshot_publisher_publish_db(pub, state);
    
    return meta_ok && db_ok;
}

/* --------------------------------------------------------- info access        */

void snapshot_publisher_get_info(const SnapshotPublisher *pub,
                                  SnapshotInfo *info) {
    if (!pub || !info) {
        return;
    }
    
    strncpy(info->meta_shm_name, pub->meta_name, sizeof(info->meta_shm_name) - 1);
    info->meta_shm_name[sizeof(info->meta_shm_name) - 1] = '\0';
    
    strncpy(info->db_shm_name, pub->db_name, sizeof(info->db_shm_name) - 1);
    info->db_shm_name[sizeof(info->db_shm_name) - 1] = '\0';
    
    info->meta_generation = pub->meta_generation;
    info->db_generation = pub->db_generation;
    info->meta_size = pub->meta_size;
    info->db_size = pub->db_size;
}

uint64_t snapshot_publisher_get_meta_generation(const SnapshotPublisher *pub) {
    if (!pub) {
        return 0;
    }
    return pub->meta_generation;
}

uint64_t snapshot_publisher_get_db_generation(const SnapshotPublisher *pub) {
    if (!pub) {
        return 0;
    }
    return pub->db_generation;
}

size_t snapshot_publisher_get_meta_size(const SnapshotPublisher *pub) {
    if (!pub) {
        return 0;
    }
    return pub->meta_size;
}

size_t snapshot_publisher_get_db_size(const SnapshotPublisher *pub) {
    if (!pub) {
        return 0;
    }
    return pub->db_size;
}
