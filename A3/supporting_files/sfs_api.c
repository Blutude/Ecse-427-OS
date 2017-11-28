
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
#define NUM_BLOCKS_ROOTDIR 5
#define DATA_BLOCKS_START INODE_LEN+1
#define NUM_FILES INODE_LEN-1  // INODE_LEN - 1 because first INODE belongs to root directory

/* macros */
#define FREE_BIT(_data, _which_bit) \
    _data = _data | (1 << _which_bit)

#define USE_BIT(_data, _which_bit) \
    _data = _data & ~(1 << _which_bit)

struct superblock_t superblock;
struct file_descriptor fd[INODE_LEN]; // is that the inode table?
struct directory_entry files[INODE_LEN];
struct inode_t iNodeTable[INODE_LEN];
//int directoryEntryIndex = 0;
//int lastDirectoryEntryIndex = 0;
void* buffer;

// ********************************** HELPER FUNCTIONS ******************************************


// **** WRITING TO DISK
int writeINode(int i) { // write to disk
	buffer = (void*) malloc(BLOCK_SIZE);
	memset(buffer, 0, BLOCK_SIZE);
	memcpy(buffer, &iNodeTable[i], sizeof(inode_t));
	int r = write_blocks(1+i, 1, buffer); // 1+i because index 0 is for superblock. 1 inode = 1 block???
	free(buffer);
	return r;
}

int writeRootDirectory() { // write to disk. 5 blocks good enough??
	buffer = (void*) malloc(NUM_BLOCKS_ROOTDIR*BLOCK_SIZE);
	memset(buffer, 0, NUM_BLOCKS_ROOTDIR*BLOCK_SIZE);
	memcpy(buffer, files, NUM_FILES*sizeof(directory_entry));
	int r = write_blocks(DATA_BLOCKS_START, 5, buffer);
	free(buffer);
	return r;
}

int writeFreeBitMap() { // write to disk 
	buffer = (void*) malloc(BLOCK_SIZE);
	memset(buffer, 0, BLOCK_SIZE);
	int r = write_blocks(NUM_BLOCKS-1, 1, buffer);
	free(buffer);
	return r;
}


// *********************************************************************************
// *********************************************************************************


//initialize all bits to high
uint8_t free_bit_map[BITMAP_ROW_SIZE] = { [0 ... BITMAP_ROW_SIZE - 1] = UINT8_MAX };

void mksfs(int fresh) {
	// clear inode table ???? why?
	int r;
	if (fresh) {
		r = init_fresh_disk(AZRAK_RONY_DISK, BLOCK_SIZE, NUM_BLOCKS);


		// init superblock
		superblock = (superblock_t) {0xACBD0005, BLOCK_SIZE, NUM_BLOCKS, INODE_LEN, 0};
		buffer = (void*) malloc(BLOCK_SIZE);
		memset(buffer, 0, BLOCK_SIZE);
		memcpy(buffer, &superblock, sizeof(superblock_t));
		if (write_blocks(0, 1, buffer) < 0)
			printf("Failure(s) writing superblock\n");
		free(buffer);
		// updating freeBitMap
		force_set_index(0);



		int i;
		// init directory entries
		for (i=0; i<NUM_FILES; i++) {
			files[i].num = -1;
			files[i].name[0] = 0; // empty string
		}
		if (writeRootDirectory() < 0)
			printf("Failure(s) writing root directory\n")
		// updating freeBitMap
		force_set_index(DATA_BLOCKS_START);
		force_set_index(DATA_BLOCKS_START+1);
		force_set_index(DATA_BLOCKS_START+2);
		force_set_index(DATA_BLOCKS_START+3);
		force_set_index(DATA_BLOCKS_START+4);



		// init file descriptors. Does not write to disk, is in-memory
		for (i=0; i<NUM_FILES; i++) {
			fd[i] = (file_descriptor) {-1, NULL, -1};
		}



		// init root inode and inode table
		iNodeTable[0] = (inode_t) {777, 0, 0, 0, 0, {DATA_BLOCKS_START, DATA_BLOCKS_START+1, DATA_BLOCKS_START+2, DATA_BLOCKS_START+3, DATA_BLOCKS_START+4, -1, -1, -1, -1, -1, -1, -1}, -1}; // ?? root INode points to 5 blocks (size of root directory)
		if (writeINode(0) < 0)
			printf("Failure(s) writing inode nb%d\n", 0);
		force_set_index(1); // updating freeBitMap
		for (i=1; i<INODE_LEN; i++) {
			iNodeTable[i] = (inode_t) {777, 0, 0, 0, 0, {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}, -1}; // ??
			if (writeINode(i) < 0)
				printf("Failure(s) writing inode nb%d\n", i);
			force_set_index(i+1); // updating freeBitMap
		}



		// writing freeBitMap to disk
		if (writeFreeBitMap() < 0)
			printf("Failure(s) writing freeBitMap to disk\n");
	
	} else {
		r = init_disk(AZRAK_RONY_DISK, BLOCK_SIZE, NUM_BLOCKS);
		

		//readB


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

