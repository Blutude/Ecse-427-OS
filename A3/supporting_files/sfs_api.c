
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
#define NUM_FILES (INODE_LEN-1)  // INODE_LEN - 1 because first INODE belongs to root directory
#define NUM_BLOCKS_INODETABLE ((INODE_LEN)*(sizeof(inode_t))/BLOCK_SIZE + (((INODE_LEN)*(sizeof(inode_t)))%BLOCK_SIZE > 0)) // getting ceiling value = 8
#define NUM_BLOCKS_ROOTDIR ((NUM_FILES)*(sizeof(directory_entry))/BLOCK_SIZE + (((NUM_FILES)*(sizeof(directory_entry)))%BLOCK_SIZE > 0)) // getting ceiling value = 3
#define DATABLOCKS_START_ADDRESS (1+NUM_BLOCKS_INODETABLE) // = 9
#define NUM_ADDRESSES_INDIRECT (BLOCK_SIZE/sizeof(int))

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


// ********************************** HELPER FUNCTIONS ******************************************


// **** WRITING TO DISK
int writeINodeTable() { // write to disk
	buffer = (void*) malloc(NUM_BLOCKS_INODETABLE*BLOCK_SIZE);
	memset(buffer, 0, NUM_BLOCKS_INODETABLE*BLOCK_SIZE);
	memcpy(buffer, iNodeTable, (INODE_LEN)*(sizeof(inode_t)));
	int r = write_blocks(1, NUM_BLOCKS_INODETABLE, buffer); // 1 because index 0 is for superblock. 1 inode = 1 block???
	free(buffer);
	return r;
}

int writeRootDirectory() { // write to disk.
	buffer = (void*) malloc(NUM_BLOCKS_ROOTDIR*BLOCK_SIZE);
	memset(buffer, 0, NUM_BLOCKS_ROOTDIR*BLOCK_SIZE);
	memcpy(buffer, files, (NUM_FILES)*(sizeof(directory_entry)));
	int r = write_blocks(DATABLOCKS_START_ADDRESS, NUM_BLOCKS_ROOTDIR, buffer);
	free(buffer);
	return r;
}

