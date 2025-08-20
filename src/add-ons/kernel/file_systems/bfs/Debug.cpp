/*
 * Copyright 2001-2009, Axel Dörfler, axeld@pinc-software.de.
 * Some code is based on work previously done by Marcus Overhagen.
 *
 * This file may be used under the terms of the MIT License.
 */


#include "Debug.h"
#include "BlockAllocator.h"
#include "BPlusTree.h"
#include "Inode.h"
#include "Journal.h"
#include "bfs_control.h"


char*
get_tupel(uint32 id)
{
	static unsigned char tupel[5];

	tupel[0] = 0xff & (id >> 24);
	tupel[1] = 0xff & (id >> 16);
	tupel[2] = 0xff & (id >> 8);
	tupel[3] = 0xff & (id);
	tupel[4] = 0;
	for (int16 i = 0;i < 4;i++) {
		if (tupel[i] < ' ' || tupel[i] > 128)
			tupel[i] = '.';
	}

	return (char*)tupel;
}


void
dump_block_run(const char* prefix, const block_run& run)
{
	kprintf("%s(%d, %d, %d)\n", prefix, (int)run.allocation_group, run.start,
		run.length);
}


void
dump_super_block(const disk_super_block* superBlock)
{
	kprintf("disk_super_block:\n");
	kprintf("  name           = %s\n", superBlock->name);
	kprintf("  magic1         = %#08x (%s) %s\n", (int)superBlock->Magic1(),
		get_tupel(superBlock->magic1),
		(superBlock->magic1 == SUPER_BLOCK_MAGIC1 ? "valid" : "INVALID"));
	kprintf("  fs_byte_order  = %#08x (%s)\n", (int)superBlock->fs_byte_order,
		get_tupel(superBlock->fs_byte_order));
	kprintf("  block_size     = %u\n", (unsigned)superBlock->BlockSize());
	kprintf("  block_shift    = %u\n", (unsigned)superBlock->BlockShift());
	kprintf("  num_blocks     = %" B_PRIdOFF "\n", superBlock->NumBlocks());
	kprintf("  used_blocks    = %" B_PRIdOFF "\n", superBlock->UsedBlocks());
	kprintf("  inode_size     = %u\n", (unsigned)superBlock->InodeSize());
	kprintf("  magic2         = %#08x (%s) %s\n", (int)superBlock->Magic2(),
		get_tupel(superBlock->magic2),
		(superBlock->magic2 == (int)SUPER_BLOCK_MAGIC2 ? "valid" : "INVALID"));
	kprintf("  blocks_per_ag  = %u\n",
		(unsigned)superBlock->BlocksPerAllocationGroup());
	kprintf("  ag_shift       = %u (%ld bytes)\n",
		(unsigned)superBlock->AllocationGroupShift(),
		1L << superBlock->AllocationGroupShift());
	kprintf("  num_ags        = %u\n", (unsigned)superBlock->AllocationGroups());
	kprintf("  flags          = %#08x (%s)\n", (int)superBlock->Flags(),
		get_tupel(superBlock->Flags()));
	dump_block_run("  log_blocks     = ", superBlock->log_blocks);
	kprintf("  log_start      = %" B_PRIdOFF "\n", superBlock->LogStart());
	kprintf("  log_end        = %" B_PRIdOFF "\n", superBlock->LogEnd());
	kprintf("  magic3         = %#08x (%s) %s\n", (int)superBlock->Magic3(),
		get_tupel(superBlock->magic3),
		(superBlock->magic3 == SUPER_BLOCK_MAGIC3 ? "valid" : "INVALID"));
	dump_block_run("  root_dir       = ", superBlock->root_dir);
	dump_block_run("  indices        = ", superBlock->indices);
}


void
dump_data_stream(const data_stream* stream)
{
	kprintf("data_stream:\n");
	for (int i = 0; i < NUM_DIRECT_BLOCKS; i++) {
		if (!stream->direct[i].IsZero()) {
			kprintf("  direct[%02d]                = ", i);
			dump_block_run("", stream->direct[i]);
		}
	}
	kprintf("  max_direct_range          = %" B_PRIdOFF "\n",
		stream->MaxDirectRange());

	if (!stream->indirect.IsZero())
		dump_block_run("  indirect                  = ", stream->indirect);

	kprintf("  max_indirect_range        = %" B_PRIdOFF "\n",
		stream->MaxIndirectRange());

	if (!stream->double_indirect.IsZero()) {
		dump_block_run("  double_indirect           = ",
			stream->double_indirect);
	}

	kprintf("  max_double_indirect_range = %" B_PRIdOFF "\n",
		stream->MaxDoubleIndirectRange());
	kprintf("  size                      = %" B_PRIdOFF "\n", stream->Size());
}


