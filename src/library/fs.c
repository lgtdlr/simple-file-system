// fs.cpp: File System

#include "sfs/fs.h"

// #include <algorithm>

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>


// Macros
#define CEILING_POS(X) ((X-(int)(X)) > 0 ? (int)(X+1) : (int)(X))
#define CEILING_NEG(X) ((int)(X))
#define CEILING(X) ( ((X) > 0) ? CEILING_POS(X) : CEILING_NEG(X) )

// Mounted file system
size_t Blocks;
size_t InodeBlocks;
size_t Inodes;
Disk* mounted_disk;
bool *free_block_bitmap;

// Helper Functions
ssize_t allocate_free_block();
bool save_inode(size_t inumber, Inode *inode);
bool load_inode(size_t inumber, Inode *inode);

// Debug file system -----------------------------------------------------------

// This method scans a mounted filesystem and reports on how the inodes and blocks are organized. Your output from this method should be similar to the following:
void debug(Disk *disk) {


    // Read Superblock
    Block superBlock;
    disk->readDisk(disk, 0, superBlock.Data);

    printf("SuperBlock:\n");
    printf("    magic number is %s\n",  (superBlock.Super.MagicNumber == MAGIC_NUMBER ? "valid" : "invalid"));
    printf("    %u blocks\n", superBlock.Super.Blocks);
    printf("    %u inode blocks\n", superBlock.Super.InodeBlocks);
    printf("    %u inodes\n", superBlock.Super.Inodes);

    // Read Inode blocks
    int inodeBlocks = superBlock.Super.InodeBlocks;

    if (inodeBlocks == 0)
        return;

    Block InodeBlock;
    for (int i = 1; i <= inodeBlocks; i++) {
        disk->readDisk(disk, i, InodeBlock.Data);

        for (size_t j = 0; j < INODES_PER_BLOCK; j++) {
            Inode current_inode = InodeBlock.Inodes[j];
            if (current_inode.Valid == 0) {
                continue;
            }
            printf("Inode %ld:\n", j);
            printf("    size: %u bytes\n", current_inode.Size);
            printf("    direct blocks:");
            for (size_t k = 0; k < POINTERS_PER_INODE; k++) {
                if (current_inode.Direct[k]) {
                    printf(" %u", current_inode.Direct[k]);
                }
            }
            printf("\n");

            if (current_inode.Indirect != 0) {
                Block indirectBlock;
                disk->readDisk(disk, current_inode.Indirect, indirectBlock.Data);
                printf("    indirect block: %d\n", current_inode.Indirect);
                printf("    indirect data blocks:");
                for (size_t k = 0; k < POINTERS_PER_BLOCK; k++) {
                    if (indirectBlock.Pointers[k] != 0) {
                        printf(" %u", indirectBlock.Pointers[k]);
                    }
                }
                printf("\n");
            }

        }
    }
}

// Format file system ----------------------------------------------------------

//This method Creates a new filesystem on the disk, destroying any data already present. It should set aside ten percent of the blocks for inodes, clear the inode table, and write the superblock. It must
//return true on success, false otherwise.
//Note: formatting a filesystem does not cause it to be mounted. Also, an attempt to format an already-mounted disk should do nothing and return failure.
bool format(Disk *disk) {

    // Check if disk is mounted
    if (disk->mounted(disk)) {
        return false;
    }

    // Write superblock
    Block superBlock;
    memset(superBlock.Data, 0, BLOCK_SIZE);
    superBlock.Super.MagicNumber = MAGIC_NUMBER;
    superBlock.Super.Blocks = disk->Blocks;
    superBlock.Super.InodeBlocks = (size_t)CEILING((float)disk->Blocks/10);
    superBlock.Super.Inodes = superBlock.Super.InodeBlocks * INODES_PER_BLOCK;

    disk->writeDisk(disk, 0, superBlock.Data);

    // Clear all other blocks
    Block emptyBlock;
    memset(emptyBlock.Data, 0, BLOCK_SIZE);
    for (size_t i = 1; i < disk->Blocks; i++) {
        disk->writeDisk(disk, i, emptyBlock.Data);
    }

    return true;
}

// Mount file system -----------------------------------------------------------

