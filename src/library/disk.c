// disk.cpp: disk emulator

#include "sfs/disk.h"
#include "sfs/fs.h"

// #include <stdexcept>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include<signal.h>

char signal_msg[256];
void handle_sigint(int sig) 
{ 
    printf("%s\n",signal_msg); 
    exit(0);
}

void openDisk(struct Disk * self,const char *path, size_t nblocks) {
    self->FileDescriptor = open(path, O_RDWR|O_CREAT, 0600);
    if (self->FileDescriptor < 0) {
    	char what[BUFSIZ];
    	snprintf(what, BUFSIZ, "Unable to open %s: %s", path, strerror(errno));
        strcpy(signal_msg,"ERROR: unable to open the disk.\0");
        signal(SIGINT, handle_sigint); 
        raise(SIGINT);
    }

    if (ftruncate(self->FileDescriptor, nblocks*BLOCK_SIZE) < 0) {
    	char what[BUFSIZ];
    	snprintf(what, BUFSIZ, "Unable to open %s: %s", path, strerror(errno));
        strcpy(signal_msg,"ERROR: unable to open the disk.\0");
        signal(SIGINT, handle_sigint); 
        raise(SIGINT);
    }

    self->Blocks = nblocks;
    self->Reads  = 0;
    self->Writes = 0;
}

void DiskDestructor(struct Disk * self) {
    if (self->FileDescriptor > 0) {
    	printf("%d disk block reads\n", self->Reads);
    	printf("%d disk block writes\n", self->Writes);
        close(self->FileDescriptor);
    	self->FileDescriptor = 0;
    }
}

void sanity_check(struct Disk * self,int blocknum, char *data) {
    char what[BUFSIZ];

    if (blocknum < 0) {
    	snprintf(what, BUFSIZ, "blocknum (%d) is negative!", blocknum);
        strcpy(signal_msg,"ERROR: blocknum is negative.\0");
        signal(SIGINT, handle_sigint); 
        raise(SIGINT);
    }

    if (blocknum >= (int)self->Blocks) {
    	snprintf(what, BUFSIZ, "blocknum (%d) is too big!", blocknum);
        strcpy(signal_msg,"ERROR: blocknum is too big!.\0");
        signal(SIGINT, handle_sigint); 
        raise(SIGINT);
    }

    if (data == NULL) {
    	snprintf(what, BUFSIZ, "null data pointer!");
        strcpy(signal_msg,"ERROR: null data pointer.\0");
        signal(SIGINT, handle_sigint);
        raise(SIGINT);
    }
}

void readDisk(struct Disk * self,int blocknum, char *data) {
    sanity_check(self,blocknum, data);

    if (lseek(self->FileDescriptor, blocknum*BLOCK_SIZE, SEEK_SET) < 0) {
    	char what[BUFSIZ];
    	snprintf(what, BUFSIZ, "Unable to lseek %d: %s", blocknum, strerror(errno));
        strcpy(signal_msg,"ERROR: unable to lseek blocknum.\0");
        signal(SIGINT, handle_sigint); 
    }

    if (read(self->FileDescriptor, data, BLOCK_SIZE) != BLOCK_SIZE) {
    	char what[BUFSIZ];
    	snprintf(what, BUFSIZ, "Unable to read %d: %s", blocknum, strerror(errno));
        strcpy(signal_msg,"ERROR: unable to read blocknum.\0");
        signal(SIGINT, handle_sigint); 
        raise(SIGINT);
    }

    self->Reads=self->Reads+1;
}

void writeDisk(struct Disk * self,int blocknum, char *data) {
    sanity_check(self,blocknum, data);

    if (lseek(self->FileDescriptor, blocknum*BLOCK_SIZE, SEEK_SET) < 0) {
    	char what[BUFSIZ];
    	snprintf(what, BUFSIZ, "Unable to lseek %d: %s", blocknum, strerror(errno));
    	strcpy(signal_msg,"ERROR: unable to lseek blocknum.\0");
        signal(SIGINT, handle_sigint); 
        raise(SIGINT);
    }

    if (write(self->FileDescriptor, data, BLOCK_SIZE) != BLOCK_SIZE) {
    	char what[BUFSIZ];
    	snprintf(what, BUFSIZ, "Unable to write %d: %s", blocknum, strerror(errno));
    	strcpy(signal_msg,"ERROR: unable to write blocknum.\0");
        signal(SIGINT, handle_sigint); 
        raise(SIGINT);
    }

    self->Writes=self->Writes+1;
}


size_t size(struct Disk * self){
    return  self->Blocks;
}
    
bool mountedDisk(struct Disk * self){
    return self->Mounts > 0;
}

void mountDisk(struct Disk * self){
    self->Mounts=self->Mounts+1;
}

void unmountDisk(struct Disk * self){
     if (self->Mounts > 0) self->Mounts=self->Mounts-1;
}