void
dump_inode(const bfs_inode* inode)
{
	kprintf("inode:\n");
	kprintf("  magic1             = %08x (%s) %s\n", (int)inode->Magic1(),
		get_tupel(inode->magic1),
		(inode->magic1 == INODE_MAGIC1 ? "valid" : "INVALID"));
	dump_block_run(	"  inode_num          = ", inode->inode_num);
	kprintf("  uid                = %u\n", (unsigned)inode->UserID());
	kprintf("  gid                = %u\n", (unsigned)inode->GroupID());
	kprintf("  mode               = %08x\n", (int)inode->Mode());
	kprintf("  flags              = %08x\n", (int)inode->Flags());
	kprintf("  create_time        = %" B_PRIx64 " (%" B_PRIdTIME ".%u)\n",
		inode->CreateTime(), bfs_inode::ToSecs(inode->CreateTime()),
		(unsigned)bfs_inode::ToNsecs(inode->CreateTime()));
	kprintf("  last_modified_time = %" B_PRIx64 " (%" B_PRIdTIME ".%u)\n",
		inode->LastModifiedTime(), bfs_inode::ToSecs(inode->LastModifiedTime()),
		(unsigned)bfs_inode::ToNsecs(inode->LastModifiedTime()));
	kprintf("  status_change_time = %" B_PRIx64 " (%" B_PRIdTIME ".%u)\n",
		inode->StatusChangeTime(), bfs_inode::ToSecs(inode->StatusChangeTime()),
		(unsigned)bfs_inode::ToNsecs(inode->StatusChangeTime()));
	dump_block_run(	"  parent             = ", inode->parent);
	dump_block_run(	"  attributes         = ", inode->attributes);
	kprintf("  type               = %u\n", (unsigned)inode->Type());
	kprintf("  inode_size         = %u\n", (unsigned)inode->InodeSize());
	kprintf("  short_symlink      = %s\n",
		S_ISLNK(inode->Mode()) && (inode->Flags() & INODE_LONG_SYMLINK) == 0
			? inode->short_symlink : "-");
	dump_data_stream(&(inode->data));
	kprintf("  --\n  pad[0]             = %08x\n", (int)inode->pad[0]);
	kprintf("  pad[1]             = %08x\n", (int)inode->pad[1]);
}


void
dump_bplustree_header(const bplustree_header* header)
{
	kprintf("bplustree_header:\n");
	kprintf("  magic                = %#08x (%s) %s\n", (int)header->Magic(),
		get_tupel(header->magic),
		(header->magic == BPLUSTREE_MAGIC ? "valid" : "INVALID"));
	kprintf("  node_size            = %u\n", (unsigned)header->NodeSize());
	kprintf("  max_number_of_levels = %u\n",
		(unsigned)header->MaxNumberOfLevels());
	kprintf("  data_type            = %u\n", (unsigned)header->DataType());
	kprintf("  root_node_pointer    = %" B_PRIdOFF "\n", header->RootNode());
	kprintf("  free_node_pointer    = %" B_PRIdOFF "\n", header->FreeNode());
	kprintf("  maximum_size         = %" B_PRIdOFF "\n", header->MaximumSize());
}


#define DUMPED_BLOCK_SIZE 16

void
dump_block(const char* buffer,int size)
{
	for (int i = 0; i < size;) {
		int start = i;

		for (; i < start + DUMPED_BLOCK_SIZE; i++) {
			if (!(i % 4))
				kprintf(" ");

			if (i >= size)
				kprintf("  ");
			else
				kprintf("%02x", *(unsigned char *)(buffer + i));
		}
		kprintf("  ");

		for (i = start; i < start + DUMPED_BLOCK_SIZE; i++) {
			if (i < size) {
				char c = *(buffer + i);

				if (c < 30)
					kprintf(".");
				else
					kprintf("%c", c);
			} else
				break;
		}
		kprintf("\n");
	}
}