// This method examines the disk for a filesystem. If one is present, read the superblock, build a free block free_block_bitmap, and prepare the filesystem for use. Return true on success, false otherwise.
// Note: a successful mount is a pre-requisite for the remaining calls.
// If a filesystem is already mounted, this method should do nothing and return failure.
bool mount(Disk *disk) {

    // Check if disk is mounted
    if (disk->mounted(disk)) {
        return false;
    }

    // Read superblock
    Block block;
    disk->readDisk(disk, 0, block.Data);

    // Check if superblock is valid
    if (block.Super.Inodes != block.Super.InodeBlocks * INODES_PER_BLOCK ||
       block.Super.MagicNumber != MAGIC_NUMBER || block.Super.Blocks < 0 ||
       block.Super.InodeBlocks != CEILING(0.1 * block.Super.Blocks)){
        return false;
    }

    // Set device and mount
    mounted_disk = disk;
    disk->mount(disk);

    // Copy metadata
    Blocks = block.Super.Blocks;
    InodeBlocks = block.Super.InodeBlocks;
    Inodes = block.Super.Inodes;
    mounted_disk = disk;

    // Allocate free block free_block_bitmap
    free_block_bitmap = (bool*)malloc(Blocks * sizeof(bool));

    // Set all blocks as free
    for (int i = 0; i < Blocks; i++) {
        free_block_bitmap[i] = true;
    }

    //Mark superblock as used
    free_block_bitmap[0] = false;

    //Mark inode blocks as used
    for (int i = 1; i <= InodeBlocks; i++) {
        free_block_bitmap[i] = false;
    }

    //Mark inodes as used
    Block InodeBlock;
    for (int i = 1; i <= InodeBlocks; i++) { //Read Inode blocks
        disk->readDisk(disk, i, InodeBlock.Data);

        for (size_t j = 0; j < INODES_PER_BLOCK; j++) { //Read Inodes
            Inode current_inode = InodeBlock.Inodes[j];
            if (!current_inode.Valid) {
                continue;
            }
            for (size_t k = 0; k < POINTERS_PER_INODE; k++) { //Read Direct Blocks
                if (current_inode.Direct[k]) {
                    free_block_bitmap[current_inode.Direct[k]] = false;
                }
            }
            if (current_inode.Indirect != 0) { //Read Indirect Blocks
                free_block_bitmap[current_inode.Indirect] = false;
                Block indirectBlock;
                disk->readDisk(disk, current_inode.Indirect, indirectBlock.Data);
                for (size_t k = 0; k < POINTERS_PER_BLOCK; k++) { //Read Indirect Data Blocks
                    if (indirectBlock.Pointers[k] != 0) {
                        free_block_bitmap[indirectBlock.Pointers[k]] = false;
                    }
                }
            }
        }
    }

    return true;
}

// Create inode ----------------------------------------------------------------

//This method Creates a new inode of zero length. On success, return the inumber . On failure, return -1 .
size_t create() {

    // Locate free inode in inode table
    Block InodeBlock;
    for (int i = 1; i <= InodeBlocks; i++) {
        mounted_disk->readDisk(mounted_disk, i, InodeBlock.Data);

        for (size_t j = 0; j < INODES_PER_BLOCK; j++) {
            Inode current_inode = InodeBlock.Inodes[j];
            if (current_inode.Valid == 0) {
                // Create new inode
                Inode new_inode;
                new_inode.Valid = 1;
                new_inode.Size = 0;
                for (size_t k = 0; k < POINTERS_PER_INODE; k++) {
                    new_inode.Direct[k] = 0;
                }
                new_inode.Indirect = 0;

                // Write new inode to disk
                InodeBlock.Inodes[j] = new_inode;
                mounted_disk->writeDisk(mounted_disk, i, InodeBlock.Data);

                // Return inode number
                return (i - 1) * INODES_PER_BLOCK + j;
            }
        }
    }

    return -1;
}

// Remove inode ----------------------------------------------------------------

