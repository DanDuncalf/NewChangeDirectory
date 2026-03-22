/*
 * service_publish.h  --  Snapshot publication for NCD service
 *
 * This module handles publishing snapshots to shared memory for clients:
 * - Building snapshots from service state
 * - Managing shared memory objects
 * - Generation-based publication with atomic swap
 */

#ifndef NCD_SERVICE_PUBLISH_H
#define NCD_SERVICE_PUBLISH_H

#ifdef __cplusplus
extern "C" {
#endif

#include "service_state.h"
#include <stdbool.h>
#include <stddef.h>

/* --------------------------------------------------------- types              */

typedef struct SnapshotPublisher SnapshotPublisher;

/* Snapshot info for clients */
typedef struct {
    char     meta_shm_name[256];
    char     db_shm_name[256];
    uint64_t meta_generation;
    uint64_t db_generation;
    size_t   meta_size;
    size_t   db_size;
} SnapshotInfo;

/* --------------------------------------------------------- lifecycle          */

/*
 * snapshot_publisher_init  --  Initialize snapshot publisher
 *
 * Creates shared memory objects and prepares for publishing.
 */
SnapshotPublisher *snapshot_publisher_init(void);

/*
 * snapshot_publisher_cleanup  --  Cleanup snapshot publisher
 *
 * Unmaps and unlinks all shared memory objects.
 */
void snapshot_publisher_cleanup(SnapshotPublisher *pub);

/* --------------------------------------------------------- publication        */

/*
 * snapshot_publisher_publish_meta  --  Publish metadata snapshot
 *
 * Builds a new metadata snapshot from service state and publishes it
 * atomically by bumping the generation.
 */
bool snapshot_publisher_publish_meta(SnapshotPublisher *pub,
                                       const ServiceState *state);

/*
 * snapshot_publisher_publish_db  --  Publish database snapshot
 *
 * Builds a new database snapshot from service state and publishes it
 * atomically by bumping the generation.
 */
bool snapshot_publisher_publish_db(SnapshotPublisher *pub,
                                     const ServiceState *state);

/*
 * snapshot_publisher_publish_all  --  Publish both snapshots
 *
 * Convenience function to publish both metadata and database.
 */
bool snapshot_publisher_publish_all(SnapshotPublisher *pub,
                                      const ServiceState *state);

/* --------------------------------------------------------- info access        */

/*
 * snapshot_publisher_get_info  --  Get current snapshot info
 *
 * Fills info structure with current generation numbers and
 * shared memory object names for clients.
 */
void snapshot_publisher_get_info(const SnapshotPublisher *pub,
                                  SnapshotInfo *info);

/*
 * snapshot_publisher_get_meta_generation  --  Get published metadata generation
 */
uint64_t snapshot_publisher_get_meta_generation(const SnapshotPublisher *pub);

/*
 * snapshot_publisher_get_db_generation  --  Get published database generation
 */
uint64_t snapshot_publisher_get_db_generation(const SnapshotPublisher *pub);

/* --------------------------------------------------------- utility            */

/*
 * snapshot_publisher_get_meta_size  --  Get metadata snapshot size
 */
size_t snapshot_publisher_get_meta_size(const SnapshotPublisher *pub);

/*
 * snapshot_publisher_get_db_size  --  Get database snapshot size
 */
size_t snapshot_publisher_get_db_size(const SnapshotPublisher *pub);

#ifdef __cplusplus
}
#endif

#endif /* NCD_SERVICE_PUBLISH_H */
