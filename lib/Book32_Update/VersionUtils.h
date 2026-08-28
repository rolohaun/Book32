#pragma once

#include <Arduino.h>

namespace VersionUtils {

// Returns true only when candidate is a valid semantic-style version newer
// than current. Leading v/V and omitted trailing zero components are accepted.
bool isNewer(const String& candidate, const String& current);

}  // namespace VersionUtils
