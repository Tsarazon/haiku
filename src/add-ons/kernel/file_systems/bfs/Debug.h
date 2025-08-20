/*
 * Copyright 2001-2008, Axel Dörfler, axeld@pinc-software.de.
 * This file may be used under the terms of the MIT License.
 */
#ifndef DEBUG_H
#define DEBUG_H


#include "system_dependencies.h"

#ifdef USER
#	define __out printf
#else
#	define __out dprintf
#endif

// Which debugger should be used when?
// The DEBUGGER() macro actually has no effect if DEBUG is not defined,
// use the DIE() macro if you really want to die.
#ifdef DEBUG
#	ifdef USER
#		define DEBUGGER(x) debugger x
#	else
#		define DEBUGGER(x) kernel_debugger x
#	endif
#else
#	define DEBUGGER(x) ;
#endif

#ifdef USER
#	define DIE(x) debugger x
#else
#	define DIE(x) kernel_debugger x
#endif

// Short overview over the debug output macros:
//	PRINT()
//		is for general messages that very unlikely should appear in a release build
//	FATAL()
//		this is for fatal messages, when something has really gone wrong
//	INFORM()
//		general information, as disk size, etc.
//	REPORT_ERROR(status_t)
//		prints out error information
//	RETURN_ERROR(status_t)
//		calls REPORT_ERROR() and return the value
//	D()
//		the statements in D() are only included if DEBUG is defined

#if DEBUG
	#define PRINT(x) { __out("bfs: "); __out x; }
	#define REPORT_ERROR(status) \
		__out("bfs: %s:%d: %s\n", __FUNCTION__, __LINE__, strerror(status));
	#define RETURN_ERROR(err) { status_t _status = err; if (_status < B_OK) REPORT_ERROR(_status); return _status;}
	#define FATAL(x) { __out("bfs: "); __out x; }
	#define INFORM(x) { __out("bfs: "); __out x; }
//	#define FUNCTION() __out("bfs: %s()\n",__FUNCTION__);
	#define FUNCTION_START(x) { __out("bfs: %s() ",__FUNCTION__); __out x; }
	#define FUNCTION() ;
//	#define FUNCTION_START(x) ;
	#define D(x) {x;};
	#ifndef ASSERT
	#	define ASSERT(x) { if (!(x)) DEBUGGER(("bfs: assert failed: " #x "\n")); }
	#endif
#else
	#define PRINT(x) ;
	#define REPORT_ERROR(status) \
		__out("bfs: %s:%d: %s\n", __FUNCTION__, __LINE__, strerror(status));
	#define RETURN_ERROR(err) { return err; }
//	#define FATAL(x) { panic x; }
	#define FATAL(x) { __out("bfs: "); __out x; }
	#define INFORM(x) { __out("bfs: "); __out x; }
	#define FUNCTION() ;
	#define FUNCTION_START(x) ;
	#define D(x) ;
	#ifndef ASSERT
	#	define ASSERT(x) { if (!(x)) DEBUGGER(("bfs: assert failed: " #x "\n")); }
//	#	define ASSERT(x) ;
	#endif
#endif

#ifdef DEBUG
	struct block_run;
	struct bplustree_header;
	struct bplustree_node;
	struct data_stream;
	struct bfs_inode;
	struct disk_super_block;
	class Inode;
	class Volume;

	// some structure dump functions
	extern void dump_block_run(const char *prefix, const block_run &run);
	extern void dump_super_block(const disk_super_block *superBlock);
	extern void dump_data_stream(const data_stream *stream);
	extern void dump_inode(const bfs_inode *inode);
	extern void dump_bplustree_header(const bplustree_header *header);
	extern void dump_bplustree_node(const bplustree_node *node,
					const bplustree_header *header = NULL, Volume *volume = NULL);
	extern void dump_block(const char *buffer, int size);
	
	// Enhanced validation and analysis functions
#if defined(DEBUG) || defined(BFS_DEBUGGER_COMMANDS)
	extern bool validate_inode_structure(const bfs_inode* inode);
	extern bool validate_btree_node_structure(const bplustree_node* node, 
											  const bplustree_header* header);
	extern void analyze_data_integrity(const void* data, size_t size, const char* description);
	extern void print_hex_dump(const void* data, size_t size, const char* description);
#endif
#endif
#ifdef BFS_DEBUGGER_COMMANDS
	extern void remove_debugger_commands();
	extern void add_debugger_commands();
#endif

