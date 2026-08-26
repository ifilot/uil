#pragma once

namespace launcher_readiness {

/** @brief Reads native-launcher readiness metadata from the process arguments. */
void initialize(int argc, char* argv[]);

/** @brief Returns whether this viewer was started by the native launcher. */
bool is_active();

/** @brief Records native-launcher timing metrics after logging is initialized. */
void record_startup_metrics();

/** @brief Reports a confirmed viewer startup stage to the native launcher. */
void report_progress(int stage);

/** @brief Signals that the presenter has completed its first paint. */
void signal_ready();

}  // namespace launcher_readiness