// This method removes the inode indicated by the inumber . It should release all data and indirect blocks assigned to this inode and return them to the free block map. On success, it returns true .
// On failure, it returns false .
bool removeInode(size_t inumber) {
    Inode inode;

    // Load inode information
    if (!load_inode(inumber, &inode)) {
        return false;
    }

    if (inode.Valid == 0) {
        return false;
    }

    // Free direct blocks
    for (size_t i = 0; i < POINTERS_PER_INODE; i++) {
        if (inode.Direct[i] != 0) {
            free_block_bitmap[inode.Direct[i]] = true;
            inode.Direct[i] = 0;
        }
    }

    // Free indirect blocks
    if (inode.Indirect != 0) {
        free_block_bitmap[inode.Indirect] = true;
        inode.Indirect = 0;

        Block indirectBlock;
        mounted_disk->readDisk(mounted_disk, inode.Indirect, indirectBlock.Data);
        for (size_t i = 0; i < POINTERS_PER_BLOCK; i++) {
            if (indirectBlock.Pointers[i] != 0) {
                free_block_bitmap[indirectBlock.Pointers[i]] = true;
                indirectBlock.Pointers[i] = 0;
            }
        }
        mounted_disk->writeDisk(mounted_disk, inode.Indirect, indirectBlock.Data);
    }

    // Free inode
    inode.Valid = 0;
    inode.Size = 0;
    save_inode(inumber, &inode);

    return true;

}

// Inode stat ------------------------------------------------------------------

// This method returns the logical size of the given inumber , in bytes. Note that zero is a valid logical size for an inode. On failure, it returns -1 .
size_t stat(size_t inumber) {
    // Load inode information
    Inode inode;
    if (!load_inode(inumber, &inode)) {
        return -1;
    }

    if (inode.Valid == 0) {
        return -1;
    }

    return inode.Size;
}

// Read from inode -------------------------------------------------------------

// This method reads data from a valid inode. It then copies length bytes from the data blocks of the inode into the data pointer, starting at offset in the inode. It should return the total number
// of bytes read. If the given inumber is invalid, or any other error is encountered, the method returns -1 .
// Note: the number of bytes actually read could be smaller than the number of bytes requested, perhaps if the end of the inode is reached.
size_t readInode(size_t inumber, char *data, size_t length, size_t offset) {

    // Load inode information
    Inode inode;
    if (!load_inode(inumber, &inode)) {
        return -1;
    }

    // Check if offset is out of bounds
    if (offset > inode.Size) {
        return -1;
    }

    // Adjust length
    if (length > inode.Size - offset) {
        length = inode.Size - offset;
    }

    size_t startBlock = offset / BLOCK_SIZE;
    size_t endBlock = (offset + length) / BLOCK_SIZE;

    // Read block and copy to data
    Block indirect;

    // Only read indirect block if needed
    if (endBlock >= POINTERS_PER_INODE) {
        mounted_disk->readDisk(mounted_disk, inode.Indirect, indirect.Data);
    }


    size_t bytes_read = 0;
    for (size_t i = startBlock; i < endBlock + 1 && bytes_read < length; i++) {
        Block block;
        if (i < POINTERS_PER_INODE) {
            mounted_disk->readDisk(mounted_disk, inode.Direct[i], block.Data);
        } else {
            mounted_disk->readDisk(mounted_disk, indirect.Pointers[i - POINTERS_PER_INODE], block.Data);
        }

        size_t start = 0;
        size_t end = BLOCK_SIZE;
        if (i == startBlock) {
            start = offset % BLOCK_SIZE;
        }
        if (i == (offset + length) / BLOCK_SIZE) {
            end = (offset + length) % BLOCK_SIZE;
        }

        for (size_t j = start; j < end; j++) {
            data[bytes_read] = block.Data[j];
            bytes_read++;
        }



    }



    return bytes_read;

}

// Write to inode --------------------------------------------------------------

