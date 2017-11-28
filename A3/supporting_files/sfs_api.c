
#include "sfs_api.h"
#include "bitmap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fuse.h>
#include <strings.h>
#include "disk_emu.h"
#define AZRAK_RONY_DISK "sfs_disk.disk"
#define NUM_BLOCKS 1024  //maximum number of data blocks on the disk.
#define BITMAP_ROW_SIZE (NUM_BLOCKS/8) // this essentially mimcs the number of rows we have in the bitmap. we will have 128 rows. 
#define BLOCK_SIZE 1024

/* macros */
#define FREE_BIT(_data, _which_bit) \
    _data = _data | (1 << _which_bit)

#define USE_BIT(_data, _which_bit) \
    _data = _data & ~(1 << _which_bit)

struct superblock_t superblock;
struct file_descriptor fd[100]; // is that the inode table?
struct directory_entry de[100];
int directoryEntryIndex = 0;
int lastDirectoryEntryIndex = 0;


//initialize all bits to high
uint8_t free_bit_map[BITMAP_ROW_SIZE] = { [0 ... BITMAP_ROW_SIZE - 1] = UINT8_MAX };

void mksfs(int fresh) {
	// clear inode table ???? why?
	int r;
	if (fresh) {
		r = init_fresh_disk(AZRAK_RONY_DISK, BLOCK_SIZE, NUM_BLOCKS);

		// init superblock, forceset superblock
		

		// fill members of superblock
		superblock.magic = 0xACBD0005; // ?
		superblock.block_size = 1024;
		superblock.fs_size = 1024;
		superblock.inode_table_len = 100; // len or last index? what would be used for?
		superblock.root_dir_inode = 0;
		force_set_index(0);
		write_blocks(0, 1, superblock); // this good????

		// init filedescriptor and directory entries

		// writing bitmap
		write_blocks(1023, 1, free_bit_map);
	
	} else {
		r = init_disk(AZRAK_RONY_DISK, BLOCK_SIZE, NUM_BLOCKS);
		
		//readBlocks


	}

	if (r != 0)
		return;
}

void sfs_getnextfilename(char *fname) {

}

int sfs_getfilesize(const char* path){

}
int sfs_fopen(char *name){
	// return -1 if error.
	// make sure can't open same file twice
}
int sfs_fclose(int fileID) {

}
int sfs_fread(int fileID, char *buf, int length) {
	
}
int sfs_fwrite(int fileID, const char *buf, int length) {

}
int sfs_fseek(int fileID, int loc) {

}
int sfs_remove(char *file) {


}

