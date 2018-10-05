#include <limits>

#include "database/single_node/graph_db.hpp"
#include "utils/flag_validation.hpp"
#include "utils/string.hpp"

// Durability flags.
DEFINE_bool(durability_enabled, false,
            "If durability (database persistence) should be enabled");
DEFINE_string(
    durability_directory, "durability",
    "Path to directory in which to save snapshots and write-ahead log files.");
DEFINE_bool(db_recover_on_startup, false, "Recover database on startup.");
DEFINE_VALIDATED_int32(
    snapshot_cycle_sec, 3600,
    "Amount of time between two snapshots, in seconds (min 60).",
    FLAG_IN_RANGE(1, std::numeric_limits<int32_t>::max()));
DEFINE_int32(snapshot_max_retained, -1,
             "Number of retained snapshots, -1 means without limit.");
DEFINE_bool(snapshot_on_exit, false, "Snapshot on exiting the database.");

// Misc flags
DEFINE_int32(query_execution_time_sec, 180,
             "Maximum allowed query execution time. Queries exceeding this "
             "limit will be aborted. Value of -1 means no limit.");
DEFINE_int32(gc_cycle_sec, 30,
             "Amount of time between starts of two cleaning cycles in seconds. "
             "-1 to turn off.");
// Data location.
DEFINE_string(properties_on_disk, "",
              "Property names of properties which will be stored on available "
              "disk. Property names have to be separated with comma (,).");

// Full durability.
DEFINE_bool(synchronous_commit, false,
            "Should a transaction end wait for WAL records to be written to "
            "disk before the transaction finishes.");

database::Config::Config()
    // Durability flags.
    : durability_enabled{FLAGS_durability_enabled},
      durability_directory{FLAGS_durability_directory},
      db_recover_on_startup{FLAGS_db_recover_on_startup},
      snapshot_cycle_sec{FLAGS_snapshot_cycle_sec},
      snapshot_max_retained{FLAGS_snapshot_max_retained},
      snapshot_on_exit{FLAGS_snapshot_on_exit},
      synchronous_commit{FLAGS_synchronous_commit},
      // Misc flags.
      gc_cycle_sec{FLAGS_gc_cycle_sec},
      query_execution_time_sec{FLAGS_query_execution_time_sec},
      // Data location.
      properties_on_disk(utils::Split(FLAGS_properties_on_disk, ",")) {}