void
dump_bplustree_node(const bplustree_node* node, const bplustree_header* header,
	Volume* volume)
{
	kprintf("bplustree_node:\n");
	kprintf("  left_link      = %" B_PRId64 "\n", node->left_link);
	kprintf("  right_link     = %" B_PRId64 "\n", node->right_link);
	kprintf("  overflow_link  = %" B_PRId64 "\n", node->overflow_link);
	kprintf("  all_key_count  = %u\n", node->all_key_count);
	kprintf("  all_key_length = %u\n", node->all_key_length);

	if (header == NULL)
		return;

	if (node->all_key_count > node->all_key_length
		|| uint32(node->all_key_count * 10) > (uint32)header->node_size
		|| node->all_key_count == 0) {
		kprintf("\n");
		dump_block((char *)node, header->node_size/*, sizeof(off_t)*/);
		return;
	}

	kprintf("\n");
	for (int32 i = 0;i < node->all_key_count;i++) {
		uint16 length;
		char buffer[256], *key = (char *)node->KeyAt(i, &length);
		if (length > 255 || length == 0) {
			kprintf("  %2d. Invalid length (%u)!!\n", (int)i, length);
			dump_block((char *)node, header->node_size/*, sizeof(off_t)*/);
			break;
		}
		memcpy(buffer, key, length);
		buffer[length] = '\0';

		Unaligned<off_t>* value = node->Values() + i;
		if ((addr_t)value < (addr_t)node
			|| (addr_t)value > (addr_t)node + header->node_size)
			kprintf("  %2d. Invalid Offset!!\n", (int)i);
		else {
			kprintf("  %2d. ", (int)i);
			if (header->data_type == BPLUSTREE_STRING_TYPE)
				kprintf("\"%s\"", buffer);
			else if (header->data_type == BPLUSTREE_INT32_TYPE) {
				kprintf("int32 = %d (0x%x)", (int)*(int32 *)&buffer,
					(int)*(int32 *)&buffer);
			} else if (header->data_type == BPLUSTREE_UINT32_TYPE) {
				kprintf("uint32 = %u (0x%x)", (unsigned)*(uint32 *)&buffer,
					(unsigned)*(uint32 *)&buffer);
			} else if (header->data_type == BPLUSTREE_INT64_TYPE) {
				kprintf("int64 = %" B_PRId64 " (%#" B_PRIx64 ")",
					*(int64 *)&buffer, *(int64 *)&buffer);
			} else
				kprintf("???");

			off_t offset = *value & 0x3fffffffffffffffLL;
			kprintf(" (%d bytes) -> %" B_PRIdOFF, length, offset);
			if (volume != NULL) {
				block_run run = volume->ToBlockRun(offset);
				kprintf(" (%d, %d)", (int)run.allocation_group, run.start);
			}
			if (bplustree_node::LinkType(*value)
					== BPLUSTREE_DUPLICATE_FRAGMENT) {
				kprintf(" (duplicate fragment %" B_PRIdOFF ")\n",
					*value & 0x3ff);
			} else if (bplustree_node::LinkType(*value)
					== BPLUSTREE_DUPLICATE_NODE) {
				kprintf(" (duplicate node)\n");
			} else
				kprintf("\n");
		}
	}
}


//	#pragma mark - Enhanced validation functions


#if defined(DEBUG) || defined(BFS_DEBUGGER_COMMANDS)

bool
validate_inode_structure(const bfs_inode* inode)
{
	if (inode == NULL) {
		kprintf("BFS: NULL inode pointer in validation\n");
		return false;
	}
	
	// Check magic number using centralized validation
	if (!validate_inode_magic(inode)) {
		return false;
	}
	
	// Check inode size
	if (inode->InodeSize() < (int32)sizeof(bfs_inode)) {
		kprintf("BFS: Invalid inode size: %u (minimum %zu)\n",
			(unsigned)inode->InodeSize(), sizeof(bfs_inode));
		return false;
	}
	
	// Check mode
	if ((inode->Mode() & S_IFMT) == 0) {
		kprintf("BFS: Invalid file mode: %#08x\n", (int)inode->Mode());
		return false;
	}
	
	// Validate timestamps (basic sanity check)
	if (inode->CreateTime() == 0 || inode->LastModifiedTime() == 0) {
		kprintf("BFS: Invalid timestamps: create=%" B_PRId64 ", modified=%" B_PRId64 "\n",
			inode->CreateTime(), inode->LastModifiedTime());
		return false;
	}
	
	return true;
}


bool
validate_btree_node_structure(const bplustree_node* node, const bplustree_header* header)
{
	if (node == NULL || header == NULL) {
		kprintf("BFS: NULL pointer in B+tree node validation (node=%p, header=%p)\n",
			node, header);
		return false;
	}
	
	// Check key count sanity
	if (node->all_key_count > header->node_size / 8) {
		kprintf("BFS: Too many keys: %u for node size %u\n",
			node->all_key_count, (unsigned)header->node_size);
		return false;
	}
	
	// Check key length sanity
	if (node->all_key_length > header->node_size) {
		kprintf("BFS: Key length %u exceeds node size %u\n",
			node->all_key_length, (unsigned)header->node_size);
		return false;
	}
	
	// Validate space usage
	size_t requiredSpace = node->all_key_length + node->all_key_count * sizeof(off_t);
	size_t availableSpace = header->node_size - sizeof(bplustree_node);
	if (requiredSpace > availableSpace) {
		kprintf("BFS: Required space %zu exceeds available %zu\n",
			requiredSpace, availableSpace);
		return false;
	}
	
	return true;
}


