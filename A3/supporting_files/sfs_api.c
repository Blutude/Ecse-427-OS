
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
#define NUM_FILES (INODE_LEN-1)  // INODE_LEN - 1 because first INode belongs to root directory
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

int writeRootDirectory() { // write to disk
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

int calulateByteIndex(int blockIndex, int indexInBlock) {
	return BLOCK_SIZE*blockIndex+indexInBlock;
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
		iNodeTable[0] = (inode_t) {777, 0, 0, 0, 0, {DATABLOCKS_START_ADDRESS, DATABLOCKS_START_ADDRESS+1, DATABLOCKS_START_ADDRESS+2, -1, -1, -1, -1, -1, -1, -1, -1, -1}, -1}; // root INode points to first 3 blocks of data blocks (root directory)
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
		for (i=1; i<INODE_LEN; i++) {
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
	if (fileID <= 0 || fileID > NUM_FILES) { // fileID 0 is reserved for root
		printf("fileID %d is invalid\n", fileID);
		return -1;
	}

	// check if file already closed - iterate fd table
	if (fd[fileID].inodeIndex == -1) {
		printf("File is already closed\n");
	}

	fd[fileID] = (file_descriptor) {-1, NULL, 0};
	return 0;
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

	inode_t myINode = iNodeTable[myFd.inodeIndex];

	// calculate nb of blocks needed and index inside last block
	int rwptr = myFd.rwptr;
	int startBlockIndex = rwptr / BLOCK_SIZE;
	int startIndexInBlock = rwptr % BLOCK_SIZE; // start writing here
	int endRwptr = rwptr + length;
	int endBlockIndex = endRwptr / BLOCK_SIZE;
	int endIndexInBlock = endRwptr % BLOCK_SIZE; // exclusive

	int bytesRead = 0;

	buffer = (void*) malloc(BLOCK_SIZE);
	int addresses[NUM_ADDRESSES_INDIRECT]; // pointers inside indirect block
	int indirectBlockIndex;

	for (i=startBlockIndex; i<= endBlockIndex; i++) {
		if (calulateByteIndex(i,0) >= myFd.size) {
			printf("File cannot be read past its size\n");
			break;
		}
		memset(buffer, 0, BLOCK_SIZE);
		if (i > 11) {
			// indirect pointers

			if (i = 12) {
				// reading indirect block, initializing addresses
				read_blocks(myINode.indirectPointer, 1, buffer);
				/*for (i=0; i<NUM_ADDRESSES_INDIRECT; i++)  {
					memcpy(addresses[i], (int*)buffer+i, sizeof(int));
				}*/
				memcpy(addresses, buffer, BLOCK_SIZE);
				memset(buffer, 0, BLOCK_SIZE);
			}
			
			// write data to buf
			indirectBlockIndex = i-11-1;
			if (addresses[indirectBlockIndex] == -1) {
					printf("Address of block has not been found. Investigate\n"); // bytesIndex < myFd.size so it should be found
					break;
			}
			read_blocks(addresses[indirectBlockIndex], 1, buffer);
			if (i == startBlockIndex) {
				if (startBlockIndex == endBlockIndex) {
					// read buffer from startIndexInBlock to endIndexInBlock and write (endIndexInBlock-startIndexInBlock) bytes to buf
					memcpy(buf, buffer+startIndexInBlock, endIndexInBlock-startIndexInBlock);
					bytesRead += endIndexInBlock-startIndexInBlock;
				} else {
					// read buffer from startIndexInBlock to end of block and write (BLOCK_SIZE-startIndexInBlock) bytes to buf
					memcpy(buf, buffer+startIndexInBlock, BLOCK_SIZE-startIndexInBlock);
					bytesRead += BLOCK_SIZE-startIndexInBlock;
				}
			} else if (i == endBlockIndex) {
				// read until endIndexInBlock (or size) and write endIndexInBlock bytes to buf + bytesRead
				if (calculateByteIndex(i, endIndexInBlock) > myFd.size) {
					// read until size
					memcpy(buf+bytesRead, buffer, myFd.size-calculateByteIndex(i,0));
					bytesRead += myFd.size-calculateByteIndex(i,0);
					if (length - bytesRead != myFd.size-calculateByteIndex(i,0))
						printf("Investigate endIndexInBlock in indirect pointers\n");
					printf("File cannot be read past its size\n");
				} else {
					// read until endIndexInBlock
					memcpy(buf+bytesRead, buffer, endIndexInBlock);
					bytesRead += endIndexInBlock;
					if (length - bytesRead != endIndexInBlock)
						printf("Investigate endIndexInBlock in indirect pointers\n");
				}
			} else {
				// read entire block and write BLOCK_SIZE bytes to buf + bytesRead
				memcpy(buf+bytesRead, buffer, BLOCK_SIZE);
				bytesRead += BLOCK_SIZE;
			}
		} else {
			// direct pointers

			// write data to buf
			if (myINode.data_ptrs[i] == -1) {
					printf("Address of block has not been found. Investigate\n"); // bytesIndex < myFd.size so it should be found
					break;
			}
			read_blocks(myINode.data_ptrs[i], 1, buffer);
			if (i == startBlockIndex) {
				if (startBlockIndex == endBlockIndex) {
					// read buffer from startIndexInBlock to endIndexInBlock and write (endIndexInBlock-startIndexInBlock) bytes to buf
					memcpy(buf, buffer+startIndexInBlock, endIndexInBlock-startIndexInBlock);
					bytesRead += endIndexInBlock-startIndexInBlock;
				} else {
					// read buffer from startIndexInBlock to end of block and write (BLOCK_SIZE-startIndexInBlock) bytes to buf
					memcpy(buf, buffer+startIndexInBlock, BLOCK_SIZE-startIndexInBlock);
					bytesRead += BLOCK_SIZE-startIndexInBlock;
				}
			} else if (i == endBlockIndex) {
				// read until endIndexInBlock (or size) and write endIndexInBlock bytes to buf + bytesRead
				if (calculateByteIndex(i, endIndexInBlock) > myFd.size) {
					// read until size
					memcpy(buf+bytesRead, buffer, myFd.size-calculateByteIndex(i,0));
					bytesRead += myFd.size-calculateByteIndex(i,0);
					if (length - bytesRead != myFd.size-calculateByteIndex(i,0))
						printf("Investigate endIndexInBlock in direct pointers\n");
					printf("File cannot be read past its size\n");
				} else {
					// read until endIndexInBlock
					memcpy(buf+bytesRead, buffer, endIndexInBlock);
					bytesRead += endIndexInBlock;
					if (length - bytesRead != endIndexInBlock)
						printf("Investigate endIndexInBlock in direct pointers\n");
				}
			} else {
				// read entire block and write BLOCK_SIZE bytes to buf + bytesRead
				memcpy(buf+bytesRead, buffer, BLOCK_SIZE);
				bytesRead += BLOCK_SIZE;
			}
		}
	}

	myFd.rwptr += bytesRead;
	free(buffer);
	return bytesRead;
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

	inode_t myINode = iNodeTable[myFd.inodeIndex];

	// calculate nb of blocks needed and index inside last block
	int rwptr = myFd.rwptr;
	int startBlockIndex = rwptr / BLOCK_SIZE;
	int startIndexInBlock = rwptr % BLOCK_SIZE; // start writing here
	int endRwptr = rwptr + length;
	int endBlockIndex = endRwptr / BLOCK_SIZE;
	int endIndexInBlock = endRwptr % BLOCK_SIZE; // exclusive

	int bytesWritten = 0;

	buffer = (void*) malloc(BLOCK_SIZE);
	int addresses[NUM_ADDRESSES_INDIRECT]; // pointers inside indirect block
	int indirectBlockIndex;
	int indirectBlockAddressModified = 0; // boolean

	for (i=startBlockIndex; i<= endBlockIndex; i++) {
		memset(buffer, 0, BLOCK_SIZE);
		if (i > 11) {
			// indirect pointers

			if (i = 12) {
				// reading indirect block, initializing addresses
				if (myINode.indirectPointer == -1) {
					myINode.indirectPointer = get_index();
					int index;
					for (index=0; index<NUM_ADDRESSES_INDIRECT; index++)  {
						addresses[index] = -1;
					}
				} else {
					read_blocks(myINode.indirectPointer, 1, buffer);
					/*for (i=0; i<NUM_ADDRESSES_INDIRECT; i++)  {
						memcpy(addresses[i], (int*)buffer+i, sizeof(int));
					}*/
					memcpy(addresses, buffer, BLOCK_SIZE); // if this wrong, modify writing part of addresses below
					memset(buffer, 0, BLOCK_SIZE);
				}
			}
			
			// write data to disk
			indirectBlockIndex = i-11-1;
			if (addresses[indirectBlockIndex] == -1) {
				addresses[indirectBlockIndex] = get_index();
				indirectBlockAddressModified = 1;
			}
			read_blocks(addresses[indirectBlockIndex], 1, buffer);
			if (i == startBlockIndex) {
				if (startBlockIndex == endBlockIndex) {
					// write (endIndexInBlock-startIndexInBlock) bytes from buf to end of buffer (buffer+startIndexInBlock)
					memcpy(buffer+startIndexInBlock, buf, endIndexInBlock-startIndexInBlock);
					bytesWritten += endIndexInBlock-startIndexInBlock;
				} else {
					// write (BLOCK_SIZE-startIndexInBlock) bytes from buf to end of buffer (buffer+startIndexInBlock)
					memcpy(buffer+startIndexInBlock, buf, BLOCK_SIZE-startIndexInBlock);
					bytesWritten += BLOCK_SIZE-startIndexInBlock;
				}
			} else if (i == endBlockIndex) {
				// write endIndexInBlock bytes from buf+bytesWritten to beginning of buffer
				memcpy(buffer, buf+bytesWritten, endIndexInBlock);
				bytesWritten += endIndexInBlock;
				if (length - bytesRead != endIndexInBlock)
					printf("Investigate endIndexInBlock in indirect pointers\n");
			} else {
				// write BLOCK_SIZE bytes from buf+bytesWritten to buffer
				memcpy(buffer, buf+bytesWritten, BLOCK_SIZE);
				bytesWritten += BLOCK_SIZE;
			}
			write_blocks(addresses[indirectBlockIndex], 1, buffer);
			force_set_index(addresses[indirectBlockIndex]);
		} else {
			// direct pointers

			// write data to disk
			if (myINode.data_ptrs[i] == -1) {
				myINode.data_ptrs[i] = get_index();
				if (i == startBlockIndex && startIndexInBlock != 0)
					printf("(direct pointers) startIndexInBlock should be 0. Investigate.\n");
			}
			read_blocks(myINode.data_ptrs[i], 1, buffer);
			if (i == startBlockIndex) {
				if (startBlockIndex == endBlockIndex) {
					// write (endIndexInBlock-startIndexInBlock) bytes from buf to end of buffer (buffer+startIndexInBlock)
					memcpy(buffer+startIndexInBlock, buf, endIndexInBlock-startIndexInBlock);
					bytesWritten += endIndexInBlock-startIndexInBlock;
				} else {
					// write (BLOCK_SIZE-startIndexInBlock) bytes from buf to buffer+startIndexInBlock
					memcpy(buffer+startIndexInBlock, buf, BLOCK_SIZE-startIndexInBlock);
					bytesWritten += BLOCK_SIZE-startIndexInBlock;
				}
			} else if (i == endBlockIndex) {
				// write endIndexInBlock bytes from buf+bytesWritten to buffer
				memcpy(buffer, buf+bytesWritten, endIndexInBlock);
				bytesWritten += endIndexInBlock;
				if (length - bytesWritten != endIndexInBlock)
					printf("Investigate endIndexInBlock in direct pointers\n");
			} else {
				// write BLOCK_SIZE bytes from buf+bytesWritten to buffer
				memcpy(buffer, buf+bytesWritten, BLOCK_SIZE);
				bytesRead += BLOCK_SIZE;
			}
			write_blocks(myINode.data_ptrs[i], 1, buffer);
			force_set_index(myINode.data_ptrs[i]);
		}
	}

	// write indirectblock back to disk
	if (indirectBlockAddressModified) {
		memset(buffer, 0, BLOCK_SIZE);
		memcpy(buffer, addresses, BLOCK_SIZE);
		write_blocks(myINode.indirectPointer, 1, buffer);
	}
	
	myFd.rwptr += bytesWritten;
	if (myFd.size < myFd.rwptr) {
		myFd.size = myFd.rwptr; // update size
	}
	free(buffer);
	if (writeINodeTable() < 0)
		printf("Failure(s) writing inode table to disk\n");
	if (writeFreeBitMap() < 0)
		printf("Failure(s) writing free bit map to disk\n");

	return bytesWritten;
}

int sfs_fseek(int fileID, int loc) {
	if (fileID <= 0 || fileID > NUM_FILES) { // fileID 0 is reserved for root
		printf("fileID %d is invalid\n", fileID);
		return -1;
	}

	// make sure file already open - iterate fd table
	if (fd[fileID].inodeIndex == -1) {
		printf("File is not open\n");
		return -1;
	}

	if (loc < 0 || loc > iNodeTable[fd[fileID].inodeIndex].size) {
		printf("Attempting to set rwptr to part of file which is non existant");
		return -1;
	}

	fd[fileID].rwptr = loc;
	return 0;
}

int sfs_remove(char *file) {
	if (nameValid(file) < 0)
		return -1;

	// find file by name - iterate root directory
	int i;
	int iNodeIndex = -1;
	for (i=0; i<NUM_FILES; i++) {
		if (!strcmp(files[i].name, file)) {
			iNodeIndex = files[i].num;
			break;
		}
	}

	if (i == NUM_FILES) {
		printf("File to be removed is not found\n");
		return -1;
	}

	// Remove file from fd table
	for (i=0; i<NUM_FILES; i++) {
		if (fd[i].inodeIndex == iNodeIndex) {
			fd[i] = (file_descriptor) {-1, NULL, 0};
		}
	}

	inode_t myINode = iNodeTable[iNodeIndex];
	int endBlockIndex = myINode.size / BLOCK_SIZE;
	int addresses[NUM_ADDRESSES_INDIRECT]; // pointers inside indirect block
	int indirectBlockIndex;
	int indirectBlockAddressModified = 0; // boolean

	buffer = (void*) malloc(BLOCK_SIZE);
	for (i=0; i<=endBlockIndex; i++) {
		memset(buffer, 0, BLOCK_SIZE);
		if (i>11) {
			// indirect pointers

			if (i == 12) {
				if (myINode.indirectPointer == -1) {
					if (myINode.size != 12*BLOCK_SIZE) {
						printf("Issue with size. Investigate");
						return -1;
					}
					break;
				}
				// initialize addresses
				read_blocks(myINode.indirectPointer, 1, buffer);
				/*for (i=0; i<NUM_ADDRESSES_INDIRECT; i++)  {
					memcpy(addresses[i], (int*)buffer+i, sizeof(int));
				}*/
				memcpy(addresses, buffer, BLOCK_SIZE);
				memset(buffer, 0, BLOCK_SIZE);
			}

			if (addresses[indirectBlockIndex] == -1) {
				if (myINode.size != i*BLOCK_SIZE) {
					printf("Issue with size. Investigate");
					return -1;
				}
				break;
			}

			indirectBlockIndex = i-11-1;
			write_blocks(addresses[indirectBlockIndex], buffer, BLOCK_SIZE);
			rm_index(addresses[indirectBlockIndex]);
			addresses[indirectBlockIndex] = -1;
			indirectBlockAddressModified = 1;
		} else {
			// direct pointers
			write_blocks(myINode.data_ptrs[i], buffer, BLOCK_SIZE);
			rm_index(myINode.data_ptrs[i]);
			myINode.data_ptrs[i] = -1;
		}
	}

	// write indirectblock back to disk
	if (indirectBlockAddressModified) {
		// ** testing DELETE what's between stars
		for (i=0; i<NUM_ADDRESSES_INDIRECT; i++) {
			if (addresses[i]) != -1) {
				printf("Issue with addresses. Investigate\n");
				return -1;
			}
		}
		// **
		memset(buffer, 0, BLOCK_SIZE);
		memcpy(buffer, addresses, BLOCK_SIZE);
		write_blocks(myINode.indirectPointer, 1, buffer);
		indirectBlockAddressModified = 0;
	}

	// ** testing DELETE what's between stars
	for (i=0; i<12; i++) {
		if (myINode.data_ptrs[i] != -1) {
			printf("Issue with data_ptrs. Investigate\n");
			return -1;
		}
	}
	// **

	myINode.indirectPointer = -1;
	myINode.size = 0;
	free(buffer);
	if (writeINodeTable() < 0)
		printf("Failure(s) writing inode table to disk\n");
	if (writeFreeBitMap() < 0)
		printf("Failure(s) writing free bit map to disk\n");
	if (writeRootDirectory() < 0)
		printf("Failure(s) writing root directory to disk\n");

	return 0;
}

