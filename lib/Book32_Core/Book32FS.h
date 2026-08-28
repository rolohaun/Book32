#pragma once
#include <LittleFS.h>
#include <FS.h>

// =============================================================================
// Book32 Filesystem Configuration
// =============================================================================
// Two LittleFS partitions are used:
//
// 1. System Partition (label: "spiffs")
//    - VFS Mount: /littlefs (default, required for uploadfs)
//    - Size: 1MB
//    - Stores: Web UI (index.html, etc.), System Config, Metadata
//    - Upload via: `pio run -t uploadfs`
//
// 2. Ebook Partition (label: "ebooks")  
//    - VFS Mount: /ebooks
//    - Size: 10MB
//    - Stores: EPUB files and reader state; covers are decoded from EPUBs on demand
//    - Managed via Web UI upload
//
// POSIX file access (used by unzipLIB):
//    - System files:  /littlefs/path/to/file
//    - Ebook files:   /ebooks/path/to/file
//
// Arduino FS API access (File class):
//    - System files:  SystemFS.open("/path/to/file")
//    - Ebook files:   EbookFS.open("/path/to/file")
// =============================================================================

// System Filesystem - uses the default LittleFS singleton
// Mounted at /littlefs, partition label "spiffs"
#define SystemFS LittleFS

// Ebook storage is a separate LittleFS instance on the original Book32. On
// Sticky it points to MicroSD when a card is present, otherwise to the internal
// fallback LittleFS partition. Both are mounted at /ebooks for unzipLIB.
extern fs::FS* EbookFSPtr;
#define EbookFS (*EbookFSPtr)

bool beginEbookStorage(bool internalPartitionBlank);
uint64_t ebookStorageTotalBytes();
uint64_t ebookStorageUsedBytes();
bool ebookStorageUsesSD();
