#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>

#define stat xv6_stat  // avoid clash with host struct stat
#include "types.h"
#include "fs.h"
#include "stat.h"
#include "param.h"

#include "mbr.h"	//gal: including the header and struct of master boot record


#ifndef static_assert
#define static_assert(a, b) do { switch (0) case 0: case (a): ; } while (0)
#endif

#define NINODES 200

// Disk layout:
// [ boot block | sb block | log | inode blocks | free bit map | data blocks ]

int nbitmap = PART_SIZE/(BSIZE*8) + 1;
int ninodeblocks = NINODES / IPB + 1;
int nlog = LOGSIZE;  
int nmeta;    // Number of meta blocks (boot, sb, nlog, inode, bitmap)
int nblocks;  // Number of data blocks

int partitionOffsets[4];	//gal: block 0 is MBR so block 1(super block) is partition 0 beginning ==> the offest is 1

int fsfd;
struct superblock sb;
char zeroes[BSIZE];
uint freeinode = 1;
uint freeblock;


void balloc(int);
void wsect(uint, void*);
void winode(uint, struct dinode*);
void rinode(uint inum, struct dinode *ip);
void rsect(uint sec, void *buf);
uint ialloc(ushort type);
void iappend(uint inum, void *p, int n);

// convert to intel byte order
ushort
xshort(ushort x)
{
  ushort y;
  uchar *a = (uchar*)&y;
  a[0] = x;
  a[1] = x >> 8;
  return y;
}

uint
xint(uint x)
{
  uint y;
  uchar *a = (uchar*)&y;
  a[0] = x;
  a[1] = x >> 8;
  a[2] = x >> 16;
  a[3] = x >> 24;
  return y;
}

int
main(int argc, char *argv[])
{
  int i, cc, fd, bootblockFD;
  uint rootino, inum, off;
  struct dirent de;
  char buf[BSIZE];
  struct dinode din;
  
  for (i = 0; i < 4; i++)//set the partitions offsets for future use
  {
    partitionOffsets[i] = 1 + (PART_SIZE * i);
  }

  static_assert(sizeof(int) == 4, "Integers must be 4 bytes!");

  if(argc < 4){
    fprintf(stderr, "Usage: mkfs fs.img files...\n");
    exit(1);
  }

  assert((BSIZE % sizeof(struct dinode)) == 0);
  assert((BSIZE % sizeof(struct dirent)) == 0);

  fsfd = open(argv[1], O_RDWR|O_CREAT|O_TRUNC, 0666);
  if(fsfd < 0){
    perror(argv[1]);
    exit(1);
  }

    if (strcmp(argv[2], "bootblock") != 0)
  {
    fprintf(stderr, "Usage: mkfs fs.img bootblock kernel files...\n");
    exit(1);
  }

  
  // 1 fs block = 1 disk sector
  nmeta = 1 + nlog + ninodeblocks + nbitmap;		//gal: partition offset + 1 block for super block
  nblocks = PART_SIZE - nmeta;
  
  sb.size = xint(PART_SIZE);
  sb.nblocks = xint(nblocks);
  sb.ninodes = xint(NINODES);
  sb.nlog = xint(nlog);
  sb.logstart = xint(1);	//gal: log starts from the partition offest + 1 block for super block
  sb.inodestart = xint(1 + nlog);
  sb.bmapstart = xint(1 + nlog + ninodeblocks);
  sb.partitionNumber = 0;	//the root partition is partition 0
  
  printf("nmeta %d (boot, super, log blocks %u inode blocks %u, bitmap blocks %u) blocks %d total %d\n",
         nmeta, nlog, ninodeblocks, nbitmap, nblocks, PART_SIZE);

  freeblock = nmeta;     // the first free block that we can allocate

  for(i = -1; i < FSSIZE - 1; i++)
    wsect(i, zeroes);
  
    bootblockFD = open(argv[2], O_RDWR);
  if (bootblockFD < 0)
  {
    perror(argv[2]);
    exit(1);
  }

  
  memset(buf, 0, sizeof(buf));					//gal: nullify buf: a buffer the size of a block allocated on the stack
  struct mbr newMbr;						//allocate mbr struct on the stack
  memset(newMbr.bootstrap, 0, BOOTSTRAP);	//nullify the bootstrap TODO consider removal, for some reason when this was allocated it was filled with junk
    if (read(bootblockFD, &newMbr, BOOTSTRAP) < BOOTSTRAP)
  {
    perror(argv[2]);
    exit(1);
  }

  close(bootblockFD);
  
  for (i = 0; i < 4; i++)
  {
      newMbr.partitions[i].type = FS_INODE;
      newMbr.partitions[i].offset = partitionOffsets[i];				//partition 0 starts from block 1
      newMbr.partitions[i].size = PART_SIZE;			//partition 0 size is FSSIZE = 1000 block
      newMbr.partitions[i].flags = 0;
  }
  newMbr.partitions[0].flags = PART_ALLOCATED | PART_BOOTABLE; //set partition 0 to be bootable and allocated. 
  newMbr.magic[0] = 0x55;
  newMbr.magic[1] = 0xAA;
  
  memmove(buf, &newMbr, sizeof(newMbr));		//copy the super block into the allocated buffer
  wsect(-1, buf);								//write the buffer(mbr) into the first block

  memset(buf, 0, sizeof(buf));		//gal: nullify buf: a buffer the size of a block allocated on the stack
  memmove(buf, &sb, sizeof(sb));	//copy the super block into the allocated buffer
  wsect(0, buf);					//write the buffer(super block) into the first block

  rootino = ialloc(T_DIR);
  assert(rootino == ROOTINO);//shold pass for every partition, the inode num is relative to the partition

  bzero(&de, sizeof(de));
  de.inum = xshort(rootino);
  strcpy(de.name, ".");
  iappend(rootino, &de, sizeof(de));

  bzero(&de, sizeof(de));
  de.inum = xshort(rootino);
  strcpy(de.name, "..");
  iappend(rootino, &de, sizeof(de));

  for(i = 4; i < argc; i++){
    assert(index(argv[i], '/') == 0);

    if((fd = open(argv[i], 0)) < 0){
      perror(argv[i]);
      exit(1);
    }
    
    // Skip leading _ in name when writing to file system.
    // The binaries are named _rm, _cat, etc. to keep the
    // build operating system from trying to execute them
    // in place of system binaries like rm and cat.
    if(argv[i][0] == '_')
      ++argv[i];

    inum = ialloc(T_FILE);

    bzero(&de, sizeof(de));
    de.inum = xshort(inum);
    strncpy(de.name, argv[i], DIRSIZ);
    iappend(rootino, &de, sizeof(de));

    while((cc = read(fd, buf, sizeof(buf))) > 0)
      iappend(inum, buf, cc);

    close(fd);
  }

  // fix size of root inode dir
  rinode(rootino, &din);
  off = xint(din.size);
  off = ((off/BSIZE) + 1) * BSIZE;
  din.size = xint(off);
  winode(rootino, &din);

  balloc(freeblock);

  exit(0);
}