void
analyze_data_integrity(const void* data, size_t size, const char* description)
{
	if (data == NULL) {
		kprintf("BFS: NULL data pointer in integrity check: %s\n", description);
		return;
	}
	
	// Simple checksum for basic integrity checking
	uint32 checksum = 0;
	const uint8* bytes = (const uint8*)data;
	for (size_t i = 0; i < size; i++) {
		checksum = (checksum << 1) ^ bytes[i];
	}
	
	kprintf("BFS: Data integrity check for %s: size=%zu, checksum=%#08x\n",
		description, size, checksum);
}


void
print_hex_dump(const void* data, size_t size, const char* description)
{
	const uint8* bytes = (const uint8*)data;
	kprintf("BFS HEX DUMP: %s (%zu bytes)\n", description, size);
	
	for (size_t i = 0; i < size; i += 16) {
		kprintf("  %04zx: ", i);
		
		// Print hex bytes
		for (size_t j = 0; j < 16; j++) {
			if (i + j < size)
				kprintf("%02x ", bytes[i + j]);
			else
				kprintf("   ");
		}
		
		kprintf(" ");
		
		// Print ASCII representation
		for (size_t j = 0; j < 16 && i + j < size; j++) {
			uint8 c = bytes[i + j];
			kprintf("%c", (c >= 32 && c < 127) ? c : '.');
		}
		
		kprintf("\n");
	}
}


//	#pragma mark - Centralized Magic Number Management


bool
validate_superblock_magic(const disk_super_block* superblock)
{
	if (superblock == NULL) {
		kprintf("BFS: NULL superblock pointer in magic validation\n");
		return false;
	}
	
	bool magic1Valid = (superblock->magic1 == SUPER_BLOCK_MAGIC1);
	bool magic2Valid = (superblock->magic2 == (int32)SUPER_BLOCK_MAGIC2);
	bool magic3Valid = (superblock->magic3 == SUPER_BLOCK_MAGIC3);
	
	if (!magic1Valid) {
		kprintf("BFS: Invalid superblock magic1: %#08x (%s), expected %#08x (%s)\n",
			(int)superblock->magic1, get_tupel(superblock->magic1),
			(int)SUPER_BLOCK_MAGIC1, get_tupel(SUPER_BLOCK_MAGIC1));
	}
	
	if (!magic2Valid) {
		kprintf("BFS: Invalid superblock magic2: %#08x (%s), expected %#08x (%s)\n",
			(int)superblock->magic2, get_tupel(superblock->magic2),
			(int)SUPER_BLOCK_MAGIC2, get_tupel(SUPER_BLOCK_MAGIC2));
	}
	
	if (!magic3Valid) {
		kprintf("BFS: Invalid superblock magic3: %#08x (%s), expected %#08x (%s)\n",
			(int)superblock->magic3, get_tupel(superblock->magic3),
			(int)SUPER_BLOCK_MAGIC3, get_tupel(SUPER_BLOCK_MAGIC3));
	}
	
	return magic1Valid && magic2Valid && magic3Valid;
}


bool
validate_inode_magic(const bfs_inode* inode)
{
	if (inode == NULL) {
		kprintf("BFS: NULL inode pointer in magic validation\n");
		return false;
	}
	
	bool valid = (inode->magic1 == INODE_MAGIC1);
	
	if (!valid) {
		kprintf("BFS: Invalid inode magic1: %#08x (%s), expected %#08x (%s)\n",
			(int)inode->magic1, get_tupel(inode->magic1),
			(int)INODE_MAGIC1, get_tupel(INODE_MAGIC1));
	}
	
	return valid;
}


bool
validate_btree_magic(const bplustree_header* header)
{
	if (header == NULL) {
		kprintf("BFS: NULL B+tree header pointer in magic validation\n");
		return false;
	}
	
	bool valid = (header->magic == BPLUSTREE_MAGIC);
	
	if (!valid) {
		kprintf("BFS: Invalid B+tree magic: %#08x (%s), expected %#08x (%s)\n",
			(int)header->magic, get_tupel(header->magic),
			(int)BPLUSTREE_MAGIC, get_tupel(BPLUSTREE_MAGIC));
	}
	
	return valid;
}


