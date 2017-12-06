
#include "sfs_api.h"
#include "bitmap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fuse.h>
#include <strings.h>
#include "disk_emu.h"
#define AZRAK_RONY_DISK "sfs_disk.disk"
#define BLOCK_SIZE 1024
#define INODE_LEN 100
#define NUM_FILES (INODE_LEN-1)  // INODE_LEN - 1 because first INode belongs to root directory
#define NUM_BLOCKS_INODETABLE ((INODE_LEN)*(sizeof(inode_t))/BLOCK_SIZE + (((INODE_LEN)*(sizeof(inode_t)))%BLOCK_SIZE > 0)) // getting ceiling value = 8
#define NUM_BLOCKS_ROOTDIR ((NUM_FILES)*(sizeof(directory_entry))/BLOCK_SIZE + (((NUM_FILES)*(sizeof(directory_entry)))%BLOCK_SIZE > 0)) // getting ceiling value = 3
#define DATABLOCKS_START_ADDRESS (1+NUM_BLOCKS_INODETABLE) // = 9
#define NUM_ADDRESSES_INDIRECT (BLOCK_SIZE/sizeof(int))

struct superblock_t superblock;
struct file_descriptor fd[INODE_LEN]; 
struct directory_entry files[NUM_FILES];
struct inode_t iNodeTable[INODE_LEN];
void* buffer;
int filesVisited = 0; // for sfs_get_next_file_name
int totalFiles = 0; // for sfs_get_next_file_name


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
	memcpy(buffer, free_bit_map, (SIZE)*(sizeof(uint8_t)));
	int r = write_blocks(NUM_BLOCKS-1, 1, buffer);
	free(buffer);
	return r;
}


// **** Other helpers
int nameValid(const char* name) {
	int length = strlen(name);
	if (length > MAX_FILE_NAME)
		return -1;
	char copy[length+1];
	memset(copy, '\0', sizeof(copy));
	strcpy(copy, name); // because strtok modifies the string

	char* token;
	const char s[2] = ".";
	token = strtok(copy, s); // token = "([]).[]"
	token = strtok(NULL, s); // token = "[].([])"
	if (strlen(token) > MAX_EXTENSION_NAME)
		return -1;
	return 0;
}

int assertDefinedValues() {
	if (NUM_BLOCKS_ROOTDIR != 3 || NUM_BLOCKS_INODETABLE != 8 || DATABLOCKS_START_ADDRESS != 9)
		return -1;
	else
		return 0;
}

int calculateByteIndex(int blockIndex, int indexInBlock) {
	return BLOCK_SIZE*blockIndex+indexInBlock;
}

void printInodeNbThreeTestHelper() {
	int i;
	printf("\n");
	printf("Inode 3\n");
	for (i=0; i<12; i++)
		printf("Data ptrs %d: %d\n", i, iNodeTable[3].data_ptrs[i]);
	printf("\n");
	if (iNodeTable[3].indirectPointer != -1) {
		printf("Indirect block: %d\n", iNodeTable[3].indirectPointer);
		void *myBuf = (void*) malloc(BLOCK_SIZE);
		memset(myBuf, 0, BLOCK_SIZE);
		read_blocks(iNodeTable[3].indirectPointer, 1, myBuf);
		int ind_ptrs[NUM_ADDRESSES_INDIRECT];
		memcpy(ind_ptrs, myBuf, BLOCK_SIZE);
		for (i=0; i<NUM_ADDRESSES_INDIRECT; i++)
			printf("Indirect ptrs %d: %d\n", i, ind_ptrs[i]);
		printf("\n");
		memset(myBuf, 0, BLOCK_SIZE);
		free(myBuf);
	}
}


// *********************************************************************************
// *********************************************************************************