void
wsect(uint sec, void *buf)
{
  sec = sec + partitionOffsets[sb.partitionNumber];
  if(lseek(fsfd, sec * BSIZE, 0) != sec * BSIZE){
    perror("lseek");
    exit(1);
  }
  if(write(fsfd, buf, BSIZE) != BSIZE){
    perror("write");
    exit(1);
  }
}

void
winode(uint inum, struct dinode *ip)
{
  char buf[BSIZE];
  uint bn;
  struct dinode *dip;

  bn = IBLOCK(inum, sb);
  rsect(bn, buf);
  dip = ((struct dinode*)buf) + (inum % IPB);
  *dip = *ip;
  wsect(bn, buf);
}

void
rinode(uint inum, struct dinode *ip)
{
  char buf[BSIZE];
  uint bn;
  struct dinode *dip;

  bn = IBLOCK(inum, sb);
  rsect(bn, buf);
  dip = ((struct dinode*)buf) + (inum % IPB);
  *ip = *dip;
}

void
rsect(uint sec, void *buf)
{
  sec = sec + partitionOffsets[sb.partitionNumber];
  if(lseek(fsfd, sec * BSIZE, 0) != sec * BSIZE){
    perror("lseek");
    exit(1);
  }
  if(read(fsfd, buf, BSIZE) != BSIZE){
    perror("read");
    exit(1);
  }
}

uint
ialloc(ushort type)
{
  uint inum = freeinode++;
  struct dinode din;

  bzero(&din, sizeof(din));
  din.type = xshort(type);
  din.nlink = xshort(1);
  din.size = xint(0);
  winode(inum, &din);
  return inum;
}

void
balloc(int used)
{
  uchar buf[BSIZE];
  int i;

  printf("balloc: first %d blocks have been allocated\n", used);
  assert(used < BSIZE*8);
  bzero(buf, BSIZE);
  for(i = 0; i < used; i++){
    buf[i/8] = buf[i/8] | (0x1 << (i%8));
  }
  printf("balloc: write bitmap block at sector %d\n", sb.bmapstart);
  wsect(sb.bmapstart, buf);
}

#define min(a, b) ((a) < (b) ? (a) : (b))

void
iappend(uint inum, void *xp, int n)
{
  char *p = (char*)xp;
  uint fbn, off, n1;
  struct dinode din;
  char buf[BSIZE];
  uint indirect[NINDIRECT];
  uint x;

  rinode(inum, &din);
  off = xint(din.size);
  // printf("append inum %d at off %d sz %d\n", inum, off, n);
  while(n > 0){
    fbn = off / BSIZE;
    assert(fbn < MAXFILE);
    if(fbn < NDIRECT){
      if(xint(din.addrs[fbn]) == 0){
        din.addrs[fbn] = xint(freeblock++);
      }
      x = xint(din.addrs[fbn]);
    } else {
      if(xint(din.addrs[NDIRECT]) == 0){
        din.addrs[NDIRECT] = xint(freeblock++);
      }
      rsect(xint(din.addrs[NDIRECT]), (char*)indirect);
      if(indirect[fbn - NDIRECT] == 0){
        indirect[fbn - NDIRECT] = xint(freeblock++);
        wsect(xint(din.addrs[NDIRECT]), (char*)indirect);
      }
      x = xint(indirect[fbn-NDIRECT]);
    }
    n1 = min(n, (fbn + 1) * BSIZE - off);
    rsect(x, buf);
    bcopy(p, buf + off - (fbn * BSIZE), n1);
    wsect(x, buf);
    n -= n1;
    off += n1;
    p += n1;
  }
  din.size = xint(off);
  winode(inum, &din);
}
