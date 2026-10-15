#pragma once

namespace util {

// Choice for resolving a destination-file conflict during export.
// Kept in its own portable header so the export core does not depend on the
// platform dialog layer (util/Platform.h) that asks the user for this choice.
enum class OverwriteChoice { Overwrite, OverwriteAll, Skip, SkipAll };

}  // namespace util
