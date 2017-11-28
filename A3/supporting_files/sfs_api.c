
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
#define INODE_LEN 100

/* macros */
#define FREE_BIT(_data, _which_bit) \
    _data = _data | (1 << _which_bit)

#define USE_BIT(_data, _which_bit) \
    _data = _data & ~(1 << _which_bit)

struct superblock_t superblock;
struct file_descriptor fd[100]; // is that the inode table?
struct directory_entry files[100];
int directoryEntryIndex = 0;
int lastDirectoryEntryIndex = 0;
void* buffer;


//initialize all bits to high
uint8_t free_bit_map[BITMAP_ROW_SIZE] = { [0 ... BITMAP_ROW_SIZE - 1] = UINT8_MAX };

void mksfs(int fresh) {
	// clear inode table ???? why?
	int r;
	if (fresh) {
		r = init_fresh_disk(AZRAK_RONY_DISK, BLOCK_SIZE, NUM_BLOCKS);

		// init superblock, forceset superblock
		superblock = (superblock_t) {0xACBD0005, BLOCK_SIZE, NUM_BLOCKS, INODE_LEN, 0};
		force_set_index(0);
		buffer = (void*) malloc(BLOCK_SIZE);
		memcpy(buffer, &superblock, sizeof(superblock_t));
		if (write_blocks(0, 1, buffer) < 0)
			printf("Failure(s) writing superblock\n");

		// init file descriptors
		for (int i=0; i<INODE_LEN - 1; i++) { // INODE_LEN - 1 because first INODE belongs to root directory
			files[i].num = -1;
			files[i].name[0] = 0; // empty string
		}

		// init directory entries

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