void mksfs(int fresh) {
	if (assertDefinedValues() == -1) {
		printf("Issue with defined values. Investigate.\n");
		return;
	}
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
		int i;
		for (i=1; i<INODE_LEN; i++)
			iNodeTable[i] = (inode_t) {777, 0, 0, 0, -1, {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}, -1};
		if (writeINodeTable() < 0)
				printf("Failure(s) writing inode table\n");
		for (i=0; i<NUM_BLOCKS_INODETABLE; i++)
			force_set_index(i+1); // 0 is for superblock



		// init directory entries
		for (i=0; i<NUM_FILES; i++) {
			files[i].num = -1;
			memset(files[i].name, '\0', sizeof(files[i].name));
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
		memcpy(free_bit_map, buffer, (SIZE)*(sizeof(uint8_t)));
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
		int i;
		for (i=0; i<NUM_BLOCKS_INODETABLE; i++)
			force_set_index(i+1);


		// read root directory
		buffer = (void*) malloc(NUM_BLOCKS_ROOTDIR*BLOCK_SIZE);
		memset(buffer, 0, NUM_BLOCKS_ROOTDIR*BLOCK_SIZE);
		read_blocks(DATABLOCKS_START_ADDRESS, NUM_BLOCKS_ROOTDIR, buffer);
		memcpy(files, buffer, NUM_FILES*sizeof(directory_entry));
		free(buffer);
		for (i=0; i<NUM_BLOCKS_ROOTDIR; i++)
			force_set_index(i+DATABLOCKS_START_ADDRESS);


		// init file descriptors. Is not written in disk, is in-memory
		fd[0] = (file_descriptor) {0, &iNodeTable[0], 0};
		for (i=1; i<INODE_LEN; i++) {
			fd[i] = (file_descriptor) {-1, NULL, 0};
		}
	}

	if (r != 0) {
		printf("Failure initializing disk\n");
		return;
	}
}

int sfs_getnextfilename(char *fname) {
	if (totalFiles == filesVisited) { // get back to beginning of the list
		filesVisited = 0;
		return 0;
	}

	// iterate through all files in root
	int fileIndex = 0;
	int i;
	for (i=0; i<NUM_FILES; i++){
		if (files[i].num < 0) {
			if (fileIndex == filesVisited) {
				memcpy(fname, files[i].name, sizeof(files[i].name));
				break;
			}
			fileIndex++;
		}
	}
	filesVisited++;
	return 1;
}

int sfs_getfilesize(const char* path) {
	if (nameValid(path) < 0)
		return -1;

	// find file by name - iterate through root directory
	int i;
	for (i=0; i<NUM_FILES; i++) {
		if (files[i].num != -1 && !strcmp(files[i].name, path))
			return iNodeTable[files[i].num].size;
	}
	printf("File %s not found\n", path);
	return -1;
}