const char*
get_magic_string(uint32 magic)
{
	// Check against known BFS magic numbers
	if (magic == SUPER_BLOCK_MAGIC1)
		return "SUPER_BLOCK_MAGIC1 (BFS1)";
	if (magic == SUPER_BLOCK_MAGIC2)
		return "SUPER_BLOCK_MAGIC2";
	if (magic == SUPER_BLOCK_MAGIC3)
		return "SUPER_BLOCK_MAGIC3";
	if (magic == INODE_MAGIC1)
		return "INODE_MAGIC1";
	if (magic == BPLUSTREE_MAGIC)
		return "BPLUSTREE_MAGIC";
	if (magic == SUPER_BLOCK_FS_LENDIAN)
		return "SUPER_BLOCK_FS_LENDIAN (BIGE)";
	if (magic == SUPER_BLOCK_DISK_CLEAN)
		return "SUPER_BLOCK_DISK_CLEAN (CLEN)";
	if (magic == SUPER_BLOCK_DISK_DIRTY)
		return "SUPER_BLOCK_DISK_DIRTY (DIRT)";
	if (magic == BFS_IOCTL_CHECK_MAGIC)
		return "BFS_IOCTL_CHECK_MAGIC (BChk)";
	
	// Return tupel representation if not recognized
	return get_tupel(magic);
}


void
dump_all_magic_numbers(void)
{
	kprintf("BFS Magic Numbers Reference:\n");
	kprintf("  Superblock:\n");
	kprintf("    SUPER_BLOCK_MAGIC1      = %#08x (%s)\n", 
		(int)SUPER_BLOCK_MAGIC1, get_tupel(SUPER_BLOCK_MAGIC1));
	kprintf("    SUPER_BLOCK_MAGIC2      = %#08x (%s)\n", 
		(int)SUPER_BLOCK_MAGIC2, get_tupel(SUPER_BLOCK_MAGIC2));
	kprintf("    SUPER_BLOCK_MAGIC3      = %#08x (%s)\n", 
		(int)SUPER_BLOCK_MAGIC3, get_tupel(SUPER_BLOCK_MAGIC3));
	kprintf("    SUPER_BLOCK_FS_LENDIAN  = %#08x (%s)\n", 
		(int)SUPER_BLOCK_FS_LENDIAN, get_tupel(SUPER_BLOCK_FS_LENDIAN));
	kprintf("    SUPER_BLOCK_DISK_CLEAN  = %#08x (%s)\n", 
		(int)SUPER_BLOCK_DISK_CLEAN, get_tupel(SUPER_BLOCK_DISK_CLEAN));
	kprintf("    SUPER_BLOCK_DISK_DIRTY  = %#08x (%s)\n", 
		(int)SUPER_BLOCK_DISK_DIRTY, get_tupel(SUPER_BLOCK_DISK_DIRTY));
	kprintf("  Structures:\n");
	kprintf("    INODE_MAGIC1            = %#08x (%s)\n", 
		(int)INODE_MAGIC1, get_tupel(INODE_MAGIC1));
	kprintf("    BPLUSTREE_MAGIC         = %#08x (%s)\n", 
		(int)BPLUSTREE_MAGIC, get_tupel(BPLUSTREE_MAGIC));
	kprintf("  Control:\n");
	kprintf("    BFS_IOCTL_CHECK_MAGIC   = %#08x (%s)\n", 
		(int)BFS_IOCTL_CHECK_MAGIC, get_tupel(BFS_IOCTL_CHECK_MAGIC));
}

#endif // DEBUG || BFS_DEBUGGER_COMMANDS


//	#pragma mark - debugger commands


#ifdef BFS_DEBUGGER_COMMANDS


static int
dump_inode(int argc, char** argv)
{
	bool block = false;
	if (argc >= 3 && !strcmp(argv[1], "-b"))
		block = true;

	if (argc != 2 + (block ? 1 : 0) || !strcmp(argv[1], "--help")) {
		kprintf("usage: bfsinode [-b] <ptr-to-inode>\n"
			"  -b the address is regarded as pointer to a block instead of one "
			"to an inode.\n");
		return 0;
	}

	addr_t address = parse_expression(argv[argc - 1]);
	bfs_inode* node;
	if (block)
		node = (bfs_inode*)address;
	else {
		Inode* inode = (Inode*)address;

		kprintf("INODE %p\n", inode);
		kprintf("  rw lock:           %p\n", &inode->Lock());
		kprintf("  tree:              %p\n", inode->Tree());
		kprintf("  file cache:        %p\n", inode->FileCache());
		kprintf("  file map:          %p\n", inode->Map());
		kprintf("  old size:          %" B_PRIdOFF "\n", inode->OldSize());
		kprintf("  old last modified: %" B_PRIdOFF "\n",
			inode->OldLastModified());

		node = &inode->Node();
	}

	dump_inode(node);
	return 0;
}


