#include "VersionUtils.h"

namespace {

struct ParsedVersion {
    uint32_t parts[4] = {0, 0, 0, 0};
    int partCount = 0;
    String prerelease;
    bool valid = false;
};

bool isNumeric(const String& value) {
    if (value.length() == 0) return false;
    for (size_t i = 0; i < value.length(); i++) {
        if (!isdigit(static_cast<unsigned char>(value[i]))) return false;
    }
    return true;
}

ParsedVersion parseVersion(String value) {
    ParsedVersion parsed;
    value.trim();
    if (value.startsWith("v") || value.startsWith("V")) value.remove(0, 1);

    int build = value.indexOf('+');
    if (build >= 0) value = value.substring(0, build);
    int dash = value.indexOf('-');
    if (dash >= 0) {
        parsed.prerelease = value.substring(dash + 1);
        value = value.substring(0, dash);
        if (parsed.prerelease.length() == 0) return parsed;
    }

    int start = 0;
    while (start <= static_cast<int>(value.length())) {
        if (parsed.partCount >= 4) return parsed;
        int dot = value.indexOf('.', start);
        if (dot < 0) dot = value.length();
        String component = value.substring(start, dot);
        if (!isNumeric(component)) return parsed;
        parsed.parts[parsed.partCount++] = static_cast<uint32_t>(strtoul(component.c_str(), nullptr, 10));
        if (dot >= static_cast<int>(value.length())) break;
        start = dot + 1;
    }

    parsed.valid = parsed.partCount > 0;
    return parsed;
}

int comparePrerelease(const String& left, const String& right) {
    int leftStart = 0;
    int rightStart = 0;
    while (true) {
        int leftDot = left.indexOf('.', leftStart);
        int rightDot = right.indexOf('.', rightStart);
        if (leftDot < 0) leftDot = left.length();
        if (rightDot < 0) rightDot = right.length();
        String leftPart = left.substring(leftStart, leftDot);
        String rightPart = right.substring(rightStart, rightDot);
        bool leftNumeric = isNumeric(leftPart);
        bool rightNumeric = isNumeric(rightPart);

        if (leftNumeric && rightNumeric) {
            uint32_t leftValue = strtoul(leftPart.c_str(), nullptr, 10);
            uint32_t rightValue = strtoul(rightPart.c_str(), nullptr, 10);
            if (leftValue != rightValue) return leftValue > rightValue ? 1 : -1;
        } else if (leftNumeric != rightNumeric) {
            return leftNumeric ? -1 : 1;
        } else {
            int comparison = leftPart.compareTo(rightPart);
            if (comparison != 0) return comparison > 0 ? 1 : -1;
        }

        bool leftDone = leftDot >= static_cast<int>(left.length());
        bool rightDone = rightDot >= static_cast<int>(right.length());
        if (leftDone || rightDone) {
            if (leftDone && rightDone) return 0;
            return leftDone ? -1 : 1;
        }
        leftStart = leftDot + 1;
        rightStart = rightDot + 1;
    }
}

}  // namespace

namespace VersionUtils {

bool isNewer(const String& candidate, const String& current) {
    ParsedVersion latest = parseVersion(candidate);
    ParsedVersion installed = parseVersion(current);
    if (!latest.valid || !installed.valid) return false;

    for (int i = 0; i < 4; i++) {
        if (latest.parts[i] != installed.parts[i]) return latest.parts[i] > installed.parts[i];
    }

    if (latest.prerelease.length() == 0 && installed.prerelease.length() > 0) return true;
    if (latest.prerelease.length() > 0 && installed.prerelease.length() == 0) return false;
    if (latest.prerelease.length() == 0) return false;
    return comparePrerelease(latest.prerelease, installed.prerelease) > 0;
}

}  // namespace VersionUtils
