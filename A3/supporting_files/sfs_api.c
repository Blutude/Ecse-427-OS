
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
void* buffer;
int i; // iterate forloops
int nbBlocksRoot;
int nbBlocksINodeTable;
int dataBlocksStartIndex;


// ********************************** HELPER FUNCTIONS ******************************************


// **** WRITING TO DISK
int writeINodeTable() { // write to disk
	buffer = (void*) malloc(nbBlocksINodeTable*BLOCK_SIZE);
	memset(buffer, 0, nbBlocksINodeTable*BLOCK_SIZE);
	memcpy(buffer, iNodeTable, (INODE_LEN)*(sizeof(inode_t)));
	int r = write_blocks(1, nbBlocksINodeTable, buffer); // 1 because index 0 is for superblock. 1 inode = 1 block???
	free(buffer);
	return r;
}

int writeRootDirectory() { // write to disk.
	buffer = (void*) malloc(nbBlocksRoot*BLOCK_SIZE);
	memset(buffer, 0, nbBlocksRoot*BLOCK_SIZE);
	memcpy(buffer, files, (NUM_FILES)*(sizeof(directory_entry)));
	int r = write_blocks(dataBlocksStartIndex, nbBlocksRoot, buffer);
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
//uint8_t free_bit_map[BITMAP_ROW_SIZE] = { [0 ... BITMAP_ROW_SIZE - 1] = UINT8_MAX };

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



		// init root inode and inode table.
		// calculate nbBlocksINodeTable i.e nb of blocks needed for inode table
		nbBlocksINodeTable = (INODE_LEN)*(sizeof(inode_t))/BLOCK_SIZE + (((INODE_LEN)*(sizeof(inode_t)))%BLOCK_SIZE > 0); // getting ceiling value = 8
		dataBlocksStartIndex = 1+nbBlocksINodeTable; // = 9
		iNodeTable[0] = (inode_t) {777, 0, 0, 0, 0, {dataBlocksStartIndex, dataBlocksStartIndex+1, dataBlocksStartIndex+2, -1, -1, -1, -1, -1, -1, -1, -1, -1}, -1}; // root INode points to first 3 blocks of data blocks
		for (i=1; i<INODE_LEN; i++)
			iNodeTable[i] = (inode_t) {777, 0, 0, 0, 0, {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}, -1};
		if (writeINodeTable() < 0)
				printf("Failure(s) writing inode table\n");
		for (i=0; i<nbBlocksINodeTable; i++)
			force_set_index(i+1); // 0 is for superblock
		if (nbBlocksINodeTable != 8)
			printf("nbBlocksINodeTable is %d and not 8. Need to change stuff\n", nbBlocksINodeTable);



		// init directory entries
		// calculate nbBlocksRoot i.e nb of blocks needed for root directory
		nbBlocksRoot = (NUM_FILES)*(sizeof(directory_entry))/BLOCK_SIZE + (((NUM_FILES)*(sizeof(directory_entry)))%BLOCK_SIZE > 0); // getting ceiling value = 3
		for (i=0; i<NUM_FILES; i++) {
			files[i].num = -1;
			files[i].name[0] = 0; // empty string
		}
		if (writeRootDirectory() < 0)
			printf("Failure(s) writing root directory\n");
		// updating freeBitMap
		for (i=0; i<nbBlocksRoot; i++) {
			force_set_index(dataBlocksStartIndex+i);
		}
		if (nbBlocksRoot != 3)
			printf("nbBlocksRoot is %d and not 3. Need to change iNodeTable[0] and maybe other stuff\n", nbBlocksRoot);



		// init file descriptors. Does not write to disk, is in-memory
		fd[0] = (file_descriptor) {0, &iNodeTable[0], 0};
		for (i=1; i<NUM_FILES; i++) {
			fd[i] = (file_descriptor) {-1, NULL, 0};
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
		for (i=1; i<NUM_FILES; i++) { // 0 is for root directory
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
		if (writeINodeTable() < 0)
			printf("Failure(s) writing iNode to disk");
		if (writeRootDirectory() < 0)
			printf("Failure(s) writing root directory to disk");
	}

	// file is not open yet - find fd slot - iterate fd table
	int fdIndex = -1;
	for (i=1; i<NUM_FILES; i++) { // 0 is for root directory
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
	if (length < 0) {
		printf("Length cannot be negative");
		return -1;
	}

	file_descriptor myFd = fd[fileID];

	// check that file is open
	if (myFd.inodeIndex == -1) {
		printf("File is not open");
		return -1;
	}

	// calculate nb of blocks needed and index inside last block
	int rwptr = myFd.rwptr;
	int startBlockIndex = rwptr / BLOCK_SIZE;
	int startIndexInBlock = rwptr % BLOCK_SIZE; // start writing here
	int endRwptr = rwptr + length;
	int endBlockIndex = endRwptr / BLOCK_SIZE;
	int endIndexInBlock = endRwptr % BLOCK_SIZE; // exclusive

	int bytesWritten = 0;

	if (startBlockIndex > 11) {
		// TODO !!!!
	} else {
		// write to first block
		buffer = (void*) malloc(BLOCK_SIZE);

		// ** replace end of first block
		// read entire startBlock
		inode_t myINode = iNodeTable[fileID];
		memset(buffer, 0, BLOCK_SIZE);
		if (myINode.data_ptrs[startBlockIndex] == -1) { // startIndexInBlock should be 0
			myINode.data_ptrs[startBlockIndex] = get_index();
			if (startIndexInBlock != 0)
				printf("startIndexInBlock should be 0. Investigate.\n");
		}
		read_blocks(myINode.data_ptrs[startBlockIndex], 1, buffer);
		memcpy(buffer+startIndexInBlock, buf, BLOCK_SIZE-startIndexInBlock); // !!!!! double check this
		write_blocks(myINode.data_ptrs[startBlockIndex], 1, buffer);
		bytesWritten += BLOCK_SIZE-startIndexInBlock;
		force_set_index(myINode.data_ptrs[startBlockIndex]); // in case block was originally empty


		if (endBlockIndex > 11) {
			for(i=startBlockIndex+1; i<=11; i++) { // ** replace the whole block
				memset(buffer, 0, BLOCK_SIZE);
				memcpy(buffer, &buf[bytesWritten], BLOCK_SIZE); // !!!!! cast buf to void*?
				if (myINode.data_ptrs[i] != -1)
					printf("data_ptr should be -1. Investigate.\n");
				myINode.data_ptrs[i] = get_index();
				write_blocks(myINode.data_ptrs[i], 1, buffer);
				bytesWritten += BLOCK_SIZE;
				force_set_index(myINode.data_ptrs[i]);
			}

			// TODO !!!!
			// deal with indrect pointers

		} else {
			for(i=startBlockIndex+1; i<=endBlockIndex; i++) { // ** replace beginning of last block - partly read buf
				if (i == endBlockIndex) {
					memset(buffer, 0, BLOCK_SIZE);
					memcpy(buffer, &buf[bytesWritten], length - bytesWritten); // !!!!! cast buf to void*? // 
					if (myINode.data_ptrs[i] != -1)
						printf("data_ptr should be -1. Investigate.\n");
					myINode.data_ptrs[i] = get_index();
					write_blocks(myINode.data_ptrs[i], 1, buffer);
					bytesWritten += BLOCK_SIZE;
					force_set_index(myINode.data_ptrs[i]);
				} else {
					memset(buffer, 0, BLOCK_SIZE);
					memcpy(buffer, &buf[bytesWritten], BLOCK_SIZE); // !!!!! cast buf to void*?
					if (myINode.data_ptrs[i] != -1)
						printf("data_ptr should be -1. Investigate.\n");
					myINode.data_ptrs[i] = get_index();
					write_blocks(myINode.data_ptrs[i], 1, buffer);
					bytesWritten += BLOCK_SIZE;
					force_set_index(myINode.data_ptrs[i]);
				}
			}
		}

		
	}

	myFd.rwptr = rwptr + bytesWritten;
	free(buffer);
	writeINodeTable();
	writeFreeBitMap();

	return bytesWritten;
}
int sfs_fseek(int fileID, int loc) {

}
int sfs_remove(char *file) {


}

