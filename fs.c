// File system implementation.  Five layers:
//   + Blocks: allocator for raw disk blocks.
//   + Log: crash recovery for multi-step updates.
//   + Files: inode allocator, reading, writing, metadata.
//   + Directories: inode with special contents (list of other inodes!)
//   + Names: paths like /usr/rtm/xv6/fs.c for convenient naming.
//
// This file contains the low-level file system manipulation 
// routines.  The (higher-level) system call implementations
// are in sysfile.c.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"
#include "fs.h"
#include "buf.h"
#include "file.h"

#include "mbr.h"	//gal: load mbr header for sturct mbr that is used in readmbr()

#define min(a, b) ((a) < (b) ? (a) : (b))
static void itrunc(struct inode*);
struct superblock sb;   	// there should be one per dev, but we run with one dev
struct mbr  loadedMbr;		//points the mbr
int partitionBootNum = -1;


struct mbr *//loads the MBR from ROOTDEV
loadMbrFromDisk(void)
{
  struct buf *buffer = bread(ROOTDEV, 0);
  memmove((void*)&loadedMbr, buffer->data, sizeof(struct mbr));
  brelse(buffer);
  return &loadedMbr;
}

struct mbr *//loads the MBR and prints the partitions details
readmbr(void)
{
  int i;
  char* bootable;
  char* type;

  struct mbr * mbrPtr = loadMbrFromDisk();
  for (i = 0; i < 4; i++)
  {
    if (mbrPtr->partitions[i].flags & PART_BOOTABLE)
      bootable = "YES";
    else
      bootable = "NO";
    
    if (mbrPtr->partitions[i].type == FS_INODE)
      type = "INODE";
    else if (mbrPtr->partitions[i].type == FS_FAT)
      type = "FAT";
    else
      type = "unknown type";
    cprintf("Partition %d: bootable %s, type %s, offset %d, size %d\n", 
	    i, 
	    bootable, 
	    type, 
	    (uint)mbrPtr->partitions[i].offset, 
	    (uint)mbrPtr->partitions[i].size);
  }
  return mbrPtr;
}

int
getRootPartitionNum(void)
{
  int partitionIndex;
  if (partitionBootNum == -1)
  {
    struct mbr * loadedMbr = loadMbrFromDisk();
    for (partitionIndex = 0; partitionIndex < 4; partitionIndex++)
    {//TODO maybe we should check what partition is bootable
      if (loadedMbr->partitions[partitionIndex].flags &  PART_BOOTABLE)
      {
	partitionBootNum=partitionIndex;
	break;
      }
    }
  }
  return partitionBootNum;
}

// Read the super block.
void
readsb(int dev, struct superblock *sb, int partitionNum)
{
  struct buf *bp;
  int superBlockLocation = loadedMbr.partitions[partitionNum].offset;
  bp = bread(dev, superBlockLocation);
  memmove(sb, bp->data, sizeof(*sb));
  sb->partitionNumber = partitionNum;
  brelse(bp);
}

// Zero a block.
static void
bzero(int dev, int bno, int partitionNumber)
{
  struct buf *bp;
  APPLY_P_OFFSET(bno, partitionNumber);//shift the bno according to the root directory partition
  bp = bread(dev, bno);
  memset(bp->data, 0, BSIZE);
  log_write(bp);
  brelse(bp);
}

// Blocks. 

// Allocate a zeroed disk block.
static uint//returns a relative block number
balloc(uint dev, int partitionNumber)
{
  int b, bi, m;
  uint bno;
  struct buf *bp;

  bp = 0;
  readsb(dev, &sb, partitionNumber);
  for(b = 0; b < sb.size; b += BPB){//BPB=512 bytes=512*8 bits = it means each block in block map represent 512*8(BPB) blocks of data 
    bno = BBLOCK(b, sb);
    APPLY_P_OFFSET(bno, partitionNumber);
    bp = bread(dev, bno);
    for(bi = 0 ; bi < BPB && b + bi < sb.size; bi++){ //iterate on BPB block. bi represents a data block 
      m = 1 << (bi % 8);
      if((bp->data[bi/8] & m) == 0){  // Is block free?
        bp->data[bi/8] |= m;  // Mark block in use.
        log_write(bp);
        brelse(bp);
	bno = b + bi;
        bzero(dev, bno, partitionNumber);
        return b + bi;
      }
    }
    brelse(bp);
  }
  panic("balloc: out of blocks");
}