// This method writes data to a valid inode by copying length bytes from the pointer data into the data blocks of the inode starting at offset bytes. It will allocate any necessary direct and indirect
// blocks in the process. Afterwards, it returns the number of bytes actually written. If the given inumber is invalid, or any other error is encountered, return -1 .
// Note: the number of bytes actually written could be smaller than the number of bytes request, perhaps if the disk becomes full.
size_t writeInode(size_t inumber, char *data, size_t length, size_t offset) {


//    // Load inode
//    Inode inode;
//    if(!load_inode(inumber, &inode) || offset > inode.Size) return -1;
//
//    size_t MAX_FILE_SIZE = BLOCK_SIZE * (POINTERS_PER_INODE + POINTERS_PER_BLOCK);
//
//    //Adjust Length
//    if (length > MAX_FILE_SIZE - offset) {
//        length = MAX_FILE_SIZE - offset;
//    }
//    uint32_t startBlock = offset / BLOCK_SIZE;
//    Block indirect;
//
//    bool readIndirect = false;
//    bool modifiedInode = false;
//    bool modifiedIndirect = false;
//
//    // Write block and copy to data
//    size_t written = 0;
//    for (uint32_t block = startBlock; written < length && block < POINTERS_PER_INODE + POINTERS_PER_BLOCK; block++) {
//        size_t blockToWrite;
//        if(block < POINTERS_PER_INODE){
//            //Direct Block
//            //Allocate block if necessary
//            if(inode.Direct[block] == 0){
//                ssize_t allocated_block = allocate_free_block();
//                if(allocated_block == -1) break;
//                inode.Direct[block] = allocated_block;
//                modifiedInode = true;
//            }
//            blockToWrite = inode.Direct[block];
//        } else {
//            //Indirect Block
//
//            //Allocate Indirect block if necessary
//            if(inode.Indirect == 0){
//                ssize_t allocatedBlock = allocate_free_block();
//                if(allocatedBlock == -1) return written;
//                inode.Indirect = allocatedBlock;
//                modifiedIndirect = true;
//            }
//
//            //Read indirect block if it hasn't been read yet
//            if(!readIndirect){
//                mounted_disk->readDisk(mounted_disk, inode.Indirect, indirect.Data);
//                readIndirect = true;
//            }
//
//            //Allocate block if necessary
//            if(indirect.Pointers[block - POINTERS_PER_INODE] == 0){
//                ssize_t allocatedBlock = allocate_free_block();
//                if(allocatedBlock == -1) break;
//                indirect.Pointers[block - POINTERS_PER_INODE] = allocatedBlock;
//                modifiedIndirect = true;
//            }
//            blockToWrite = indirect.Pointers[block - POINTERS_PER_INODE];
//        }
//
//        //Get the block (either direct or indirect)
//        size_t writeOffset;
//        size_t writeLength;
//
//        // if it's the first block written, have to start from an offset
//        // and write either until the end of the block, or the whole request
//        if(written == 0){
//            writeOffset = offset % BLOCK_SIZE;
//            if (length < BLOCK_SIZE - writeOffset) {
//                writeLength = length;
//            } else {
//                writeLength = BLOCK_SIZE - writeOffset;
//            }
////            writeLength = min(BLOCK_SIZE - writeOffset, length);
//        } else {
//            // otherwise, start from the beginning, and write
//            // either the whole block or the rest of the request
//            writeOffset = 0;
//            if (written + BLOCK_SIZE > length) {
//                writeLength = length - written;
//            } else {
//                writeLength = BLOCK_SIZE;
//            }
//        }
//
//        char writeBuffer[BLOCK_SIZE];
//
//        // if we're not writing the whole block, need to copy what's there
//        if(writeLength < BLOCK_SIZE)
//            mounted_disk->readDisk(mounted_disk, blockToWrite, writeBuffer);
//
//        //Copy into buffer
//        memcpy(writeBuffer + writeOffset, data + written, writeLength);
//        mounted_disk->writeDisk(mounted_disk, blockToWrite, (char *) writeBuffer);
//        written += writeLength;
//    }
//
//    //Update Inode size if Inode was modified
//    uint32_t newSize;
//    if (inode.Size > written + offset) {
//        newSize = inode.Size;
//    } else {
//        newSize = written + offset;
//    }
//
//    if(newSize != inode.Size){
//        inode.Size = newSize;
//        modifiedInode = true;
//    }
//
//    //Save modifications on indirect and inode, if any
//    if(modifiedInode) save_inode(inumber, &inode);
//    if(modifiedIndirect) mounted_disk->writeDisk(mounted_disk, inode.Indirect, indirect.Data);
//
//    return written;

//     Load inode information
    Inode inode;
    if (!load_inode(inumber, &inode)) {
        return -1;
    }
//    if (inode.Valid == 0) {
//        return -1;
//    }
    if (offset > inode.Size) {
        return -1;
    }

    // Adjust length
//    size_t MAX_SIZE = (POINTERS_PER_INODE + POINTERS_PER_BLOCK) * BLOCK_SIZE; // 4,214,784
    size_t MAX_SIZE = (Blocks - InodeBlocks - 1) * BLOCK_SIZE; // 69,632 for 20 blocks
    // length 27160
    if (offset + length > MAX_SIZE) {
        length = MAX_SIZE - offset;
    }

    size_t startBlock = offset / BLOCK_SIZE; // 0
    size_t endBlock = (offset + length) / BLOCK_SIZE; // 6

    size_t bytes_written = 0;
    Block indirect;


    // Direct blocks
    for (size_t i = startBlock; i < POINTERS_PER_INODE /* 5 */ && bytes_written < length; i++) { // length 27160
        Block block;

        if (inode.Direct[i] == 0) {
            ssize_t allocated_block = allocate_free_block();
            if (allocated_block == -1) {
                // Update inode size
                if (offset + bytes_written > inode.Size) {
                    inode.Size = offset + length;

                }

                if (!save_inode(inumber, &inode)) {
                    return -1;
                }

                return bytes_written;
            }

            inode.Direct[i] = allocated_block;

        }

        mounted_disk->readDisk(mounted_disk, inode.Direct[i], block.Data);

        size_t start = 0;
        size_t end = BLOCK_SIZE;
        if (i == startBlock) {
            start = offset % BLOCK_SIZE; // 0
        } else if (i == endBlock) {
            end = (offset + length) % BLOCK_SIZE;
        }

        for (size_t j = start; j < end; j++) {
            block.Data[j] = data[bytes_written];
            bytes_written++;
        }

        // i = 0 ; bytes_written = 4096


        mounted_disk->writeDisk(mounted_disk, inode.Direct[i], block.Data);

    }


    // Indirect blocks
    if (endBlock >= POINTERS_PER_INODE) { // 6 >= 5
        if (inode.Indirect == 0) {
            ssize_t allocated_block = allocate_free_block();

            if (allocated_block == -1) {
                // Update inode size
                if (offset + bytes_written > inode.Size) {
                    inode.Size = (size_t)offset + length;

                }

                if (!save_inode(inumber, &inode)) {
                    return -1;
                }

                return bytes_written;
            }
            inode.Indirect = allocated_block;
        }

        mounted_disk->readDisk(mounted_disk, inode.Indirect, indirect.Data);

        for (size_t i = 0; i < POINTERS_PER_BLOCK && bytes_written < length; i++) { // 20,480 of 27,160 2: 24,576 of 27,160
            Block block;

            if (indirect.Pointers[i] == 0) {
                ssize_t allocated_block = allocate_free_block();

                if (allocated_block == -1) {
                    // Update inode size
                    if (offset + bytes_written > inode.Size) {
                        inode.Size = (size_t)offset + length;
                    }

                    if (!save_inode(inumber, &inode)) {
                        return -1;
                    }

                    return bytes_written;
                }
                indirect.Pointers[i] = allocated_block;
            }

            mounted_disk->readDisk(mounted_disk, indirect.Pointers[i], block.Data);

            size_t start = 0;
            size_t end = BLOCK_SIZE;
            if (i == endBlock - POINTERS_PER_INODE) {
                end = (offset + length) % BLOCK_SIZE;
            }

            for (size_t j = start; j < end; j++) {
                block.Data[j] = data[bytes_written];
                bytes_written++;
            }

            mounted_disk->writeDisk(mounted_disk, indirect.Pointers[i], block.Data);

        }

        mounted_disk->writeDisk(mounted_disk, inode.Indirect, indirect.Data);
    }

    // Update inode size
    if (offset + bytes_written > inode.Size) {
        inode.Size = (size_t)offset + length;

    }

    if (!save_inode(inumber, &inode)) {
        return -1;
    }

    return bytes_written;

//    // Load inode information
//    Inode inode;
//    if (!load_inode(inumber, &inode)) {
//        return -1;
//    }
//    if (inode.Valid == 0) {
//        return -1;
//    }
//    if (offset > inode.Size) {
//        return -1;
//    }
//
//    // Adjust length
//    size_t MAX_SIZE = POINTERS_PER_INODE * BLOCK_SIZE + POINTERS_PER_BLOCK * BLOCK_SIZE;
//    if (offset + length > MAX_SIZE) {
//        length = MAX_SIZE - offset;
//    }
//
//    size_t startBlock = offset / BLOCK_SIZE;
//    size_t endBlock = (offset + length) / BLOCK_SIZE;
//
//    size_t bytes_written = 0;
//    Block indirect;
//
//    // Only read indirect block if needed
//    if (endBlock >= POINTERS_PER_INODE) {
//        mounted_disk->readDisk(mounted_disk, inode.Indirect, indirect.Data);
//
//    }
//
//    for (size_t i = startBlock; i < endBlock + 1 && bytes_written < length; i++) {
//        Block block;
//        if (i < POINTERS_PER_INODE) {
//            if (inode.Direct[i] == 0) {
//                inode.Direct[i] = allocate_free_block();
//                if (inode.Direct[i] <= 0) {
//                    break;
//                }
//
//            }
//            mounted_disk->readDisk(mounted_disk, inode.Direct[i], block.Data);
//        } else {
//            if (inode.Indirect == 0 && i == POINTERS_PER_INODE) {
//                inode.Indirect = allocate_free_block();
//                if (inode.Indirect <= 0) {
//                    break;
//                }
//            }
//            if (indirect.Pointers[i - POINTERS_PER_INODE] == 0) {
//                indirect.Pointers[i - POINTERS_PER_INODE] = allocate_free_block();
//                if (indirect.Pointers[i - POINTERS_PER_INODE] <= 0) {
//                    break;
//                }
//            }
//            mounted_disk->readDisk(mounted_disk, indirect.Pointers[i - POINTERS_PER_INODE], block.Data);
//        }
//
//        size_t start = 0;
//        size_t end = BLOCK_SIZE;
//        if (i == startBlock) {
//            start = offset % BLOCK_SIZE;
//        }
//        if (i == (offset + length) / BLOCK_SIZE) {
//            end = (offset + length) % BLOCK_SIZE;
//        }
//
//        for (size_t j = start; j < end; j++) {
//            block.Data[j] = data[bytes_written];
//            bytes_written++;
//        }
//
//        if (i < POINTERS_PER_INODE) {
//            mounted_disk->writeDisk(mounted_disk, inode.Direct[i], block.Data);
//        } else {
//            mounted_disk->writeDisk(mounted_disk, indirect.Pointers[i - POINTERS_PER_INODE], block.Data);
//        }
//    }
//
//    // Update inode size
//    if (offset + bytes_written > inode.Size) {
//        inode.Size = offset + bytes_written;
//    }
//
//    // Save inode
//    if (!save_inode(inumber, &inode)) {
//        return -1;
//    }
//
//
//    return bytes_written;
}

