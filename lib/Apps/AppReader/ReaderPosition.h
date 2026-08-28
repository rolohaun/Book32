#pragma once

#include <Arduino.h>
#include <vector>
#include "EpubLoader.h"

struct PagePointer {
    int nodeIndex;
    int charOffset;
};

namespace ReaderPosition {

uint32_t toVisibleOffset(const std::vector<ContentNode>& content, const PagePointer& pointer);
PagePointer fromVisibleOffset(const std::vector<ContentNode>& content, uint32_t visibleOffset);
uint32_t totalVisibleLength(const std::vector<ContentNode>& content);

}  // namespace ReaderPosition