static int
dump_volume(int argc, char** argv)
{
	if (argc < 2 || !strcmp(argv[1], "--help")) {
		kprintf("usage: bfs <ptr-to-volume> [<block-run>]\n"
			"Dumps a BFS volume - <block-run> is given, it is converted to a "
			"block offset instead (and vice versa).\n");
		return 0;
	}

	Volume* volume = (Volume*)parse_expression(argv[1]);

	if (argc > 2) {
		// convert block_runs/offsets
		for (int i = 2; i < argc; i++) {
			char* arg = argv[i];
			if (strchr(arg, '.') != NULL || strchr(arg, ',') != NULL) {
				// block_run to offset
				block_run run;
				run.allocation_group = HOST_ENDIAN_TO_BFS_INT32(
					strtoul(arg, &arg, 0));
				run.start = HOST_ENDIAN_TO_BFS_INT16(strtoul(arg + 1, NULL, 0));
				run.length = 0;

				kprintf("%" B_PRId32 ".%u -> block %" B_PRIdOFF ", bitmap block"
					" %" B_PRId32 "\n", run.AllocationGroup(), run.Start(),
					volume->ToBlock(run),
					volume->SuperBlock().BlocksPerAllocationGroup()
						* run.AllocationGroup() + 1);
			} else {
				// offset to block_run
				off_t offset = parse_expression(arg);
				block_run run = volume->ToBlockRun(offset);

				kprintf("block %" B_PRIdOFF " -> %" B_PRId32 ".%u, bitmap block"
					" %" B_PRId32 "\n", offset, run.AllocationGroup(),
					run.Start(), volume->SuperBlock().BlocksPerAllocationGroup()
						* run.AllocationGroup() + 1);
			}
		}
		return 0;
	}

	kprintf("id:           %" B_PRId32 "\n", volume->ID());
	kprintf("block cache:  %p\n", volume->BlockCache());
	kprintf("journal:      %p\n", volume->GetJournal(0));
	kprintf("allocator:    %p\n", &volume->Allocator());
	kprintf("root node:    %p\n", volume->RootNode());
	kprintf("indices node: %p\n\n", volume->IndicesNode());

	dump_super_block(&volume->SuperBlock());

	set_debug_variable("_cache", (addr_t)volume->BlockCache());
	set_debug_variable("_root", (addr_t)volume->RootNode());
	set_debug_variable("_indices", (addr_t)volume->IndicesNode());

	return 0;
}


static int
dump_block_run_array(int argc, char** argv)
{
	if (argc < 2 || !strcmp(argv[1], "--help")) {
		kprintf("usage: %s <ptr-to-array> [number-of-runs] [block-size] "
			"[start-offset] [search-offset]\n", argv[0]);
		return 0;
	}

	const block_run* runs = (const block_run*)parse_expression(argv[1]);
	uint32 count = 16;
	if (argc > 2)
		count = parse_expression(argv[2]);

	uint32 blockSize = 0;
	if (argc > 3)
		blockSize = parse_expression(argv[3]);

	off_t offset = 0;
	if (argc > 4)
		offset = parse_expression(argv[4]);

	off_t searchOffset = 0;
	if (argc > 5)
		searchOffset = parse_expression(argv[5]);

	for (uint32 i = 0; i < count; i++) {
		if (blockSize != 0)
			dprintf("[%3" B_PRIu32 "]  %10" B_PRIdOFF "  ", i, offset);
		else
			dprintf("[%3" B_PRIu32 "]  ", i);

		uint32 size = runs[i].Length() * blockSize;
		if (searchOffset != 0 && searchOffset >= offset
			&& searchOffset < offset + size)
			dprintf("*  ");

		dump_block_run("", runs[i]);

		offset += size;
	}

	return 0;
}


static int
dump_bplustree_node(int argc, char** argv)
{
	if (argc < 2 || argc > 4 || !strcmp(argv[1], "--help")) {
		kprintf("usage: %s <ptr-to-node> [ptr-to-header] [ptr-to-volume]\n",
			argv[0]);
		return 0;
	}

	bplustree_node* node = (bplustree_node*)parse_expression(argv[1]);
	bplustree_header* header = NULL;
	Volume* volume = NULL;

	if (argc > 2)
		header = (bplustree_header*)parse_expression(argv[2]);
	if (argc > 3)
		volume = (Volume*)parse_expression(argv[3]);

	dump_bplustree_node(node, header, volume);

	return 0;
}