// Helper functions ---------------------------------------------------------

ssize_t allocate_free_block() {
    int block = -1;

    // Find a free block
    for (size_t i = 0; i < Blocks; i++) {
        if (free_block_bitmap[i]) {
            block = i;
            free_block_bitmap[i] = false;
            break;
        }
    }

    // Write free_block_bitmap to disk
    if (block != -1) {
        Block zeroBlock;
        memset(zeroBlock.Data, 0, BLOCK_SIZE);
        mounted_disk->writeDisk(mounted_disk, block, zeroBlock.Data);
    }

    return block;
}

bool save_inode(size_t inumber, Inode *inode) {

    if (inumber >= Inodes) {
        return false;
    }

    // Find the block that contains the inode
    size_t block = inumber / INODES_PER_BLOCK + 1;

    // Read the block
    Block blockData;
    mounted_disk->readDisk(mounted_disk, block, blockData.Data);

    // Copy the inode into the block
    memcpy(blockData.Inodes + (inumber % INODES_PER_BLOCK), inode, sizeof(Inode));

    // Write the block back to disk
    mounted_disk->writeDisk(mounted_disk, block, blockData.Data);

    return true;
}

bool load_inode(size_t inumber, Inode *inode) {

    // Check if inumber is valid
    if (inumber >= Inodes) {
        return false;
    }

    // Load inode
    Block block;
    mounted_disk->readDisk(mounted_disk, (inumber / INODES_PER_BLOCK) + 1, block.Data);
    *inode = block.Inodes[inumber % INODES_PER_BLOCK];

    return true;

}