// Free a disk block.
static void
bfree(int dev, uint b, int partitionNum)
{
  struct buf *bp;
  uint bno;
  int bi, m;

  readsb(dev, &sb, partitionNum);
  
  bno = BBLOCK(b, sb); //b is realtive, sb is absoloute
  APPLY_P_OFFSET(bno, partitionNum);
  
  bp = bread(dev, bno);
  bi = b % BPB;
  m = 1 << (bi % 8);
  if((bp->data[bi/8] & m) == 0)
    panic("freeing free block");
  bp->data[bi/8] &= ~m;
  log_write(bp);
  brelse(bp);
}

// Inodes.
//
// An inode describes a single unnamed file.
// The inode disk structure holds metadata: the file's type,
// its size, the number of links referring to it, and the
// list of blocks holding the file's content.
//
// The inodes are laid out sequentially on disk at
// sb.startinode. Each inode has a number, indicating its
// position on the disk.
//
// The kernel keeps a cache of in-use inodes in memory
// to provide a place for synchronizing access
// to inodes used by multiple processes. The cached
// inodes include book-keeping information that is
// not stored on disk: ip->ref and ip->flags.
//
// An inode and its in-memory represtative go through a
// sequence of states before they can be used by the
// rest of the file system code.
//
// * Allocation: an inode is allocated if its type (on disk)
//   is non-zero. ialloc() allocates, iput() frees if
//   the link count has fallen to zero.
//
// * Referencing in cache: an entry in the inode cache
//   is free if ip->ref is zero. Otherwise ip->ref tracks
//   the number of in-memory pointers to the entry (open
//   files and current directories). iget() to find or
//   create a cache entry and increment its ref, iput()
//   to decrement ref.
//
// * Valid: the information (type, size, &c) in an inode
//   cache entry is only correct when the I_VALID bit
//   is set in ip->flags. ilock() reads the inode from
//   the disk and sets I_VALID, while iput() clears
//   I_VALID if ip->ref has fallen to zero.
//
// * Locked: file system code may only examine and modify
//   the information in an inode and its content if it
//   has first locked the inode. The I_BUSY flag indicates
//   that the inode is locked. ilock() sets I_BUSY,
//   while iunlock clears it.
//
// Thus a typical sequence is:
//   ip = iget(dev, inum)
//   ilock(ip)
//   ... examine and modify ip->xxx ...
//   iunlock(ip)
//   iput(ip)
//
// ilock() is separate from iget() so that system calls can
// get a long-term reference to an inode (as for an open file)
// and only lock it for short periods (e.g., in read()).
// The separation also helps avoid deadlock and races during
// pathname lookup. iget() increments ip->ref so that the inode
// stays cached and pointers to it remain valid.
//
// Many internal file system functions expect the caller to
// have locked the inodes involved; this lets callers create
// multi-step atomic operations.

struct {
  struct spinlock lock;
  struct inode inode[NINODE];
} icache;

void
iinit(int dev)
{
  int partitionIndex;
  initlock(&icache.lock, "icache");
  struct mbr * loadedMbr = readmbr();
  for (partitionIndex = 0; partitionIndex < 4; partitionIndex++)
  {//TODO maybe we should check what partition is bootable
    if (loadedMbr->partitions[partitionIndex].flags &  PART_BOOTABLE)
    {
      partitionBootNum=partitionIndex;
      break;
    }
  }
  readsb(dev, &sb, partitionIndex);
  cprintf("sb: size %d nblocks %d ninodes %d nlog %d logstart %d inodestart %d bmap start %d\n", sb.size,
          sb.nblocks, sb.ninodes, sb.nlog, sb.logstart, sb.inodestart, sb.bmapstart);
}

static struct inode* iget(uint dev, uint inum, int partitionNumber);