int writeFreeBitMap() { // write to disk 
	buffer = (void*) malloc(BLOCK_SIZE);
	memset(buffer, 1, BLOCK_SIZE);
	memcpy(buffer, free_bit_map, (BITMAP_ROW_SIZE)*(sizeof(uint8_t)));
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

int assertDefinedValues() {
	if (NUM_BLOCKS_ROOTDIR != 3 || NUM_BLOCKS_INODETABLE != 8 || DATABLOCKS_START_ADDRESS != 9)
		return -1;
	else
		return 0;
}


// *********************************************************************************
// *********************************************************************************


//initialize all bits to high
//uint8_t free_bit_map[BITMAP_ROW_SIZE] = { [0 ... BITMAP_ROW_SIZE - 1] = UINT8_MAX };

void mksfs(int fresh) {
	if (assertDefinedValues() == -1) {
		printf("Issue with defined values. Investigate.\n");
		return;
	}
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
		iNodeTable[0] = (inode_t) {777, 0, 0, 0, 0, {DATABLOCKS_START_ADDRESS, DATABLOCKS_START_ADDRESS+1, DATABLOCKS_START_ADDRESS+2, -1, -1, -1, -1, -1, -1, -1, -1, -1}, -1}; // root INode points to first 3 blocks of data blocks
		for (i=1; i<INODE_LEN; i++)
			iNodeTable[i] = (inode_t) {777, 0, 0, 0, 0, {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}, -1};
		if (writeINodeTable() < 0)
				printf("Failure(s) writing inode table\n");
		for (i=0; i<NUM_BLOCKS_INODETABLE; i++)
			force_set_index(i+1); // 0 is for superblock



		// init directory entries
		for (i=0; i<NUM_FILES; i++) {
			files[i].num = -1;
			files[i].name[0] = 0; // empty string
		}
		if (writeRootDirectory() < 0)
			printf("Failure(s) writing root directory\n");
		// updating freeBitMap
		for (i=0; i<NUM_BLOCKS_ROOTDIR; i++) {
			force_set_index(DATABLOCKS_START_ADDRESS+i);
		}



		// init file descriptors. Does not write to disk, is in-memory
		fd[0] = (file_descriptor) {0, &iNodeTable[0], 0};
		for (i=1; i<NUM_FILES; i++) {
			fd[i] = (file_descriptor) {-1, NULL, 0};
		}



		// writing freeBitMap to disk
		if (writeFreeBitMap() < 0)
			printf("Failure(s) writing freeBitMap to disk\n");
		force_set_index(NUM_BLOCKS-1);
	
	} else {
		r = init_disk(AZRAK_RONY_DISK, BLOCK_SIZE, NUM_BLOCKS);


		// read freebitmap
		buffer = (void*) malloc(BLOCK_SIZE);
		memset(buffer, 1, BLOCK_SIZE);
		read_blocks(NUM_BLOCKS-1, 1, buffer);
		memcpy(free_bit_map, buffer, (BITMAP_ROW_SIZE)*(sizeof(uint8_t)));
		free(buffer);
		force_set_index(NUM_BLOCKS-1);
		

		// read superblock
		buffer = (void*) malloc(BLOCK_SIZE);
		memset(buffer, 0, BLOCK_SIZE);
		read_blocks(0, 1, buffer);
		memcpy(&superblock, buffer, sizeof(superblock_t));
		free(buffer);
		force_set_index(0);


		// read inode table
		buffer = (void*) malloc(NUM_BLOCKS_INODETABLE*BLOCK_SIZE);
		memset(buffer, 0, NUM_BLOCKS_INODETABLE*BLOCK_SIZE);
		read_blocks(1, NUM_BLOCKS_INODETABLE, buffer);
		memcpy(iNodeTable, buffer, INODE_LEN*sizeof(inode_t));
		free(buffer);
		for (i=0; i<NUM_BLOCKS_INODETABLE; i++)
			force_set_index(i+1);


		// read root directory
		buffer = (void*) malloc(NUM_BLOCKS_ROOTDIR);
		memset(buffer, 0, NUM_BLOCKS_ROOTDIR*BLOCK_SIZE);
		read_blocks(DATABLOCKS_START_ADDRESS, NUM_BLOCKS_ROOTDIR, buffer);
		memcpy(files, buffer, NUM_FILES*sizeof(directory_entry));
		free(buffer);
		for (i=0; i<NUM_BLOCKS_ROOTDIR; i++)
			force_set_index(i+DATABLOCKS_START_ADDRESS);


		// init file descriptors. Is not written in disk, is in-memory
		fd[0] = (file_descriptor) {0, &iNodeTable[0], 0};
		for (i=1; i<NUM_FILES; i++) {
			fd[i] = (file_descriptor) {-1, NULL, 0};
		}
	}

	if (r != 0) {
		printf("Failure initializing disk\n");
		return;
	}
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

	inode_t myINode = iNodeTable[fileID];

	// calculate nb of blocks needed and index inside last block
	int rwptr = myFd.rwptr;
	int startBlockIndex = rwptr / BLOCK_SIZE;
	int startIndexInBlock = rwptr % BLOCK_SIZE; // start writing here
	int endRwptr = rwptr + length;
	int endBlockIndex = endRwptr / BLOCK_SIZE;
	int endIndexInBlock = endRwptr % BLOCK_SIZE; // exclusive

	int bytesWritten = 0;

	buffer = (void*) malloc(BLOCK_SIZE);
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

	inode_t myINode = iNodeTable[fileID];

	// calculate nb of blocks needed and index inside last block
	int rwptrOriginal = myFd.rwptr;
	int startBlockIndex = rwptrOriginal / BLOCK_SIZE;
	int startIndexInBlock = rwptrOriginal % BLOCK_SIZE; // start writing here
	int endRwptr = rwptrOriginal + length;
	int endBlockIndex = endRwptr / BLOCK_SIZE;
	int endIndexInBlock = endRwptr % BLOCK_SIZE; // exclusive

	int bytesWritten = 0;

	buffer = (void*) malloc(BLOCK_SIZE);

	if (startBlockIndex > 11) {
		// indirect pointers

		memset(buffer, 0, BLOCK_SIZE);
		// read indirect block
		read_blocks(myINode.indirectPointer, 1, buffer);
		int addresses[NUM_ADDRESSES_INDIRECT];
		for (i=0; i<NUM_ADDRESSES_INDIRECT; i++)  {
			memcpy(addresses[i], (int*)buffer+i, sizeof(int));
		}

		// write data
		for (i=startBlockIndex; i<=endBlockIndex; i++) {
			memset(buffer, 0, BLOCK_SIZE);
			int indirectBlockIndex = i-11-1;
			if (i == startBlockIndex) {
				// read first block, fill up block and write back to disk
				if (addresses[indirectBlockIndex] == -1) {
					addresses[indirectBlockIndex] = get_index();
					if (startIndexInBlock != 0)
						printf("(indirect pointer) startIndexInBlock should be 0. Investigate.\n");
				}
				read_blocks(addresses[indirectBlockIndex], 1, buffer);
				memcpy((char*)buffer+startIndexInBlock, buf, BLOCK_SIZE-startIndexInBlock); // casting to char because in some c compilers, void pointer arithmetic is not allowed. Also sizeof(char) == 1 bytes
				write_blocks(addresses[indirectBlockIndex], 1, buffer);
				bytesWritten += BLOCK_SIZE-startIndexInBlock;
			} else if (i == endBlockIndex) {
				// read block (caution to not overwrite end of block), fill up beginning of block, write to disk
				read_blocks(addresses[indirectBlockIndex], 1, buffer);
				memcpy(buffer, &buf[bytesWritten], length - bytesWritten);
				if (length - bytesWritten != endIndexInBlock)
					printf("Investigate endIndexInBlock in indirect pointers\n");
				addresses[indirectBlockIndex] = get_index();
				write_blocks(addresses[indirectBlockIndex], 1, buffer);
				bytesWritten += length - bytesWritten;
			} else {
				// fill up entire block, write to disk
				memcpy(buffer, &buf[bytesWritten], BLOCK_SIZE);
				addresses[indirectBlockIndex] = get_index();
				write_blocks(addresses[indirectBlockIndex], 1, buffer);
				bytesWritten += BLOCK_SIZE;
			}
			force_set_index(addresses[indirectBlockIndex]);
		}

		// write indirectblock back to disk
		memset(buffer, 0, BLOCK_SIZE);
		memcpy(buffer, addresses, BLOCK_SIZE);
		write_blocks(myINode.indirectPointer, 1, buffer);
	} else {
		// direct pointers

		// first block: read entire block, fill up block space, write back to disk
		memset(buffer, 0, BLOCK_SIZE);
		if (myINode.data_ptrs[startBlockIndex] == -1) { // startIndexInBlock should be 0
			myINode.data_ptrs[startBlockIndex] = get_index();
			if (startIndexInBlock != 0)
				printf("(direct pointers) startIndexInBlock should be 0. Investigate.\n");
		}
		read_blocks(myINode.data_ptrs[startBlockIndex], 1, buffer);
		memcpy((char*)buffer+startIndexInBlock, buf, BLOCK_SIZE-startIndexInBlock); // casting to char because in some c compilers, void pointer arithmetic is not allowed. Also sizeof(char) == 1 bytes
		write_blocks(myINode.data_ptrs[startBlockIndex], 1, buffer);
		bytesWritten += BLOCK_SIZE-startIndexInBlock;
		force_set_index(myINode.data_ptrs[startBlockIndex]); // in case block was originally empty


		if (endBlockIndex > 11) {
			// write to all direct pointer blocks
			for(i=startBlockIndex+1; i<=11; i++) { // ** replace the whole block
				memset(buffer, 0, BLOCK_SIZE);
				memcpy(buffer, &buf[bytesWritten], BLOCK_SIZE);
				if (myINode.data_ptrs[i] != -1)
					printf("data_ptr should be -1. Investigate.\n");
				myINode.data_ptrs[i] = get_index();
				write_blocks(myINode.data_ptrs[i], 1, buffer);
				bytesWritten += BLOCK_SIZE;
				force_set_index(myINode.data_ptrs[i]);
			}

			// indirect pointers. Initialize indirect block
			myINode.indirectPointer = get_index();
			force_set_index(myINode.indirectPointer);
			
			int nbExtraBlocks = endBlockIndex - 11;
			int addresses[NUM_ADDRESSES_INDIRECT];
			for (i=0; i<NUM_ADDRESSES_INDIRECT; i++) {
				if (nbExtraBlocks > 0) {
					nbExtraBlocks--;
					addresses[i] = get_index();
					force_set_index(addresses[i]);
					if (nbExtraBlocks == 0) { // last block to write
						// read block (caution to not overwrite end of block), fill up beginning of block, write to disk
						memset(buffer, 0, BLOCK_SIZE);
						read_blocks(addresses[i], 1, buffer);
						memcpy(buffer, &buf[bytesWritten], length - bytesWritten);
						if (length - bytesWritten != endIndexInBlock)
							printf("Investigate endIndexInBlock in indirect pointers\n");
						write_blocks(addresses[i], 1, buffer);
						bytesWritten += BLOCK_SIZE;
					} else {
						memset(buffer, 0, BLOCK_SIZE);
						memcpy(buffer, &buf[bytesWritten], BLOCK_SIZE);
						if (myINode.data_ptrs[i] != -1)
							printf("data_ptr should be -1. Investigate.\n");
						myINode.data_ptrs[i] = get_index();
						write_blocks(myINode.data_ptrs[i], 1, buffer);
						bytesWritten += BLOCK_SIZE;
					}
				} else {
					addresses[i] = -1;
				}
			}
			memset(buffer, 0, BLOCK_SIZE);
			memcpy(buffer, addresses, BLOCK_SIZE);
			write_blocks(myINode.indirectPointer, 1, buffer);
		} else {
			// only direct pointers
			for(i=startBlockIndex+1; i<=endBlockIndex; i++) { 
				if (i == endBlockIndex) {
					// fill up beginning of block, write to disk
					memset(buffer, 0, BLOCK_SIZE);
					read_blocks(myINode.data_ptrs[i], 1, buffer);
					memcpy(buffer, &buf[bytesWritten], length - bytesWritten);
					if (length - bytesWritten != endIndexInBlock)
						printf("Investigate endIndexInBlock in direct pointers\n");
					myINode.data_ptrs[i] = get_index();
					write_blocks(myINode.data_ptrs[i], 1, buffer);
					bytesWritten += length - bytesWritten;
					force_set_index(myINode.data_ptrs[i]);
				} else {
					// fill up entire block, write to disk
					memset(buffer, 0, BLOCK_SIZE);
					memcpy(buffer, &buf[bytesWritten], BLOCK_SIZE);
					myINode.data_ptrs[i] = get_index();
					write_blocks(myINode.data_ptrs[i], 1, buffer);
					bytesWritten += BLOCK_SIZE;
					force_set_index(myINode.data_ptrs[i]);
				}
			}
		}

		
	}

	myFd.rwptr += bytesWritten;
	if (myFd.size < myFd.rwptr) {
		// update size
		myFd.size = myFd.rwptr;
	}
	free(buffer);
	writeINodeTable();
	writeFreeBitMap();

	return bytesWritten;
}
int sfs_fseek(int fileID, int loc) {

}
int sfs_remove(char *file) {


}

