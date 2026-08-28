#pragma once
#include <LittleFS.h>

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

// Ebook Filesystem - separate LittleFSFS instance
// Mounted at /ebooks, partition label "ebooks"
extern fs::LittleFSFS EbookFS;
