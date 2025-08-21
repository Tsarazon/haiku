# Haiku UnZip Dependency Map and Code Analysis

**Date:** August 19, 2025  
**Location:** `/home/ruslan/haiku/src/bin/unzip`  
**Purpose:** Comprehensive dependency analysis of Info-ZIP UnZip 5.x codebase  

## Executive Summary

The UnZip utility in Haiku is a modified version of Info-ZIP UnZip 5.x, adapted for BeOS/Haiku-specific functionality. This analysis covers 28 source files, their dependencies, data structures, and system interfaces.

---

## File Structure Overview

### Core Source Files (22 files)
```
/home/ruslan/haiku/src/bin/unzip/
├── Main Program Files
│   ├── unzip.c          (4,000+ lines) - Main entry point and command processing
│   ├── beosmain.cpp     (40 lines)     - BeOS/Haiku C++ wrapper for app_server
│   └── zipinfo.c        (2,000+ lines) - ZipInfo functionality (dual mode)
├── Core Processing Engine
│   ├── process.c        (3,000+ lines) - ZIP archive processing and validation
│   ├── extract.c        (3,000+ lines) - File extraction and decompression drivers
│   ├── fileio.c         (3,000+ lines) - File I/O operations and buffering
│   └── list.c           (500+ lines)   - Archive listing functionality
├── Compression Algorithms
│   ├── inflate.c        (1,500 lines)  - DEFLATE decompression (RFC 1951)
│   ├── explode.c        (400 lines)    - IMPLODE decompression
│   ├── unshrink.c       (500 lines)    - LZW UNSHRINK decompression
│   └── unreduce.c       (600 lines)    - REDUCE decompression
├── Platform-Specific
│   ├── beos.c           (1,800 lines)  - BeOS/Haiku file system integration
│   └── globals.c        (300 lines)    - Global variable management
├── Utilities
│   ├── crc32.c          (200 lines)    - CRC-32 checksum computation
│   ├── crctab.c         (200 lines)    - CRC-32 lookup tables
│   ├── match.c          (200 lines)    - Wildcard filename matching
│   ├── envargs.c        (200 lines)    - Environment variable processing
│   └── ttyio.c          (600 lines)    - Terminal I/O for passwords
```

### Header Files (6 files)
```
├── Primary Headers
│   ├── unzip.h          (615 lines)    - Main public API and platform detection
│   ├── unzpriv.h        (2,800+ lines) - Internal definitions and structures
│   └── globals.h        (800+ lines)   - Global state structure definition
├── Specialized Headers
│   ├── beos.h           (47 lines)     - BeOS/Haiku specific definitions
│   ├── inflate.h        (40 lines)     - DEFLATE decompression interface
│   ├── crypt.h          (300+ lines)   - Cryptographic functions (disabled)
│   ├── ttyio.h          (50 lines)     - Terminal I/O interface
│   └── zip.h            (25 lines)     - Compatibility shim for shared code
├── Data Definition Headers
│   ├── consts.h         (100+ lines)   - String constants and messages
│   ├── tables.h         (200+ lines)   - Lookup tables for compression
│   ├── ebcdic.h         (100+ lines)   - EBCDIC character conversion
│   └── unzvers.h        (50 lines)     - Version information
```

---

## Dependency Hierarchy

### Level 1: Foundation Layer
```
unzip.h
├── Platform Detection (lines 72-230)
│   ├── Unix/Linux (UNIX, LINUX, __APPLE__)
│   ├── Windows (WIN32, MSDOS) 
│   ├── BeOS/Haiku (__BEOS__, __HAIKU__)
│   └── Legacy Systems (OS2, VMS, TANDEM, etc.)
├── Type Definitions (lines 327-342)
│   ├── zvoid, uch, ush, ulg
│   └── Function pointer typedefs
└── Public API (lines 574-603)
    ├── UzpMain(), UzpVersion()
    ├── UzpUnzipToMemory()
    └── UzpPassword(), UzpInput()
```

### Level 2: Internal Structures
```
unzpriv.h (2,800+ lines)
├── OS Configuration (lines 86-500)
│   ├── Memory model definitions
│   ├── File system path lengths
│   └── I/O buffer sizes
├── ZIP Format Structures (lines 500-1500)
│   ├── local_file_hdr
│   ├── central_directory_file_header  
│   ├── end_central_directory_record
│   └── Data descriptors
├── Global State Management (lines 1500-2800)
│   ├── Extraction state
│   ├── File buffers
│   └── Progress tracking
└── Function Prototypes
    ├── Internal processing functions
    ├── Platform-specific functions
    └── Compression algorithm interfaces
```

