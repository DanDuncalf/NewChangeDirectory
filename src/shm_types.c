/*
 * shm_types.c  --  Shared memory layout calculation implementation
 */

#include "shm_types.h"
#include "database.h"
#include <string.h>

/* --------------------------------------------------------- metadata layout  */

bool shm_metadata_compute_layout(const NcdMetadata *meta, ShmMetadataLayout *layout)
{
    if (!meta || !layout)
        return false;
    
    memset(layout, 0, sizeof(*layout));
    
    /* Calculate base sizes */
    layout->header_size = sizeof(ShmMetadataHeader);
    
    /* Section table - always allocate space for all section types */
    int section_count = 5;  /* config, groups, heuristics, exclusions, dir_history */
    layout->section_table_size = section_count * sizeof(ShmSectionDesc);
    
    /* Start calculating offsets */
    size_t offset = layout->header_size + layout->section_table_size;
    offset = SHM_ALIGN8(offset);
    
    /* Config section - always present, embedded */
    layout->config_off = (uint32_t)offset;
    layout->config_size = sizeof(ShmConfigSection);
    offset += layout->config_size;
    offset = SHM_ALIGN8(offset);
    
    /* Groups section */
    if (meta->groups.count > 0) {
        layout->groups_off = (uint32_t)offset;
        size_t entries_size = meta->groups.count * sizeof(ShmGroupEntry);
        
        /* Calculate string pool size for groups */
        size_t pool_size = 0;
        for (int i = 0; i < meta->groups.count; i++) {
            pool_size += strlen(meta->groups.groups[i].name) + 1;
            pool_size += strlen(meta->groups.groups[i].path) + 1;
        }
        
        layout->groups_size = sizeof(ShmGroupsSection) + entries_size + pool_size;
        offset += layout->groups_size;
        offset = SHM_ALIGN8(offset);
    }
    
    /* Heuristics section */
    if (meta->heuristics.count > 0) {
        layout->heuristics_off = (uint32_t)offset;
        size_t entries_size = meta->heuristics.count * sizeof(ShmHeurEntry);
        
        /* Calculate string pool size for heuristics */
        size_t pool_size = 0;
        for (int i = 0; i < meta->heuristics.count; i++) {
            pool_size += strlen(meta->heuristics.entries[i].search) + 1;
            pool_size += strlen(meta->heuristics.entries[i].target) + 1;
        }
        
        layout->heuristics_size = sizeof(ShmHeuristicsSection) + entries_size + pool_size;
        offset += layout->heuristics_size;
        offset = SHM_ALIGN8(offset);
    }
    
    /* Exclusions section */
    if (meta->exclusions.count > 0) {
        layout->exclusions_off = (uint32_t)offset;
        size_t entries_size = meta->exclusions.count * sizeof(ShmExclusionEntry);
        
        /* Calculate string pool size for exclusions */
        size_t pool_size = 0;
        for (int i = 0; i < meta->exclusions.count; i++) {
            pool_size += strlen(meta->exclusions.entries[i].pattern) + 1;
        }
        
        layout->exclusions_size = sizeof(ShmExclusionsSection) + entries_size + pool_size;
        offset += layout->exclusions_size;
        offset = SHM_ALIGN8(offset);
    }
    
    /* Directory history section */
    if (meta->dir_history.count > 0) {
        layout->dir_history_off = (uint32_t)offset;
        size_t entries_size = meta->dir_history.count * sizeof(ShmDirHistoryEntry);
        
        /* Calculate string pool size for history */
        size_t pool_size = 0;
        for (int i = 0; i < meta->dir_history.count; i++) {
            pool_size += strlen(meta->dir_history.entries[i].path) + 1;
        }
        
        layout->dir_history_size = sizeof(ShmDirHistorySection) + entries_size + pool_size;
        offset += layout->dir_history_size;
        offset = SHM_ALIGN8(offset);
    }
    
    layout->total_size = offset;
    return true;
}

/* --------------------------------------------------------- database layout  */

bool shm_database_compute_layout(const NcdDatabase *db, ShmDatabaseLayout *layout)
{
    if (!db || !layout)
        return false;
    
    /* If this is the first call, allocate the offset arrays */
    if (!layout->dirs_offsets) {
        layout->dirs_offsets = (uint32_t *)ncd_calloc(db->drive_count, sizeof(uint32_t));
        layout->pool_offsets = (uint32_t *)ncd_calloc(db->drive_count, sizeof(uint32_t));
        layout->mount_point_offsets = (uint32_t *)ncd_calloc(db->drive_count, sizeof(uint32_t));
        layout->volume_label_offsets = (uint32_t *)ncd_calloc(db->drive_count, sizeof(uint32_t));
        
        if (!layout->dirs_offsets || !layout->pool_offsets ||
            !layout->mount_point_offsets || !layout->volume_label_offsets) {
            shm_database_layout_free(layout);
            return false;
        }
    }
    
    /* Calculate base header size */
    layout->header_size = sizeof(ShmDatabaseHeader);
    layout->mount_table_size = db->drive_count * sizeof(ShmMountEntry);
    
    /* Start after header + mount table */
    size_t offset = layout->header_size + layout->mount_table_size;
    offset = SHM_ALIGN8(offset);
    
    /* Calculate per-mount data offsets */
    for (int i = 0; i < db->drive_count; i++) {
        DriveData *drv = &db->drives[i];
        
        /* DirEntry array for this mount */
        layout->dirs_offsets[i] = (uint32_t)offset;
        offset += drv->dir_count * sizeof(DirEntry);
        offset = SHM_ALIGN8(offset);
        
        /* Name pool for this mount */
        layout->pool_offsets[i] = (uint32_t)offset;
        offset += drv->name_pool_len;
        offset = SHM_ALIGN8(offset);
        
        /* Mount point string (platform-native encoding) */
        layout->mount_point_offsets[i] = (uint32_t)offset;
        offset += (shm_strlen(drv->mount_point) + 1) * sizeof(NcdShmChar);
        offset = SHM_ALIGN8(offset);
        
        /* Volume label string (platform-native encoding) */
        layout->volume_label_offsets[i] = (uint32_t)offset;
        offset += (shm_strlen(drv->volume_label) + 1) * sizeof(NcdShmChar);
        offset = SHM_ALIGN8(offset);
    }
    
    layout->total_size = offset;
    return true;
}

void shm_database_layout_free(ShmDatabaseLayout *layout)
{
    if (!layout)
        return;
    
    if (layout->dirs_offsets) {
        free(layout->dirs_offsets);
        layout->dirs_offsets = NULL;
    }
    if (layout->pool_offsets) {
        free(layout->pool_offsets);
        layout->pool_offsets = NULL;
    }
    if (layout->mount_point_offsets) {
        free(layout->mount_point_offsets);
        layout->mount_point_offsets = NULL;
    }
    if (layout->volume_label_offsets) {
        free(layout->volume_label_offsets);
        layout->volume_label_offsets = NULL;
    }
    
    layout->total_size = 0;
}