int sfs_fopen(char *name) { // make sure can't open same file twice
	if (nameValid(name) < 0)
		return -1;

	// find file by name - iterate root directory
	int iNodeIndex = -1;
	int exists = 0;
	int i;
	for (i=0; i<NUM_FILES; i++) {
		if (!strcmp(files[i].name, name)) {
			exists = 1;
			iNodeIndex = files[i].num;
			break;
		}
	}

	if (exists) {
		// check if file already open - iterate fd table
		for (i=1; i<INODE_LEN; i++) { // 0 is for root directory
			if (fd[i].inodeIndex == iNodeIndex) {
				return i; // file already open
			}
		}
	} else {
		// find slot in root direcory
		int fileIndex = -1;
		for (i=0; i<NUM_FILES; i++) {
			if (files[i].num < 0) {
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
			if (iNodeTable[i].size == -1) {
				iNodeIndex = i;
				break;
			}
		}
		if (iNodeIndex < 0) {
			printf("Investigate!!!!\n");
			return -1;
		}

		// got fileIndex and iNodeIndex
		strcpy(files[fileIndex].name, name);
		files[fileIndex].num = iNodeIndex;
		iNodeTable[iNodeIndex].size = 0; // value that determines inode is used (-1)
		// write to disk
		if (writeINodeTable() < 0)
			printf("Failure(s) writing iNode to disk");
		if (writeRootDirectory() < 0)
			printf("Failure(s) writing root directory to disk");
	}

	// file is not open yet - find fd slot - iterate fd table
	int fdIndex = -1;
	for (i=1; i<INODE_LEN; i++) { // 0 is for root directory
		if (fd[i].inodeIndex == -1) { // found free file descriptor slot
			fdIndex = i;
			break;
		}
	}
	if (i == INODE_LEN) { // found no available slot
		printf("Open file descriptor table full. Investigate\n");
		return -1;
	}

	int rwptr = iNodeTable[iNodeIndex].size;
	fd[fdIndex] = (file_descriptor) {iNodeIndex, &iNodeTable[iNodeIndex], rwptr};
	totalFiles++;

	return fdIndex;
}

int sfs_fclose(int fileID) {
	if (fileID <= 0 || fileID > NUM_FILES) { // fileID 0 is reserved for root
		printf("fileID %d is invalid\n", fileID);
		return -1;
	}

	// check if file already closed
	if (fd[fileID].inodeIndex == -1) {
		return -1;
	}

	fd[fileID] = (file_descriptor) {-1, NULL, 0};
	totalFiles--;
	return 0;
}

int sfs_fread(int fileID, char *buf, int length) {
	if (length < 0) {
		printf("Length cannot be negative\n");
		return -1;
	}

	file_descriptor *myFd;
	myFd = &fd[fileID];

	// check that file is open
	if ((*myFd).inodeIndex == -1) {
		return -1;
	}

	inode_t *myINode;
	myINode = (*myFd).inode;
	
	// calculate nb of blocks needed and index inside last block
	int rwptr = (*myFd).rwptr;
	int startBlockIndex = rwptr / BLOCK_SIZE;
	int startIndexInBlock = rwptr % BLOCK_SIZE; // start writing here
	int endRwptr;
	if (rwptr+length > (*myINode).size) {
		endRwptr = (*myINode).size;
	} else {
		endRwptr = rwptr + length;
	}
	int endBlockIndex = endRwptr / BLOCK_SIZE;
	int endIndexInBlock = endRwptr % BLOCK_SIZE; // exclusive

	int bytesRead = 0;

	buffer = (void*) malloc(BLOCK_SIZE);
	int addresses[NUM_ADDRESSES_INDIRECT]; // pointers inside indirect block
	int addressesInitialized = 0;
	int indirectBlockIndex;

	int i;
	for (i=startBlockIndex; i<= endBlockIndex; i++) {
		int tmp = calculateByteIndex(i,0);
		if (tmp >= (*myINode).size) {
			printf("File cannot be read past its size\n"); // should not pass here
			printf("SOMETHING WRONG - i: %d myINode.size %d\n", i, (*myINode).size);
			break;
		}
		memset(buffer, 0, BLOCK_SIZE);
		if (i > 11) {
			// indirect pointers

			if (!addressesInitialized) {
				// reading indirect block, initializing addresses
				read_blocks((*myINode).indirectPointer, 1, buffer);
				memcpy(addresses, buffer, BLOCK_SIZE);
				memset(buffer, 0, BLOCK_SIZE);
				addressesInitialized = 1;
			}
			
			// write data to buf
			indirectBlockIndex = i-11-1;
			if (addresses[indirectBlockIndex] == -1) {
					printf("Address of block has not been found. Investigate\n"); // bytesIndex < muINode.size so it should be found
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
				if (calculateByteIndex(i, endIndexInBlock) > (*myINode).size)
					printf("SOMETHING WRONG - i: %d endIndexInBlock: %d myINode.size %d\n", i, endIndexInBlock, (*myINode).size);
				// read until endIndexInBlock
				memcpy(buf+bytesRead, buffer, endIndexInBlock);
				if (length - bytesRead != endIndexInBlock)
					printf("Investigate endIndexInBlock in indirect pointers\n");
				bytesRead += endIndexInBlock;
				
			} else {
				// read entire block and write BLOCK_SIZE bytes to buf + bytesRead
				memcpy(buf+bytesRead, buffer, BLOCK_SIZE);
				bytesRead += BLOCK_SIZE;
			}
		} else {
			// direct pointers

			// write data to buf
			if ((*myINode).data_ptrs[i] == -1) {
					printf("Address of block has not been found. Investigate\n"); // bytesIndex < myINode.size so it should be found
					break;
			}
			read_blocks((*myINode).data_ptrs[i], 1, buffer);
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
				if (calculateByteIndex(i, endIndexInBlock) > (*myINode).size)
					printf("SOMETHING WRONG - i: %d endIndexInBlock: %d myINode.size %d\n", i, endIndexInBlock, (*myINode).size);
				// read until endIndexInBlock
				memcpy(buf+bytesRead, buffer, endIndexInBlock);
				if (length - bytesRead != endIndexInBlock)
					printf("Investigate endIndexInBlock in direct pointers\n");
				bytesRead += endIndexInBlock;
			} else {
				// read entire block and write BLOCK_SIZE bytes to buf + bytesRead
				memcpy(buf+bytesRead, buffer, BLOCK_SIZE);
				bytesRead += BLOCK_SIZE;
			}
		}
	}

	(*myFd).rwptr += bytesRead;
	free(buffer);
	return bytesRead;
}