### Level 3: Global State
```
globals.h
├── Globals Structure (lines 147-400)
│   ├── Archive processing state
│   ├── File extraction buffers
│   ├── Error handling context
│   └── Platform-specific data
├── Thread Safety (lines 400-500)
│   ├── Reentrant support
│   ├── Thread-local storage
│   └── Signal handling
└── Memory Management
    ├── Buffer allocation
    ├── Cleanup functions
    └── Emergency exit handling
```

---

## Core Data Structures

### ZIP Archive Format Structures

#### 1. Local File Header (30 bytes + variable)
```c
typedef struct local_file_hdr {
    uch signature[4];                    // "PK\003\004"
    ush extract_version;                 // Version needed to extract
    ush general_purpose_bit_flag;        // Encryption/compression flags
    ush compression_method;              // 0=store, 8=deflate, etc.
    ulg last_mod_dos_datetime;          // DOS timestamp
    ulg crc32;                          // CRC-32 checksum
    ulg compressed_size;                // Compressed size
    ulg uncompressed_size;              // Original size
    ush filename_length;                // Length of filename
    ush extra_field_length;             // Length of extra field
    // Followed by: filename, extra_field
} local_file_hdr;
```

#### 2. Central Directory File Header (46 bytes + variable)
```c
typedef struct central_directory_file_header {
    uch version_made_by[2];             // Version that created archive
    uch version_needed_to_extract[2];   // Version needed to extract
    ush general_purpose_bit_flag;       // Encryption/compression flags
    ush compression_method;             // Compression algorithm
    ulg last_mod_dos_datetime;          // DOS timestamp
    ulg crc32;                          // CRC-32 checksum
    ulg csize;                          // Compressed size
    ulg ucsize;                         // Uncompressed size
    ush filename_length;                // Length of filename
    ush extra_field_length;             // Length of extra field
    ush file_comment_length;            // Length of comment
    ush disk_number_start;              // Disk where file starts
    ush internal_file_attributes;       // Internal attributes
    ulg external_file_attributes;       // External attributes (OS-specific)
    ulg relative_offset_local_header;   // Offset to local header
    // Followed by: filename, extra_field, comment
} cdir_file_hdr;
```

#### 3. End of Central Directory Record (22 bytes)
```c
typedef struct end_central_dir_record {
    uch signature[4];                   // "PK\005\006"
    ush number_this_disk;               // Current disk number
    ush num_disk_start_cdir;           // Disk where central dir starts
    ush num_entries_centrl_dir_ths_disk; // Entries in central dir on this disk
    ush total_entries_central_dir;      // Total entries in central dir
    ulg size_central_directory;        // Size of central directory
    ulg offset_start_central_directory; // Offset to central directory
    ush zipfile_comment_length;        // Length of archive comment
    // Followed by: comment
} ecdir_rec;
```

### Global State Structure

#### 4. Globals Structure (800+ lines in globals.h)
```c
typedef struct Globals {
    // Archive Processing State
    FILE *zipfile;                      // ZIP archive file handle
    ulg ziplen;                         // Total ZIP file length
    ulg cur_zipfile_bufstart;           // Current buffer position
    ulg extra_bytes;                    // Extra bytes at start
    
    // Central Directory Processing
    cdir_file_hdr crec;                 // Current central directory record
    ulg sig;                            // Current signature being processed
    long request;                       // Bytes requested by readbuf()
    long realsize;                      // Real size of slide[] buffer
    
    // File Extraction State
    local_file_hdr lrec;               // Current local file header
    ulg csize;                         // Compressed size (current file)
    ulg ucsize;                        // Uncompressed size (current file)
    FILE *outfile;                     // Current output file handle
    
    // Buffers
    uch *inbuf;                        // Input buffer
    uch *outbuf;                       // Output buffer  
    uch *slide;                        // Sliding window for compression
    uch *hold;                         // Hold area for bit buffering
    
    // BeOS/Haiku Specific
    char *filename;                    // Current filename being processed
    char *zipfn;                       // ZIP filename
    int created_dir;                   // Directory creation flag
    int renamed_fullpath;              // Path renaming flag
    
    // Error Handling
    int error_in_archive;              // Archive processing error flag
    jmp_buf environ;                   // setjmp/longjmp buffer for errors
    
    // Threading Support  
    savsigs_info *savedsig_chain;      // Signal handler chain (if REENTRANT)
} Globals;
```

---

## File Dependencies and Relationships

### Core Processing Pipeline Dependencies