//PAGEBREAK!
// Allocate a new inode with the given type on device dev.
// A free inode has a type of zero.
struct inode*
ialloc(uint dev, short type, int partitionNumber)
{
  int inum;
  struct buf *bp;
  struct dinode *dip;
  uint bno;

  readsb(dev, &sb, partitionNumber);
  for(inum = 1; inum < sb.ninodes; inum++){
    bno = IBLOCK(inum, sb);
    APPLY_P_OFFSET(bno, partitionNumber);
    bp = bread(dev, bno);
    dip = (struct dinode*)bp->data + inum%IPB;
    if(dip->type == 0){  // a free inode
      memset(dip, 0, sizeof(*dip));
      dip->type = type;
      log_write(bp);   // mark it allocated on the disk
      brelse(bp);
      return iget(dev, inum, partitionNumber);
    }
    brelse(bp);
  }
  panic("ialloc: no inodes");
}

// Copy a modified in-memory inode to disk.
void
iupdate(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;
  uint bno;

  readsb(ROOTDEV, &sb, ip->partitionNum);
  bno = IBLOCK(ip->inum, sb);
  APPLY_P_OFFSET(bno, ip->partitionNum);
  bp = bread(ip->dev, bno);
  dip = (struct dinode*)bp->data + ip->inum%IPB;
  dip->type = ip->type;
  dip->major = ip->major;
  dip->minor = ip->minor;
  dip->nlink = ip->nlink;
  dip->size = ip->size;
  memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
  log_write(bp);
  brelse(bp);
}

// Find the inode with number inum on device dev
// and return the in-memory copy. Does not lock
// the inode and does not read it from disk.
static struct inode*
iget(uint dev, uint inum, int partitionNumber)
{
  struct inode *ip, *empty;

  acquire(&icache.lock);

  // Is the inode already cached?
  empty = 0;
  for(ip = &icache.inode[0]; ip < &icache.inode[NINODE]; ip++){
    if(ip->ref > 0 && ip->dev == dev && ip->inum == inum && ip->partitionNum == partitionNumber){
      ip->ref++;
      release(&icache.lock);
      return ip;
    }
    if(empty == 0 && ip->ref == 0)    // Remember empty slot.
      empty = ip;
  }

  // Recycle an inode cache entry.
  if(empty == 0)
    panic("iget: no inodes");

  ip = empty;
  ip->dev = dev;
  ip->inum = inum;
  ip->partitionNum = partitionNumber;
  ip->ref = 1;
  ip->flags = 0;
  release(&icache.lock);

  return ip;
}

// Increment reference count for ip.
// Returns ip to enable ip = idup(ip1) idiom.
struct inode*
idup(struct inode *ip)
{
  acquire(&icache.lock);
  ip->ref++;
  release(&icache.lock);
  return ip;
}

// Lock the given inode.
// Reads the inode from disk if necessary.
void
ilock(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;
  uint bno;
  
  readsb(ROOTDEV, &sb, ip->partitionNum);

  if(ip == 0 || ip->ref < 1)
    panic("ilock");

  acquire(&icache.lock);
  while(ip->flags & I_BUSY)
    sleep(ip, &icache.lock);
  ip->flags |= I_BUSY;
  release(&icache.lock);

  if(!(ip->flags & I_VALID)){
    bno = IBLOCK(ip->inum, sb);
    APPLY_P_OFFSET(bno, ip->partitionNum);
    bp = bread(ip->dev, bno);
    dip = (struct dinode*)bp->data + ip->inum%IPB;
    ip->type = dip->type;
    ip->major = dip->major;
    ip->minor = dip->minor;
    ip->nlink = dip->nlink;
    ip->size = dip->size;
    memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
    brelse(bp);
    ip->flags |= I_VALID;
    if(ip->type == 0)
      panic("ilock: no type");
  }
}

// Unlock the given inode.
void
iunlock(struct inode *ip)
{
  if(ip == 0 || !(ip->flags & I_BUSY) || ip->ref < 1)
    panic("iunlock");

  acquire(&icache.lock);
  ip->flags &= ~I_BUSY;
  wakeup(ip);
  release(&icache.lock);
}

// Drop a reference to an in-memory inode.
// If that was the last reference, the inode cache entry can
// be recycled.
// If that was the last reference and the inode has no links
// to it, free the inode (and its content) on disk.
// All calls to iput() must be inside a transaction in
// case it has to free the inode.
void
iput(struct inode *ip)
{
  acquire(&icache.lock);
  if(ip->ref == 1 && (ip->flags & I_VALID) && ip->nlink == 0){
    // inode has no links and no other references: truncate and free.
    if(ip->flags & I_BUSY)
      panic("iput busy");
    ip->flags |= I_BUSY;
    release(&icache.lock);
    itrunc(ip);
    ip->type = 0;
    iupdate(ip);
    acquire(&icache.lock);
    ip->flags = 0;
    wakeup(ip);
  }
  ip->ref--;
  release(&icache.lock);
}