int sfs_fwrite(int fileID, const char *buf, int length) {
	if (length < 0) {
		printf("Length cannot be negative");
		return -1;
	}

	file_descriptor *myFd;
	myFd = &fd[fileID];

	// check that file is open
	if ((*myFd).inodeIndex == -1) {
		printf("File is not open");
		return -1;
	}

	inode_t *myINode;
	myINode = (*myFd).inode;

	// calculate nb of blocks needed and index inside last block
	int rwptr = (*myFd).rwptr;
	int startBlockIndex = rwptr / BLOCK_SIZE;
	int startIndexInBlock = rwptr % BLOCK_SIZE; // start writing here
	int endRwptr = rwptr + length;
	int endBlockIndex = endRwptr / BLOCK_SIZE;
	int endIndexInBlock = endRwptr % BLOCK_SIZE; // exclusive

	int bytesWritten = 0;

	buffer = (void*) malloc(BLOCK_SIZE);
	int addresses[NUM_ADDRESSES_INDIRECT]; // pointers inside indirect block
	int addressesInitialized = 0;
	int indirectBlockIndex;
	int indirectBlockAddressModified = 0; // boolean

	int fullError = 0;

	int i;
	for (i=startBlockIndex; i<= endBlockIndex; i++) {
		memset(buffer, 0, BLOCK_SIZE);
		if (i > 11) {
			// indirect pointers

			if (!addressesInitialized) {
				// reading indirect block, initializing addresses
				if ((*myINode).indirectPointer == -1) {
					if (get_index() > 1023 || get_index() < 0) {
						printf("No more available blocks in bit map\n");
						fullError = 1;
						break;
					}
					(*myINode).indirectPointer = get_index();
					force_set_index((*myINode).indirectPointer);
					int index;
					for (index=0; index<NUM_ADDRESSES_INDIRECT; index++) 
						addresses[index] = -1;
					indirectBlockAddressModified = 1;
				} else {
					read_blocks((*myINode).indirectPointer, 1, buffer);
					memcpy(addresses, buffer, BLOCK_SIZE);
					memset(buffer, 0, BLOCK_SIZE);
				}
				addressesInitialized = 1;
			}
			
			// write data to disk
			indirectBlockIndex = i-11-1;
			if (indirectBlockIndex >= NUM_ADDRESSES_INDIRECT) {
				printf("Max file size has been reached\n");
				fullError = 1;
				break;
			}
			if (addresses[indirectBlockIndex] == -1) {
				if (get_index() > 1023 || get_index() < 0) {
					printf("No more available blocks in bit map\n");
					fullError = 1;
					break;
				}
				addresses[indirectBlockIndex] = get_index();
				force_set_index(addresses[indirectBlockIndex]);
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
				if (length - bytesWritten != endIndexInBlock)
					printf("Investigate endIndexInBlock in indirect pointers\n");
				bytesWritten += endIndexInBlock;
			} else {
				// write BLOCK_SIZE bytes from buf+bytesWritten to buffer
				memcpy(buffer, buf+bytesWritten, BLOCK_SIZE);
				bytesWritten += BLOCK_SIZE;
			}
			if (addresses[indirectBlockIndex] < 0 || addresses[indirectBlockIndex] > 1023) {
				printf("Why indirect pointer invalid value??\n");
				fullError = 1;
				break;
			} else {
				write_blocks(addresses[indirectBlockIndex], 1, buffer);
			}
		} else {
			// direct pointers

			// write data to disk
			if ((*myINode).data_ptrs[i] == -1) {
				if (get_index() > 1023 || get_index() < 0) {
					printf("No more available blocks in bit map\n");
					fullError = 1;
					break;
				}
				(*myINode).data_ptrs[i] = get_index();
				force_set_index((*myINode).data_ptrs[i]);
				if (i == startBlockIndex && startIndexInBlock != 0)
					printf("(direct pointers) startIndexInBlock should be 0. Investigate.\n");
			}
			read_blocks((*myINode).data_ptrs[i], 1, buffer);
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
				if (length - bytesWritten != endIndexInBlock)
					printf("Investigate endIndexInBlock in direct pointers\n");
				bytesWritten += endIndexInBlock;
			} else {
				// write BLOCK_SIZE bytes from buf+bytesWritten to buffer
				memcpy(buffer, buf+bytesWritten, BLOCK_SIZE);
				bytesWritten += BLOCK_SIZE;
			}
			if ((*myINode).data_ptrs[i] < 0 || (*myINode).data_ptrs[i] > 1023) {
				printf("Why data pointer invalid value??\n");
				fullError = 1;
				break;
			} else {
				write_blocks((*myINode).data_ptrs[i], 1, buffer);
			}
		}
	}

	// write indirectblock back to disk
	if (indirectBlockAddressModified) {
		memset(buffer, 0, BLOCK_SIZE);
		memcpy(buffer, addresses, BLOCK_SIZE);
		if ((*myINode).indirectPointer < 0 || (*myINode).indirectPointer > 1023) {
			printf("Why indirect block invalid value??\n");
			fullError = 1;
		} else {
			write_blocks((*myINode).indirectPointer, 1, buffer);
		}
	}

	(*myFd).rwptr += bytesWritten;
	if ((*myINode).size < (*myFd).rwptr) {
		(*myINode).size = (*myFd).rwptr; // update size
	}
	free(buffer);
	if (writeINodeTable() < 0)
		printf("Failure(s) writing inode table to disk\n");
	if (writeFreeBitMap() < 0)
		printf("Failure(s) writing free bit map to disk\n");

	if (fullError)
		return -1;
	else
		return bytesWritten;
}

