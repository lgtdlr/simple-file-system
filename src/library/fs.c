// fs.cpp: File System

#include "sfs/fs.h"

// #include <algorithm>

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

size_t Blocks;
size_t InodeBlocks;
size_t Inodes;
size_t *FreeBlocks;
Disk *shared_disk;


// Auxiliary functions

// freeBlock: returns the index of the first free block in the bitmap
// returns -1 if there are no free blocks
int freeBlock() {
    for (int i = 0; i < Blocks; i++) {
        if (FreeBlocks[i] == 0) {
            return i;
        }
    }
    return -1;
}

// Debug file system -----------------------------------------------------------

void debug(Disk *disk) {
    Block block;

    // Read Superblock

    disk->readDisk(disk, 0, block.Data);

    printf("SuperBlock:\n");
    printf("    %u blocks\n"         , block.Super.Blocks);
    printf("    %u inode blocks\n"   , block.Super.InodeBlocks);
    printf("    %u inodes\n"         , block.Super.Inodes);


    // Read Inode blocks

    printf("Inodes:\n");

    for (unsigned int i = 0; i < block.Super.InodeBlocks; i++) {
        disk->readDisk(disk, 1 + i, block.Data);

        for (unsigned int j = 0; j < INODES_PER_BLOCK; j++) {
            Inode *inode = &block.Inodes[j];

            if (inode->Valid) {
                printf("    %u: size %u blocks\n", i * INODES_PER_BLOCK + j, inode->Size);
            }
        }
    }printf("Inodes:\n");



}

// Format file system ----------------------------------------------------------

bool format(Disk *disk) {

    // Write superblock

    Block block;
    block.Super.Blocks = disk->size(disk);
    block.Super.InodeBlocks = 1;
    block.Super.Inodes = INODES_PER_BLOCK;
    disk->writeDisk(disk, 0, block.Data);

    // Clear all other blocks

    memset(block.Data, 0, BLOCK_SIZE);
    for (unsigned int i = 1; i < block.Super.Blocks; i++) {
        disk->writeDisk(disk, i, block.Data);
    }



    return true;
}



// Mount file system -----------------------------------------------------------

bool mount(Disk *disk) {
    // Read superblock

    Block block;
    disk->readDisk(disk, 0, block.Data);

    // Set device and mount

    if (block.Super.Blocks != disk->size(disk)) return false;
    if (block.Super.InodeBlocks != 1) return false;
    if (block.Super.Inodes != INODES_PER_BLOCK) return false;
    disk->mount(disk);

    // Copy metadata

    Blocks = block.Super.Blocks;
    InodeBlocks = block.Super.InodeBlocks;
    Inodes = block.Super.Inodes;
    shared_disk = disk;


    // Allocate free block bitmap

    FreeBlocks = (size_t *)malloc(Blocks / 8);
    if (FreeBlocks == NULL) return false;
    memset(FreeBlocks, 0, Blocks / 8);


    return true;
}

// Create inode ----------------------------------------------------------------

size_t create() {
    // Locate free inode in inode table

    Block block;
    for (unsigned int i = 0; i < InodeBlocks; i++) {
        shared_disk->readDisk(shared_disk, 1 + i, block.Data);

        for (unsigned int j = 0; j < INODES_PER_BLOCK; j++) {
            Inode *inode = &block.Inodes[j];

            if (!inode->Valid) {
                // Clear inode and write to disk

                memset(inode, 0, sizeof(Inode));
                inode->Valid = true;
                shared_disk->writeDisk(shared_disk, 1 + i, block.Data);

                return i * INODES_PER_BLOCK + j;
            }
        }
    }

    return -1;

    // Record inode if found
    // return 0;
}

// Remove inode ----------------------------------------------------------------

