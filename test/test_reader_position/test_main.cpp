#include <Arduino.h>
#include <unity.h>
#include "AppReader/ReaderPosition.h"
#include "VersionUtils.h"

namespace {

ContentNode textNode(const char* text, bool blockStart) {
    ContentNode node;
    node.type = CONTENT_TEXT;
    node.textNode.text = text;
    node.textNode.isBlockStart = blockStart;
    return node;
}

ContentNode imageNode(const char* href) {
    ContentNode node;
    node.type = CONTENT_IMAGE;
    node.imageNode.href = href;
    return node;
}

void testVisibleOffsetsUseUnicodeCodepoints() {
    std::vector<ContentNode> content;
    content.push_back(textNode("Alpha", true));
    content.push_back(textNode("caf\xC3\xA9", false));
    content.push_back(textNode("Omega", true));

    TEST_ASSERT_EQUAL_UINT32(8, ReaderPosition::toVisibleOffset(content, {1, 3}));
    TEST_ASSERT_EQUAL_UINT32(10, ReaderPosition::toVisibleOffset(content, {2, 0}));
    TEST_ASSERT_EQUAL_UINT32(15, ReaderPosition::totalVisibleLength(content));
}

void testVisibleOffsetsRoundTripToBytePositions() {
    std::vector<ContentNode> content;
    content.push_back(textNode("Alpha", true));
    content.push_back(textNode("caf\xC3\xA9", false));
    content.push_back(textNode("Omega", true));

    PagePointer insideUtf8Node = ReaderPosition::fromVisibleOffset(content, 8);
    TEST_ASSERT_EQUAL_INT(1, insideUtf8Node.nodeIndex);
    TEST_ASSERT_EQUAL_INT(3, insideUtf8Node.charOffset);

    PagePointer blockStart = ReaderPosition::fromVisibleOffset(content, 10);
    TEST_ASSERT_EQUAL_INT(2, blockStart.nodeIndex);
    TEST_ASSERT_EQUAL_INT(0, blockStart.charOffset);

    PagePointer beforeBlockSeparator = ReaderPosition::fromVisibleOffset(content, 9);
    TEST_ASSERT_EQUAL_INT(1, beforeBlockSeparator.nodeIndex);
    TEST_ASSERT_EQUAL_INT(5, beforeBlockSeparator.charOffset);
    TEST_ASSERT_EQUAL_UINT32(9, ReaderPosition::toVisibleOffset(content, beforeBlockSeparator));
}

void testImagesHaveStableLogicalPositions() {
    std::vector<ContentNode> content;
    content.push_back(textNode("Before", true));
    content.push_back(imageNode("OPS/art.jpg"));
    content.push_back(textNode("After", true));

    TEST_ASSERT_EQUAL_UINT32(6, ReaderPosition::toVisibleOffset(content, {1, 0}));
    TEST_ASSERT_EQUAL_UINT32(8, ReaderPosition::toVisibleOffset(content, {2, 0}));

    PagePointer afterImage = ReaderPosition::fromVisibleOffset(content, 8);
    TEST_ASSERT_EQUAL_INT(2, afterImage.nodeIndex);
    TEST_ASSERT_EQUAL_INT(0, afterImage.charOffset);
    TEST_ASSERT_EQUAL_UINT32(8, ReaderPosition::toVisibleOffset(content, afterImage));
}

void testUpdatesRequireANewerVersion() {
    TEST_ASSERT_TRUE(VersionUtils::isNewer("v1.2.1", "1.2"));
    TEST_ASSERT_TRUE(VersionUtils::isNewer("2.0", "1.9.9"));
    TEST_ASSERT_FALSE(VersionUtils::isNewer("v1.1", "1.2"));
    TEST_ASSERT_FALSE(VersionUtils::isNewer("v1.2.0", "1.2"));
    TEST_ASSERT_FALSE(VersionUtils::isNewer("latest", "1.2"));
}

}  // namespace

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(testVisibleOffsetsUseUnicodeCodepoints);
    RUN_TEST(testVisibleOffsetsRoundTripToBytePositions);
    RUN_TEST(testImagesHaveStableLogicalPositions);
    RUN_TEST(testUpdatesRequireANewerVersion);
    UNITY_END();
}

void loop() {}