// Common idiom: unlock, then put.
void
iunlockput(struct inode *ip)
{
  iunlock(ip);
  iput(ip);
}

//PAGEBREAK!
// Inode content
//
// The content (data) associated with each inode is stored
// in blocks on the disk. The first NDIRECT block numbers
// are listed in ip->addrs[].  The next NINDIRECT blocks are 
// listed in block ip->addrs[NDIRECT].

// Return the disk block address of the nth block in inode ip.
// If there is no such block, bmap allocates one.
static uint//returns a relative block number
bmap(struct inode *ip, uint bn)
{
  uint addr, *a;
  struct buf *bp;
  uint bno;

  if(bn < NDIRECT){
    if((addr = ip->addrs[bn]) == 0)
      ip->addrs[bn] = addr = balloc(ip->dev, ip->partitionNum);
    return addr;
  }
  bn -= NDIRECT;

  if(bn < NINDIRECT){
    // Load indirect block, allocating if necessary.
    if((addr = ip->addrs[NDIRECT]) == 0)
    {
      ip->addrs[NDIRECT] = addr = balloc(ip->dev, ip->partitionNum);
    }
    bno = addr;
    APPLY_P_OFFSET(bno, ip->partitionNum);
    bp = bread(ip->dev, bno);
    a = (uint*)bp->data;
    if((addr = a[bn]) == 0){
      a[bn] = addr = balloc(ip->dev, ip->partitionNum);
      log_write(bp);
    }
    brelse(bp);
    return addr;
  }

  panic("bmap: out of range");
}

// Truncate inode (discard contents).
// Only called when the inode has no links
// to it (no directory entries referring to it)
// and has no in-memory reference to it (is
// not an open file or current directory).
static void
itrunc(struct inode *ip)
{
  int i, j;
  struct buf *bp;
  uint bno;
  uint *a;

  for(i = 0; i < NDIRECT; i++){
    if(ip->addrs[i]){
      bfree(ip->dev, ip->addrs[i], ip->partitionNum);
      ip->addrs[i] = 0;
    }
  }
  
  if(ip->addrs[NDIRECT]){
    bno = ip->addrs[NDIRECT];
    APPLY_P_OFFSET(bno, ip->partitionNum);
    bp = bread(ip->dev, bno);
    a = (uint*)bp->data;
    for(j = 0; j < NINDIRECT; j++){
      if(a[j])
        bfree(ip->dev, a[j], ip->partitionNum);
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT], ip->partitionNum);
    ip->addrs[NDIRECT] = 0;
  }

  ip->size = 0;
  iupdate(ip);
}

// Copy stat information from inode.
void
stati(struct inode *ip, struct stat *st)
{
  st->dev = ip->dev;
  st->ino = ip->inum;
  st->type = ip->type;
  st->nlink = ip->nlink;
  st->size = ip->size;
}

//PAGEBREAK!
// Read data from inode.
int
readi(struct inode *ip, char *dst, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;
  uint bno;

  if(ip->type == T_DEV){
    if(ip->major < 0 || ip->major >= NDEV || !devsw[ip->major].read)
      return -1;
    return devsw[ip->major].read(ip, dst, n);
  }

  if(off > ip->size || off + n < off)
    return -1;
  if(off + n > ip->size)
    n = ip->size - off;

  for(tot=0; tot<n; tot+=m, off+=m, dst+=m){
    bno = bmap(ip, off/BSIZE);
    APPLY_P_OFFSET(bno, ip->partitionNum);
    bp = bread(ip->dev, bno);
    m = min(n - tot, BSIZE - off%BSIZE);
    memmove(dst, bp->data + off%BSIZE, m);
    brelse(bp);
  }
  return n;
}