static int
dump_bplustree_header(int argc, char** argv)
{
	if (argc != 2 || !strcmp(argv[1], "--help")) {
		kprintf("usage: %s <ptr-to-header>\n", argv[0]);
		return 0;
	}

	bplustree_header* header = (bplustree_header*)parse_expression(argv[1]);
	dump_bplustree_header(header);

	return 0;
}


static int
validate_bfs_inode(int argc, char** argv)
{
	if (argc < 2 || !strcmp(argv[1], "--help")) {
		kprintf("usage: %s <ptr-to-inode>\n"
			"Performs comprehensive validation of a BFS inode structure.\n", argv[0]);
		return 0;
	}
	
	addr_t address = parse_expression(argv[1]);
	bfs_inode* inode = (bfs_inode*)address;
	
	bool valid = validate_inode_structure(inode);
	kprintf("Inode validation: %s\n", valid ? "PASSED" : "FAILED");
	
	if (valid) {
		kprintf("Additional inode analysis:\n");
		kprintf("  Type: %s\n", 
			S_ISDIR(inode->Mode()) ? "directory" :
			S_ISREG(inode->Mode()) ? "file" :
			S_ISLNK(inode->Mode()) ? "symlink" : "other");
		kprintf("  Size: %" B_PRIdOFF " bytes\n", inode->data.size);
		kprintf("  Blocks used: estimated %" B_PRIdOFF "\n", 
			(inode->data.size + 4095) / 4096);
		
		// Check for common issues
		if (inode->data.size == 0 && S_ISREG(inode->Mode())) {
			kprintf("  WARNING: Regular file with zero size\n");
		}
		if (inode->CreateTime() > inode->LastModifiedTime()) {
			kprintf("  WARNING: Create time is after modification time\n");
		}
	}
	
	return 0;
}


static int
validate_bfs_btree_node(int argc, char** argv)
{
	if (argc < 3 || !strcmp(argv[1], "--help")) {
		kprintf("usage: %s <ptr-to-node> <ptr-to-header>\n"
			"Performs comprehensive validation of a BFS B+tree node.\n", argv[0]);
		return 0;
	}
	
	addr_t nodeAddr = parse_expression(argv[1]);
	addr_t headerAddr = parse_expression(argv[2]);
	
	bplustree_node* node = (bplustree_node*)nodeAddr;
	bplustree_header* header = (bplustree_header*)headerAddr;
	
	bool valid = validate_btree_node_structure(node, header);
	kprintf("B+tree node validation: %s\n", valid ? "PASSED" : "FAILED");
	
	if (valid) {
		kprintf("Node analysis:\n");
		kprintf("  Key count: %u\n", node->all_key_count);
		kprintf("  Key length: %u bytes\n", node->all_key_length);
		kprintf("  Space efficiency: %.1f%%\n", 
			(float)(node->all_key_length + node->all_key_count * sizeof(off_t)) * 100.0f
			/ (header->node_size - sizeof(bplustree_node)));
		
		if (node->left_link != BPLUSTREE_NULL)
			kprintf("  Has left sibling: %" B_PRId64 "\n", node->left_link);
		if (node->right_link != BPLUSTREE_NULL)
			kprintf("  Has right sibling: %" B_PRId64 "\n", node->right_link);
		if (node->overflow_link != BPLUSTREE_NULL)
			kprintf("  Has overflow: %" B_PRId64 "\n", node->overflow_link);
	}
	
	return 0;
}


static int
analyze_bfs_data(int argc, char** argv)
{
	if (argc < 3 || !strcmp(argv[1], "--help")) {
		kprintf("usage: %s <ptr> <size> [description]\n"
			"Analyzes data integrity and prints hex dump.\n", argv[0]);
		return 0;
	}
	
	addr_t address = parse_expression(argv[1]);
	size_t size = (size_t)parse_expression(argv[2]);
	const char* description = argc > 3 ? argv[3] : "data";
	
	void* data = (void*)address;
	
	// Analyze integrity
	analyze_data_integrity(data, size, description);
	
	// Print hex dump if size is reasonable
	if (size <= 256) {
		print_hex_dump(data, size, description);
	} else {
		kprintf("Size too large for hex dump, showing first 256 bytes:\n");
		print_hex_dump(data, 256, description);
	}
	
	return 0;
}