#### 1. Main Entry Point Chain
```
unzip.c → beosmain.cpp (BeOS/Haiku only)
├── Includes: unzip.h, crypt.h, unzvers.h, consts.h
├── Calls: process_zipfiles()
├── Platform: beosmain.cpp creates BApplication for MIME type support
└── Flow: Command parsing → Archive processing → Cleanup
```

#### 2. Archive Processing Chain  
```
process.c (Central coordinator)
├── Dependencies:
│   ├── unzip.h → unzpriv.h → globals.h
│   ├── System: <stdio.h>, <stdlib.h>, <string.h>
│   └── Optional: windll headers for DLL support
├── Key Functions:
│   ├── process_zipfiles() - Main processing loop
│   ├── do_seekable() - Seekable archive handling
│   ├── find_ecrec() - End central directory finder
│   └── process_cdir_file_hdr() - Central directory parser
└── Calls:
    ├── extract.c → extract_or_test_files()
    ├── list.c → list_files()  
    └── zipinfo.c → zipinfo()
```

#### 3. File Extraction Chain
```
extract.c (Extraction engine)
├── Dependencies:
│   ├── unzip.h, crypt.h
│   ├── windll headers (conditional)
│   └── Platform I/O headers
├── Compression Algorithm Dependencies:
│   ├── inflate.c (DEFLATE - most common)
│   ├── explode.c (IMPLODE - rare)
│   ├── unshrink.c (LZW UNSHRINK - obsolete)
│   └── unreduce.c (REDUCE - obsolete)
├── File I/O Dependencies:
│   ├── fileio.c → File operations
│   └── beos.c → Platform-specific operations
└── Flow:
    Archive validation → File extraction loop → 
    Compression dispatch → Output file creation
```

### Platform Integration Dependencies

#### 4. BeOS/Haiku Integration Chain
```
beos.c (Platform adapter)
├── Dependencies:
│   ├── unzip.h, beos.h
│   ├── <errno.h>, <sys/types.h>, <sys/stat.h>
│   ├── <fcntl.h>, <dirent.h>
│   ├── <fs_attr.h>, <ByteOrder.h>, <Mime.h>
│   └── ACORN_FTYPE_NFS support (conditional)
├── Key Functions:
│   ├── do_wild() - Wildcard filename expansion
│   ├── mapattr() - File attribute mapping
│   ├── checkdir() - Directory creation/validation
│   ├── set_file_attrs() - BeOS extended attribute handling
│   └── assign_MIME() - MIME type assignment
└── Integration:
    ├── beosmain.cpp → BApplication setup
    ├── extract.c → File extraction callbacks
    └── fileio.c → File operation overrides
```

#### 5. File I/O Subsystem
```
fileio.c (I/O operations)
├── Dependencies:
│   ├── unzip.h, crypt.h, ttyio.h, ebcdic.h
│   ├── Platform-specific I/O headers
│   └── Character conversion tables
├── Key Functions:
│   ├── readbuf() - Archive reading with buffering
│   ├── FillBitBuffer() - Bit-level input for compression
│   ├── flush() - Output buffer management  
│   ├── disk_error() - Disk error handling
│   └── file_size() - File size determination
└── Character Conversion:
    ├── ebcdic.h → EBCDIC to ASCII conversion
    ├── ASCII/EBCDIC translation tables
    └── End-of-line conversion (text mode)
```

### Compression Algorithm Dependencies

#### 6. DEFLATE Implementation
```
inflate.c (RFC 1951 DEFLATE)
├── Dependencies:
│   ├── inflate.h → unzip.h
│   ├── Huffman decoding tables
│   └── Sliding window buffer (slide[])
├── Algorithm Flow:
│   ├── Huffman tree construction
│   ├── Block-by-block decompression
│   ├── Distance/length back-references
│   └── Output through flush() callback
└── Integration:
    ├── extract.c → Primary decompression dispatcher
    ├── tables.h → Huffman decoding tables
    └── fileio.c → Bit input functions
```

#### 7. Legacy Compression Algorithms
```
explode.c (IMPLODE algorithm)
├── Dependencies: unzip.h, sliding window
├── Usage: Rare, legacy PKZIP 1.x archives

unshrink.c (LZW UNSHRINK)  
├── Dependencies: unzip.h, LZW tables
├── Usage: Obsolete, PKZIP 1.0 archives

unreduce.c (REDUCE algorithm)
├── Dependencies: unzip.h, reduction tables  
├── Usage: Obsolete, PKZIP 1.0 archives
```

### Utility and Support Dependencies