int sfs_fseek(int fileID, int loc) {
	if (fileID <= 0 || fileID > NUM_FILES) { // fileID 0 is reserved for root
		printf("fileID %d is invalid\n", fileID);
		return -1;
	}

	// make sure file already open
	if (fd[fileID].inodeIndex == -1) {
		printf("File is not open\n");
		return -1;
	}

	if (loc < 0) {
		fd[fileID].rwptr = 0;
	} else if (loc > iNodeTable[fd[fileID].inodeIndex].size) {
		fd[fileID].rwptr = iNodeTable[fd[fileID].inodeIndex].size;
	} else {
		fd[fileID].rwptr = loc;
	}

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
	for (i=1; i<INODE_LEN; i++) {
		if (fd[i].inodeIndex == iNodeIndex) {
			fd[i] = (file_descriptor) {-1, NULL, 0};
			totalFiles--;
		}
	}

	inode_t *myINode;
	myINode = &iNodeTable[iNodeIndex];
	int endBlockIndex = (*myINode).size / BLOCK_SIZE;
	int addresses[NUM_ADDRESSES_INDIRECT]; // pointers inside indirect block
	int addressesInitialized = 0;
	int indirectBlockIndex;
	int indirectBlockAddressModified = 0; // boolean

	buffer = (void*) malloc(BLOCK_SIZE);
	for (i=0; i<=endBlockIndex; i++) {
		memset(buffer, 0, BLOCK_SIZE);
		if (i>11) {
			// indirect pointers

			if (!addressesInitialized) {
				if ((*myINode).indirectPointer == -1) {
					if ((*myINode).size != 12*BLOCK_SIZE) {
						printf("Issue with size. Investigate\n");
						return -1;
					}
					break;
				}
				// initialize addresses
				read_blocks((*myINode).indirectPointer, 1, buffer);
				memcpy(addresses, buffer, BLOCK_SIZE);
				memset(buffer, 0, BLOCK_SIZE);
				addressesInitialized = 1;
			}

			indirectBlockIndex = i-11-1;
			if (addresses[indirectBlockIndex] == -1) {
				if ((*myINode).size != i*BLOCK_SIZE) {
					printf("Issue with size. Investigate\n");
					return -1;
				}
				break;
			}
			write_blocks(addresses[indirectBlockIndex], 1, buffer);
			rm_index(addresses[indirectBlockIndex]);
			addresses[indirectBlockIndex] = -1;
			indirectBlockAddressModified = 1;
		} else {
			// direct pointers
			write_blocks((*myINode).data_ptrs[i], 1, buffer);
			rm_index((*myINode).data_ptrs[i]);
			(*myINode).data_ptrs[i] = -1;
		}
	}

	// write indirectblock back to disk
	if (indirectBlockAddressModified) {
		memset(buffer, 0, BLOCK_SIZE);
		memcpy(buffer, addresses, BLOCK_SIZE);
		write_blocks((*myINode).indirectPointer, 1, buffer);
		indirectBlockAddressModified = 0;
	}

	if (addressesInitialized) { // indirectPointer has valid block address
		rm_index((*myINode).indirectPointer);
		(*myINode).indirectPointer = -1;
	} else {
		if ((*myINode).indirectPointer != -1)
			printf("Indirect pointer should be -1 but is %d. Investigate!\n", (*myINode).indirectPointer);
	}
	(*myINode).size = -1;
	free(buffer);
	if (writeINodeTable() < 0)
		printf("Failure(s) writing inode table to disk\n");
	if (writeFreeBitMap() < 0)
		printf("Failure(s) writing free bit map to disk\n");
	if (writeRootDirectory() < 0)
		printf("Failure(s) writing root directory to disk\n");

	return 0;
}