static int
dump_bfs_magic_numbers(int argc, char** argv)
{
	if (argc > 1 && !strcmp(argv[1], "--help")) {
		kprintf("usage: %s\n"
			"Displays all BFS magic numbers and their meanings.\n", argv[0]);
		return 0;
	}
	
	dump_all_magic_numbers();
	return 0;
}


static int
validate_magic_number(int argc, char** argv)
{
	if (argc < 2 || !strcmp(argv[1], "--help")) {
		kprintf("usage: %s <magic-number>\n"
			"Validates and identifies a magic number.\n", argv[0]);
		return 0;
	}
	
	addr_t magic = parse_expression(argv[1]);
	const char* description = get_magic_string((uint32)magic);
	
	kprintf("Magic Number Analysis:\n");
	kprintf("  Value: %#08x (%s)\n", (int)magic, get_tupel((uint32)magic));
	kprintf("  Identification: %s\n", description);
	
	// Additional validation if it's a known BFS magic
	if (magic == SUPER_BLOCK_MAGIC1 || magic == SUPER_BLOCK_MAGIC2 || 
		magic == SUPER_BLOCK_MAGIC3) {
		kprintf("  Type: Superblock magic number\n");
	} else if (magic == INODE_MAGIC1) {
		kprintf("  Type: Inode magic number\n");
	} else if (magic == BPLUSTREE_MAGIC) {
		kprintf("  Type: B+tree magic number\n");
	} else if (magic == BFS_IOCTL_CHECK_MAGIC) {
		kprintf("  Type: IOCTL control magic\n");
	} else if (magic == SUPER_BLOCK_FS_LENDIAN || magic == SUPER_BLOCK_DISK_CLEAN || 
			   magic == SUPER_BLOCK_DISK_DIRTY) {
		kprintf("  Type: Filesystem state magic\n");
	} else {
		kprintf("  Type: Unknown or non-BFS magic number\n");
	}
	
	return 0;
}


void
remove_debugger_commands()
{
	// Remove enhanced debugging commands
	remove_debugger_command("bfs_validate_inode", validate_bfs_inode);
	remove_debugger_command("bfs_validate_btree", validate_bfs_btree_node);
	remove_debugger_command("bfs_analyze_data", analyze_bfs_data);
	remove_debugger_command("bfs_magic_numbers", dump_bfs_magic_numbers);
	remove_debugger_command("bfs_validate_magic", validate_magic_number);
	
	// Remove original BFS debugger commands
	remove_debugger_command("bfs_inode", dump_inode);
	remove_debugger_command("bfs_allocator", dump_block_allocator);
#if BFS_TRACING
	remove_debugger_command("bfs_allocator_blocks",
		dump_block_allocator_blocks);
#endif
	remove_debugger_command("bfs_journal", dump_journal);
	remove_debugger_command("bfs_btree_header", dump_bplustree_header);
	remove_debugger_command("bfs_btree_node", dump_bplustree_node);
	remove_debugger_command("bfs", dump_volume);
	remove_debugger_command("bfs_block_runs", dump_block_run_array);
}


void
add_debugger_commands()
{
	// Original BFS debugger commands
	add_debugger_command("bfs_inode", dump_inode, "dump an Inode object");
	add_debugger_command("bfs_allocator", dump_block_allocator,
		"dump a BFS block allocator");
#if BFS_TRACING
	add_debugger_command("bfs_allocator_blocks", dump_block_allocator_blocks,
		"dump a BFS block allocator actions that affected a certain block");
#endif
	add_debugger_command("bfs_journal", dump_journal,
		"dump the journal log entries");
	add_debugger_command("bfs_btree_header", dump_bplustree_header,
		"dump a BFS B+tree header");
	add_debugger_command("bfs_btree_node", dump_bplustree_node,
		"dump a BFS B+tree node");
	add_debugger_command("bfs", dump_volume, "dump a BFS volume");
	add_debugger_command("bfs_block_runs", dump_block_run_array,
		"dump a block run array");
	
	// Enhanced debugging commands
	add_debugger_command("bfs_validate_inode", validate_bfs_inode,
		"validate BFS inode structure");
	add_debugger_command("bfs_validate_btree", validate_bfs_btree_node,
		"validate BFS B+tree node structure");
	add_debugger_command("bfs_analyze_data", analyze_bfs_data,
		"analyze data integrity and print hex dump");
	add_debugger_command("bfs_magic_numbers", dump_bfs_magic_numbers,
		"display all BFS magic numbers and their meanings");
	add_debugger_command("bfs_validate_magic", validate_magic_number,
		"validate and identify a magic number");
}


#endif	// BFS_DEBUGGER_COMMANDS
