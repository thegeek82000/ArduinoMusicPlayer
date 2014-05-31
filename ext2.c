#include "ext2.h"
#include "globals.h"
#include "SdReader.h"

#define IN_LEN 256
#define DIN_LEN 65536
#define TIN_LEN 16777216

static struct ext2_inode currentInode;
static uint32_t currentInodeNum = 0;

static char name[256];
static uint32_t filePos;

uint32_t getIndirect(uint32_t address, uint32_t index) {
   address *= 1024;
   address += index * 4;
   sdReadData(address / 512, address % 512, (void *) &address, 4);
   return address;
}

uint32_t getDIndirect(uint32_t address, uint32_t index) {
   address *= 1024;
   address += (index / 256) * 4;
   sdReadData(address / 512, address % 512, (void *) &address, 4);
   return getIndirect(address, index % 256);
}

uint32_t getTIndirect(uint32_t address, uint32_t index) {
   address *= 1024;
   address += (index / (DIN_LEN)) * 4;
   sdReadData(address / 512, address % 512, (void *) &address, 4);
   return getDIndirect(address, index % (DIN_LEN));
}

void getBlockData(uint32_t offset, void *data, uint16_t size) {
   if ((offset % 1024) + size > 1024) {
      uint16_t pre = 1024 - (offset % 1024);
      getBlockData(offset, data, pre);
      getBlockData(offset + pre, (void *) (((char *) data) + pre), size - pre);
      return;
   }

   uint32_t index = offset / 1024;
   uint32_t blockAddr;

   if (index < EXT2_NDIR_BLOCKS) {
      blockAddr = currentInode.i_block[index];
   } else {
      index -= EXT2_NDIR_BLOCKS;
      if (index < 256) {
         blockAddr = getIndirect(currentInode.i_block[EXT2_IND_BLOCK], index);
      } else {
         index -= 256;
         if (index < DIN_LEN) {
            blockAddr = getDIndirect(currentInode.i_block[EXT2_DIND_BLOCK], index);
         } else {
            index -= DIN_LEN;
            if (index < TIN_LEN) {
               blockAddr = getTIndirect(currentInode.i_block[EXT2_TIND_BLOCK], index);
            } else {
               fprintf(stderr, "ERROR: too large of offset\n");
               exit(0);
            }
         }
      }
   }

   blockAddr *= 1024;
   blockAddr += offset % 1024;
   if ((blockAddr % 512) + size > 512) {
      uint16_t pre = 512 - (blockAddr % 512);
      sdReadData(blockAddr / 512, blockAddr % 512, data, pre);
      sdReadData((blockAddr / 512) + 1, 0, (void *)(((char *) data) + pre), size - pre);
   } else {
      sdReadData(blockAddr / 512, blockAddr % 512, data, size);
   }
}

void getInode(uint32_t inode) {
   if (inode == currentInodeNum)
      return;

   uint32_t inodesPerGroup;
   sdReadData(2, 40, (void *) &inodesPerGroup, 4);

   uint32_t group = (inode - 1) / inodesPerGroup;
   uint32_t address = 1024 * (8192 * group + 5) + 128 * ((inode - 1) % inodesPerGroup);

   sdReadData(address / 512, address % 512, (void *) &currentInode, 128);

   currentInodeNum = inode;
}

uint8_t inodeIsFile(uint32_t inode) {
   getInode(inode);
   return currentInode.i_mode >> 12 == 8;
}

void getFile(uint8_t ndx) {
   getInode(EXT2_ROOT_INO);

   uint32_t offset = 0, nextInode;
   uint16_t recLen, nameLen;

   while (offset < currentInode.i_size) {
      getBlockData(offset, &nextInode, 4);
      getBlockData(offset + 4, &recLen, 2);

      if (inodeIsFile(nextInode)) {
         if (ndx-- == 0) {
            getBlockData(offset + 6, &nameLen, 2);
            getBlockData(offset + 8, name, nameLen);
            name[nameLen] = 0;
            filePos = WAV_HEADER;
            return;
         }
      }

      getInode(EXT2_ROOT_INO);
      offset += recLen;
   }
}

char *getCurrentName() {
   return name;
}

uint32_t getCurrentPos() {
   return filePos;
}

uint32_t getCurrentSize() {
   return currentInode.i_size;
}

void getFileChunk(uint8_t *buffer) {
   uint16_t read = 256;
   if (filePos + read > currentInode.i_size) {
      read = currentInode.i_size - filePos;
   }

   getBlockData(filePos, buffer, read);

   filePos = read == 256 ? (filePos + read) : WAV_HEADER;
}

uint8_t getNumFiles() {
   getInode(EXT2_ROOT_INO);

   uint32_t offset = 0, nextInode;
   uint16_t recLen;
   uint8_t numFiles = 0;

   while (offset < currentInode.i_size) {
      getBlockData(offset, &nextInode, 4);
      getBlockData(offset + 4, &recLen, 2);

      if (inodeIsFile(nextInode))
         numFiles++;

      getInode(EXT2_ROOT_INO);
      offset += recLen;
   }

   return numFiles;
}

void ext2_init() {
   memset(name, 0, 256);
}