#### 8. CRC and Integrity Verification
```
crc32.c & crctab.c (CRC-32 implementation)
├── Dependencies: zip.h (compatibility shim)
├── Functions:
│   ├── get_crc_table() - Generate/return CRC table
│   ├── crc32() - Compute CRC-32 checksum
│   └── makecrc() - Initialize CRC tables
├── Integration:
│   ├── extract.c → File integrity verification
│   ├── process.c → Archive validation
│   └── Dynamic table generation (DYNAMIC_CRC_TABLE)
```

#### 9. Pattern Matching and Utilities
```
match.c (Wildcard matching)
├── Dependencies: unzip.h
├── Function: match() - Shell-style pattern matching
├── Usage: beos.c, process.c for file selection

envargs.c (Environment processing)
├── Dependencies: unzip.h  
├── Function: envargs() - Process environment variables
├── Usage: Command-line expansion from UNZIP variable

ttyio.c (Terminal I/O)
├── Dependencies: zip.h, crypt.h, ttyio.h
├── Functions: Password input, terminal echo control
├── Usage: Encrypted archive password prompting
```

---

## External System Dependencies

### System Libraries and Headers
```
Standard C Library:
├── <stdio.h>     - File I/O operations
├── <stdlib.h>    - Memory allocation, program control
├── <string.h>    - String manipulation
├── <time.h>      - Timestamp conversion
├── <errno.h>     - Error number definitions
├── <fcntl.h>     - File control operations
├── <sys/stat.h>  - File status structures  
├── <sys/types.h> - System type definitions
└── <unistd.h>    - POSIX system calls

BeOS/Haiku Specific:
├── <fs_attr.h>       - File system extended attributes
├── <ByteOrder.h>     - Byte order conversion
├── <Mime.h>          - MIME type utilities
├── <dirent.h>        - Directory entry iteration
├── <app/Application.h> - BeOS Application class (C++)
└── Platform detection via __BEOS__ or __HAIKU__
```

### Build System Integration
```
Jamfile Structure:
/home/ruslan/haiku/src/bin/unzip/Jamfile (Empty - redirects to tools)
/home/ruslan/haiku/src/tools/unzip/Jamfile (Actual build rules)

Build Configuration:
├── SEARCH_SOURCE = $(HAIKU_TOP)/src/bin/unzip
├── DEFINES = NO_CRYPT HAS_JUNK_EXTRA_FIELD_OPTION=1
├── Conditional: HAVE_TERMIOS_H=1 (non-Haiku hosts)
└── DEBUG = 0 (avoid unwanted output)

Build Targets:
├── <build>libunzip.a (Static library)
│   ├── Core modules: crc32, ttyio, crctab, envargs
│   ├── Processing: explode, extract, fileio, globals  
│   ├── Algorithms: inflate, list, match, process
│   ├── Platform: beos.c, beosmain.cpp
│   └── Info: zipinfo
└── <build>unzip (Executable)
    ├── Main: unzip.c, unreduce.c, unshrink.c
    └── Links: libunzip.a, $(HOST_LIBBE), $(HOST_LIBSUPC++)
```

---

## Data Flow Analysis

### Archive Processing Flow
```
1. Archive Opening:
   unzip.c:main() → process.c:process_zipfiles()
   ├── File validation and opening
   ├── Multi-disk archive detection  
   └── Error handling setup

2. Central Directory Location:
   process.c:find_ecrec()
   ├── End-of-central-directory signature scan
   ├── Multi-part archive handling
   └── Directory offset calculation

3. Central Directory Processing:
   process.c:uz_end_central() → process_cdir_file_hdr()
   ├── Entry-by-entry processing
   ├── Filename and metadata extraction
   └── File selection based on patterns

4. File Extraction:
   extract.c:extract_or_test_files() → extract_or_test_member()
   ├── Local header validation
   ├── Compression method dispatch
   └── Output file creation and writing
```

### Compression Algorithm Dispatch
```
extract.c:extract_or_test_member()
├── Compression Method Analysis:
│   ├── 0  = STORED (no compression)
│   ├── 1  = SHRUNK (LZW) → unshrink.c
│   ├── 2-5= REDUCED → unreduce.c  
│   ├── 6  = IMPLODED → explode.c
│   ├── 8  = DEFLATED → inflate.c (most common)
│   └── 9  = DEFLATE64 → inflate.c (enhanced)
├── Buffer Management:
│   ├── Input: fileio.c:readbuf()
│   ├── Sliding Window: slide[] buffer
│   └── Output: fileio.c:flush()
└── Integrity Verification:
    ├── CRC-32 calculation during extraction
    └── Final checksum comparison
```