// PAGEBREAK!
// Write data to inode.
int
writei(struct inode *ip, char *src, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;
  uint bno;

  if(ip->type == T_DEV){
    if(ip->major < 0 || ip->major >= NDEV || !devsw[ip->major].write)
      return -1;
    return devsw[ip->major].write(ip, src, n);
  }

  if(off > ip->size || off + n < off)
    return -1;
  if(off + n > MAXFILE*BSIZE)
    return -1;

  for(tot=0; tot<n; tot+=m, off+=m, src+=m){
    bno = bmap(ip, off/BSIZE);
    APPLY_P_OFFSET(bno, ip->partitionNum);
    bp = bread(ip->dev, bno);
    m = min(n - tot, BSIZE - off%BSIZE);
    memmove(bp->data + off%BSIZE, src, m);
    log_write(bp);
    brelse(bp);
  }

  if(n > 0 && off > ip->size){
    ip->size = off;
    iupdate(ip);
  }
  return n;
}

//PAGEBREAK!
// Directories

int
namecmp(const char *s, const char *t)
{
  return strncmp(s, t, DIRSIZ);
}

// Look for a directory entry in a directory.
// If found, set *poff to byte offset of entry.
struct inode*
dirlookup(struct inode *dp, char *name, uint *poff)
{
  uint off, inum;
  struct dirent de;

  if(dp->type != T_DIR)
    panic("dirlookup not DIR");

  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlink read");
    if(de.inum == 0)
      continue;
    if(namecmp(name, de.name) == 0){
      // entry matches path element
      if(poff)
        *poff = off;
      inum = de.inum;
      return iget(dp->dev, inum, dp->partitionNum);//TODO what if the searched inode exists in a different partition(task6)
    }
  }

  return 0;
}

// Write a new directory entry (name, inum) into the directory dp.
int
dirlink(struct inode *dp, char *name, uint inum)
{
  int off;
  struct dirent de;
  struct inode *ip;

  // Check that name is not present.
  if((ip = dirlookup(dp, name, 0)) != 0){
    iput(ip);
    return -1;
  }

  // Look for an empty dirent.
  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlink read");
    if(de.inum == 0)
      break;
  }

  strncpy(de.name, name, DIRSIZ);
  de.inum = inum;
  if(writei(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
    panic("dirlink");
  
  return 0;
}

//PAGEBREAK!
// Paths

// Copy the next path element from path into name.
// Return a pointer to the element following the copied one.
// The returned path has no leading slashes,
// so the caller can check *path=='\0' to see if the name is the last one.
// If no name to remove, return 0.
//
// Examples:
//   skipelem("a/bb/c", name) = "bb/c", setting name = "a"
//   skipelem("///a//bb", name) = "bb", setting name = "a"
//   skipelem("a", name) = "", setting name = "a"
//   skipelem("", name) = skipelem("////", name) = 0
//
static char*
skipelem(char *path, char *name)
{
  char *s;
  int len;

  while(*path == '/')
    path++;
  if(*path == 0)
    return 0;
  s = path;
  while(*path != '/' && *path != 0)
    path++;
  len = path - s;
  if(len >= DIRSIZ)
    memmove(name, s, DIRSIZ);
  else {
    memmove(name, s, len);
    name[len] = 0;
  }
  while(*path == '/')
    path++;
  return path;
}

// Look up and return the inode for a path name.
// If parent != 0, return the inode for the parent and copy the final
// path element into name, which must have room for DIRSIZ bytes.
// Must be called inside a transaction since it calls iput().
static struct inode*
namex(char *path, int nameiparent, char *name)
{
  struct inode *ip, *next;

  if(*path == '/') //absoloute
    ip = iget(ROOTDEV, ROOTINO, partitionBootNum);
  else //relative
    ip = idup(proc->cwd);

  //from here all functions use ip  - relevant to its partition 
  while((path = skipelem(path, name)) != 0){
    ilock(ip);
    if(ip->type != T_DIR){
      iunlockput(ip); //TODO theres no reference to relative/absoloute in iunlockput
			      //is this eldar's todo? thats the meaning
      return 0;
    }
    if(nameiparent && *path == '\0'){
      // Stop one level early.
      iunlock(ip);
      return ip;
    }
    if((next = dirlookup(ip, name, 0)) == 0){
      iunlockput(ip);
      return 0;
    }
    iunlockput(ip);
    ip = next;
  }
  if(nameiparent){
    iput(ip);
    return 0;
  }
  return ip;
}

struct inode*
namei(char *path)
{
  char name[DIRSIZ];
  return namex(path, 0, name);
}

struct inode*
nameiparent(char *path, char *name)
{
  
  return namex(path, 1, name);
}