// Enhanced debugging macros that extend the basic ones
#ifdef DEBUG
	// Enhanced error reporting with categorization
	#define BFS_ERROR(category, status) \
		do { \
			__out("bfs[%s]: ERROR in %s:%d: %s\n", \
				#category, __FUNCTION__, __LINE__, strerror(status)); \
		} while (0)
	
	#define BFS_WARNING(category, msg, ...) \
		do { \
			__out("bfs[%s]: WARNING in %s:%d: " msg "\n", \
				#category, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
		} while (0)
	
	#define BFS_TRACE(category, msg, ...) \
		do { \
			__out("bfs[%s]: TRACE %s:%d: " msg "\n", \
				#category, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
		} while (0)
	
	// Performance timing macros
	#define BFS_PERF_START(name) \
		bigtime_t _perf_start_##name = system_time()
	
	#define BFS_PERF_END(name) \
		do { \
			bigtime_t _duration = system_time() - _perf_start_##name; \
			__out("bfs: PERF %s took %" B_PRId64 " µs in %s:%d\n", \
				#name, _duration, __FUNCTION__, __LINE__); \
		} while (0)
	
	// Validation macros
	#define BFS_VALIDATE_POINTER(ptr, name) \
		do { \
			if ((ptr) == NULL) { \
				__out("bfs: VALIDATION ERROR: NULL pointer %s in %s:%d\n", \
					#name, __FUNCTION__, __LINE__); \
				DEBUGGER(("BFS: NULL pointer validation failed: " #name "\n")); \
			} \
		} while (0)
	
	#define BFS_VALIDATE_RANGE(value, min, max, name) \
		do { \
			if ((value) < (min) || (value) > (max)) { \
				__out("bfs: VALIDATION ERROR: %s=%ld out of range [%ld, %ld] in %s:%d\n", \
					#name, (long)(value), (long)(min), (long)(max), __FUNCTION__, __LINE__); \
				DEBUGGER(("BFS: range validation failed: " #name "\n")); \
			} \
		} while (0)
#else
	// Release mode - minimal overhead
	#define BFS_ERROR(category, status) \
		__out("bfs: ERROR: %s\n", strerror(status))
	
	#define BFS_WARNING(category, msg, ...) ;
	#define BFS_TRACE(category, msg, ...) ;
	#define BFS_PERF_START(name) ;
	#define BFS_PERF_END(name) ;
	#define BFS_VALIDATE_POINTER(ptr, name) ;
	#define BFS_VALIDATE_RANGE(value, min, max, name) ;
#endif

// Unified debugging categories
#define BFS_CAT_INODE     "inode"
#define BFS_CAT_BTREE     "btree"
#define BFS_CAT_ALLOCATOR "allocator"
#define BFS_CAT_JOURNAL   "journal"
#define BFS_CAT_QUERY     "query"
#define BFS_CAT_ATTRIBUTE "attribute"
#define BFS_CAT_VOLUME    "volume"
#define BFS_CAT_CACHE     "cache"
#define BFS_CAT_LOCK      "lock"
#define BFS_CAT_MEMORY    "memory"

// Convenience macros for common operations
#define BFS_INODE_ERROR(status)     BFS_ERROR(BFS_CAT_INODE, status)
#define BFS_BTREE_ERROR(status)     BFS_ERROR(BFS_CAT_BTREE, status)
#define BFS_ALLOCATOR_ERROR(status) BFS_ERROR(BFS_CAT_ALLOCATOR, status)
#define BFS_JOURNAL_ERROR(status)   BFS_ERROR(BFS_CAT_JOURNAL, status)
#define BFS_VOLUME_ERROR(status)    BFS_ERROR(BFS_CAT_VOLUME, status)

#define BFS_INODE_WARNING(msg, ...) BFS_WARNING(BFS_CAT_INODE, msg, ##__VA_ARGS__)
#define BFS_BTREE_WARNING(msg, ...) BFS_WARNING(BFS_CAT_BTREE, msg, ##__VA_ARGS__)
#define BFS_VOLUME_WARNING(msg, ...) BFS_WARNING(BFS_CAT_VOLUME, msg, ##__VA_ARGS__)

#define BFS_INODE_TRACE(msg, ...)   BFS_TRACE(BFS_CAT_INODE, msg, ##__VA_ARGS__)
#define BFS_BTREE_TRACE(msg, ...)   BFS_TRACE(BFS_CAT_BTREE, msg, ##__VA_ARGS__)
#define BFS_JOURNAL_TRACE(msg, ...) BFS_TRACE(BFS_CAT_JOURNAL, msg, ##__VA_ARGS__)

// Centralized Magic Number Management
#ifdef DEBUG
	// Magic number validation macros
	#define BFS_VALIDATE_SUPERBLOCK_MAGIC(sb) \
		do { \
			if (!validate_superblock_magic(sb)) { \
				BFS_VOLUME_ERROR(B_BAD_DATA); \
				DEBUGGER(("BFS: Invalid superblock magic numbers")); \
			} \
		} while (0)
	
	#define BFS_VALIDATE_INODE_MAGIC(inode) \
		do { \
			if (!validate_inode_magic(inode)) { \
				BFS_INODE_ERROR(B_BAD_DATA); \
				DEBUGGER(("BFS: Invalid inode magic number")); \
			} \
		} while (0)
	
	#define BFS_VALIDATE_BTREE_MAGIC(header) \
		do { \
			if (!validate_btree_magic(header)) { \
				BFS_BTREE_ERROR(B_BAD_DATA); \
				DEBUGGER(("BFS: Invalid B+tree magic number")); \
			} \
		} while (0)
#else
	#define BFS_VALIDATE_SUPERBLOCK_MAGIC(sb) ;
	#define BFS_VALIDATE_INODE_MAGIC(inode) ;
	#define BFS_VALIDATE_BTREE_MAGIC(header) ;
#endif

// Centralized magic number utilities (always available)
#if defined(DEBUG) || defined(BFS_DEBUGGER_COMMANDS)
	// Forward declarations for magic number utilities
	struct disk_super_block;
	struct bfs_inode;  
	struct bplustree_header;
	
	extern bool validate_superblock_magic(const disk_super_block* superblock);
	extern bool validate_inode_magic(const bfs_inode* inode);
	extern bool validate_btree_magic(const bplustree_header* header);
	extern const char* get_magic_string(uint32 magic);
	extern void dump_all_magic_numbers(void);
#endif

#endif	/* DEBUG_H */