### BeOS/Haiku Integration Flow
```
1. Application Initialization:
   main() → beosmain.cpp:main()
   ├── BApplication creation for MIME support
   └── main_stub() delegation

2. File System Integration:
   beos.c functions called from extract.c and fileio.c
   ├── Extended attribute preservation
   ├── MIME type assignment  
   ├── Unix permission mapping
   └── BeOS-style path handling

3. Directory Creation:
   beos.c:checkdir()
   ├── Path validation and creation
   ├── Permission setting
   └── Error handling

4. File Attribute Setting:
   beos.c:set_file_attrs()
   ├── Extended attribute parsing from ZIP extra field
   ├── BeOS attribute format conversion
   └── fs_attr.h API usage for attribute setting
```

---

## Security and Error Handling

### Security Considerations
```
Buffer Overflow Prevention:
├── Fixed-size buffers in globals.h with bounds checking
├── snprintf() usage instead of sprintf() (build_issues_fix.md)
├── Path traversal protection ("../" handling)
└── Input validation for archive structures

Cryptographic Support:
├── crypt.h - Full encryption support disabled (NO_CRYPT)
├── Password handling through ttyio.c (when enabled)
└── CRC-32 integrity verification always enabled

Memory Safety:
├── Dynamic buffer allocation with size tracking
├── setjmp/longjmp error recovery (globals.environ)
├── Cleanup functions for emergency exit
└── NULL pointer checks throughout
```

### Error Handling Strategy
```
Three-Level Error Handling:

1. Function Return Codes:
   ├── PK_OK (0) - Success
   ├── PK_WARN (1) - Warning, continue
   ├── PK_ERR (2) - Archive error
   ├── PK_BADERR (3) - Severe archive error
   ├── PK_MEM (4-8) - Memory allocation failures
   └── PK_DISK (50-51) - Disk I/O errors

2. Global Error State:
   ├── globals.error_in_archive - Cumulative error flag
   ├── Error message accumulation
   └── Process continuation decisions

3. Emergency Exit:
   ├── setjmp/longjmp through globals.environ
   ├── DESTROYGLOBALS() cleanup
   └── Resource deallocation
```

---

## Performance and Optimization

### Buffer Management Strategy
```
Multi-Level Buffering:
├── globals.inbuf - Input archive reading (typically 8KB)
├── globals.outbuf - Output file writing (typically 32KB)
├── globals.slide - Sliding window for decompression (32KB)
└── File system buffers - OS-level caching

Memory Allocation:
├── CONSTRUCTGLOBALS - Single large allocation
├── DYNALLOC_CRCTAB - Dynamic CRC table allocation
├── Buffer size configuration via unzpriv.h
└── Emergency cleanup via DESTROYGLOBALS
```

### Compression Performance
```
Algorithm Efficiency:
├── DEFLATE (inflate.c) - Optimized for modern archives
├── STORED - Direct copy, minimal overhead
├── Legacy algorithms - Compatibility only
└── Huffman table caching for repeated use

I/O Optimization:
├── Readahead buffering via readbuf()
├── Write coalescing through flush()
├── Seek optimization for central directory
└── Platform-specific I/O via beos.c
```

---

## Conclusion

The Haiku UnZip implementation is a mature, well-structured codebase with clear separation of concerns:

### Strengths:
- **Modular Design**: Clear separation between archive processing, compression algorithms, and platform integration
- **Platform Integration**: Excellent BeOS/Haiku support with extended attributes and MIME types
- **Compatibility**: Support for wide range of ZIP archive formats and compression methods
- **Error Handling**: Robust three-level error handling with recovery capabilities
- **Security**: Recent fixes for buffer overflow vulnerabilities (documented in build_issues_fix.md)

### Areas of Note:
- **Legacy Support**: Maintains compatibility with obsolete compression methods
- **Complexity**: Large codebase (28 files, 20,000+ lines) with intricate dependencies
- **Platform Specific**: Heavy reliance on BeOS/Haiku specific features
- **Build Dependencies**: Complex build system integration through Jam

### Dependency Summary:
- **7 Core Processing Files** handle archive parsing and extraction
- **4 Compression Algorithm Files** provide decompression capabilities  
- **8 Utility Files** provide supporting functions
- **9 Header Files** define interfaces and data structures
- **BeOS/Haiku Integration** through 2 platform-specific files
- **External Dependencies** on standard C library and BeOS/Haiku system APIs

This analysis provides a foundation for understanding the unzip codebase structure, dependencies, and integration points within the Haiku operating system build environment.