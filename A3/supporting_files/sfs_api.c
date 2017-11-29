
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
struct file_descriptor fd[NUM_FILES]; 
struct directory_entry files[NUM_FILES];
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


// **** Other helpers
int nameValid(char* name) {
	int length = strlen(name);
	if (length > 20)
		return -1;
	char copy[length];
	memset(copy, '\0', sizeof(copy));
	strcpy(copy, name); // because strtok modifies the string

	char* token;
	const char s[2] = ".";
	token = strtok(copy, s); // token = "([]).[]"
	token = strtok(NULL, s); // token = "[].([])"
	if (strlen(token) > 3)
		return -1;
	return 0;
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
			printf("Failure(s) writing root directory\n");
		// updating freeBitMap
		force_set_index(DATA_BLOCKS_START); // !!!! make change fixed root directory location
		force_set_index(DATA_BLOCKS_START+1);
		force_set_index(DATA_BLOCKS_START+2);
		force_set_index(DATA_BLOCKS_START+3);
		force_set_index(DATA_BLOCKS_START+4);



		// init file descriptors. Does not write to disk, is in-memory
		for (i=0; i<NUM_FILES; i++) {
			fd[i] = (file_descriptor) {-1, NULL, 0};
		}



		// init root inode and inode table. !!!! CHANGE this to get index to we find where to write root directory? (location doesn't need to be fixed)
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
		

		//read superblock
		read_blocks(0, 1, buffer);
		memcpy(&superblock, buffer, sizeof(superblock_t));

		

		// read directory entries
		// !!!! might make root directory no fix. Need to look for location through first inode


	}

	if (r != 0)
		return;
}

int sfs_getnextfilename(char *fname) {

}

int sfs_getfilesize(const char* path) {

}
int sfs_fopen(char *name) { // make sure can't open same file twice
	
	if (nameValid(name) < 0)
		return -1;



	// find file by name - iterate root directory
	int i;
	int iNodeIndex = -1;
	int exists = 0;
	for (i=0; i<NUM_FILES; i++) {
		if (!strcmp(files[i].name, name)) {
			exists = 1;
			iNodeIndex = files[i].num;
			break;
		}
	}

	if (exists) {
		// check if file already open - iterate fd table
		for (i=0; i<NUM_FILES; i++) {
			if (fd[i].inodeIndex == iNodeIndex) {
				return i; // file already open
			}
		}
	} else {
		// find slot in root direcory
		int fileIndex = -1;
		for (i=0; i<NUM_FILES; i++) {
			if (files[i].num == -1) {
				fileIndex = i;
				break;
			}
		}
		if (fileIndex == -1) {
			printf("Root directory full - has 99 files\n");
			return -1;
		}

		// find unused inode
		for (i=1; i<INODE_LEN; i++) {
			if (iNodeTable[i].data_ptrs[0] == -1) {
				iNodeIndex = i;
				break;
			}
		}
		if (iNodeIndex == -1) {
			printf("Investigate!!!!\n");
			return -1;
		}

		// got fileIndex and iNodeIndex
		strcpy(files[fileIndex].name, name);
		files[fileIndex].num = iNodeIndex;
		// write to disk
		if (writeINode(iNodeIndex) < 0)
			printf("Failure(s) writing iNode to disk");
		if (writeRootDirectory() < 0)
			printf("Failure(s) writing root directory to disk");
	}

	// file is not open yet - find fd slot - iterate fd table
	int fdIndex = -1;
	for (i=0; i<NUM_FILES; i++) {
		if (fd[i].inodeIndex == -1) { // found free file descriptor slot
			fdIndex = i;
			break;
		}
	}
	if (i == NUM_FILES) { // found no available slot
		printf("Open file descriptor table full\n"); // should not happen because no more than 99 files anyway
		return -1;
	}

	int rwptr = iNodeTable[iNodeIndex].size;
	fd[fdIndex] = (file_descriptor) {iNodeIndex, &iNodeTable[iNodeIndex], rwptr}; // ?

	return fdIndex;
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