bool removeInode(size_t inumber) {
    // Load inode information

    Block block;
    shared_disk->readDisk(shared_disk, 1, block.Data);
    Inode *inode = &block.Inodes[inumber];
    if (!inode->Valid) return false;

    // Free direct blocks

    for (unsigned int i = 0; i < POINTERS_PER_INODE; i++) {
        if (inode->Direct[i] != 0) {
            freeBlock(inode->Direct[i]);
            inode->Direct[i] = 0;
        }
    }


    // Free indirect blocks

    if (inode->Indirect != 0) {
        // Load indirect block

        shared_disk->readDisk(shared_disk, inode->Indirect, block.Data);

        // Free indirect blocks

        for (unsigned int i = 0; i < POINTERS_PER_BLOCK; i++) {
            if (block.Pointers[i] != 0) {
                freeBlock(block.Pointers[i]);
            }
        }

        // Free indirect block

        freeBlocks(inode->Indirect);
        inode->Indirect = 0;
    }

    // Clear inode in inode table

    memset(inode, 0, sizeof(Inode));
    shared_disk->writeDisk(shared_disk, 1, block.Data);





    return true;
}

// Inode stat ------------------------------------------------------------------

size_t stat(size_t inumber) {
    // Load inode information

    Block block;
    shared_disk->readDisk(shared_disk, 1, block.Data);
    Inode *inode = &block.Inodes[inumber];
    if (!inode->Valid) return -1;

    // Return inode size

    return inode->Size;
//    return 0;
}

// Read from inode -------------------------------------------------------------

size_t readInode(size_t inumber, char *data, size_t length, size_t offset) {
    // Load inode information

    Block block;
    shared_disk->readDisk(shared_disk, 1, block.Data);
    Inode *inode = &block.Inodes[inumber];
    if (!inode->Valid) return -1;


    // Adjust length

    if (offset >= inode->Size) return 0;
    if (offset + length > inode->Size) length = inode->Size - offset;

    // Read block and copy to data

    unsigned int blockNumber = offset / BLOCK_SIZE;
    unsigned int blockOffset = offset % BLOCK_SIZE;
    unsigned int blockLength = BLOCK_SIZE - blockOffset;
    if (blockLength > length) blockLength = length;
    shared_disk->readDisk(shared_disk, inode->Direct[blockNumber], block.Data);
    memcpy(data, block.Data + blockOffset, blockLength);
    data += blockLength;
    length -= blockLength;

    // Read remaining blocks

    while (length > 0) {
        blockNumber++;
        blockLength = BLOCK_SIZE;
        if (blockLength > length) blockLength = length;
        shared_disk->readDisk(shared_disk, inode->Direct[blockNumber], block.Data);
        memcpy(data, block.Data, blockLength);
        data += blockLength;
        length -= blockLength;
    }

    return length;
//    return 0;
}

// Write to inode --------------------------------------------------------------

size_t writeInode(size_t inumber, char *data, size_t length, size_t offset) {
    // Load inode

    Block block;
    shared_disk->readDisk(shared_disk, 1, block.Data);
    Inode *inode = &block.Inodes[inumber];
    if (!inode->Valid) return -1;

    // Write block and copy to data

    unsigned int blockNumber = offset / BLOCK_SIZE;
    unsigned int blockOffset = offset % BLOCK_SIZE;
    unsigned int blockLength = BLOCK_SIZE - blockOffset;
    if (blockLength > length) blockLength = length;
    shared_disk->readDisk(shared_disk, inode->Direct[blockNumber], block.Data);
    memcpy(block.Data + blockOffset, data, blockLength);
    shared_disk->writeDisk(shared_disk, inode->Direct[blockNumber], block.Data);
    data += blockLength;
    length -= blockLength;

    // Write remaining blocks

    while (length > 0) {
        blockNumber++;
        blockLength = BLOCK_SIZE;
        if (blockLength > length) blockLength = length;
        shared_disk->readDisk(shared_disk, inode->Direct[blockNumber], block.Data);
        memcpy(block.Data, data, blockLength);
        shared_disk->writeDisk(shared_disk, inode->Direct[blockNumber], block.Data);
        data += blockLength;
        length -= blockLength;
    }

    return length;
//    return 0;
}
