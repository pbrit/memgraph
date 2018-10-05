#pragma once

#include <experimental/filesystem>

#include "database/distributed/graph_db.hpp"

namespace durability {

/**
 * Make snapshot and save it in snapshots folder. Returns true if successful.
 * @param db - database for which we are creating a snapshot
 * @param dba - db accessor with which we are creating a snapshot (reading data)
 * @param durability_dir - directory where durability data is stored.
 * @param snapshot_max_retained - maximum number of snapshots to retain.
 */
bool MakeSnapshot(database::GraphDb &db, database::GraphDbAccessor &dba,
                  int worker_id,
                  const std::experimental::filesystem::path &durability_dir,
                  int snapshot_max_retained);

}  // namespace durability
