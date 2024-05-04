#include <interrupt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <memory.h>
#include <kernel.h>

void    *minheap;   /* Start address of heap    */
void    *maxheap;   /* End address of heap      */
struct  memblk  memlist;

uint32_t heap_free(){
  struct  memblk  *memptr;  /* Ptr to memory block    */
  uint32  free_mem;   /* Total amount of free memory  */
  free_mem = 0;
  for (memptr = memlist.mnext; memptr != NULL; memptr = memptr->mnext) {
       free_mem += memptr->mlength;
  }
  return free_mem;
}

void    meminit(void)
{
    struct  memblk *memptr; /* Memory block pointer */
    /* Initialize the minheap and maxheap variables */
    minheap = (void *)KMALLOC_START;
    /* 1024 bytes is reserved for supervise mode handling */
    maxheap = (void *)MAXADDR;
    /* Initialize the memory list as one big block */
    memlist.mnext = (struct memblk *)minheap;
    memptr = memlist.mnext;

    memptr->mnext = (struct memblk *)NULL;
    memlist.mlength = memptr->mlength =
        (uint32)maxheap - (uint32)minheap;
}



/*------------------------------------------------------------------------
 *  freemem  -  Free a memory block, returning the block to the free list
 *------------------------------------------------------------------------
 */
uint32 freemem(
      char      *blkaddr,   /* Pointer to memory block  */
      uint32    nbytes      /* Size of block in bytes   */
    )
{
    uint32 mask;           /* Saved interrupt mask     */
    struct  memblk  *next, *prev, *block;
    uint32  top;

    //mask = irq_disable();
    if ((nbytes == 0) || ((uint32) blkaddr < (uint32) minheap)
              || ((uint32) blkaddr > (uint32) maxheap)) {
       // irq_restore(mask);
        return SYSERR;
    }

    nbytes = (uint32) roundmb(nbytes);  /* Use memblk multiples */
    block = (struct memblk *)blkaddr;

    prev = &memlist;            /* Walk along free list */
    next = memlist.mnext;
    while ((next != NULL) && (next < block)) {
        prev = next;
        next = next->mnext;
    }

    if (prev == &memlist) {     /* Compute top of previous block*/
        top = (uint32) NULL;
    } else {
        top = (uint32) prev + prev->mlength;
    }

    /* Ensure new block does not overlap previous or next blocks    */

    if (((prev != &memlist) && (uint32) block < top)
        || ((next != NULL)  && (uint32) block+nbytes>(uint32)next)) {
        //irq_restore(mask);
        return SYSERR;
    }

    memlist.mlength += nbytes;

    /* Either coalesce with previous block or add to free list */

    if (top == (uint32) block) {    /* Coalesce with previous block */
        prev->mlength += nbytes;
        block = prev;
    } else {            /* Link into list as new node   */
        block->mnext = next;
        block->mlength = nbytes;
        prev->mnext = block;
    }

    /* Coalesce with next block if adjacent */

    if (((uint32) block + block->mlength) == (uint32) next) {
        block->mlength += next->mlength;
        block->mnext = next->mnext;
    }
    //irq_restore(mask);
    return OK;
}

/*------------------------------------------------------------------------
 *  getmem  -  Allocate heap storage, returning lowest word address
 *------------------------------------------------------------------------
 */
char    *getmem(
      uint32    nbytes      /* Size of memory requested */
    )
{
    uint32 mask;           /* Saved interrupt mask     */
    struct  memblk  *prev, *curr, *leftover;

    //mask = irq_disable();
    if (nbytes == 0) {
        //irq_restore(mask);
        return (char *)SYSERR;
    }

    nbytes = (uint32) roundmb(nbytes);  /* Use memblk multiples */

    prev = &memlist;
    curr = memlist.mnext;
    while (curr != NULL) {          /* Search free list */

        if (curr->mlength == nbytes) {  /* Block is exact match */
            prev->mnext = curr->mnext;
            memlist.mlength -= nbytes;
            //irq_restore(mask);
            return (char *)(curr);

        } else if (curr->mlength > nbytes) { /* Split big block */
            leftover = (struct memblk *)((uint32) curr +
                    nbytes);
            prev->mnext = leftover;
            leftover->mnext = curr->mnext;
            leftover->mlength = curr->mlength - nbytes;
            memlist.mlength -= nbytes;
            //irq_restore(mask);
            return (char *)(curr);
        } else {            /* Move to next block   */
            prev = curr;
            curr = curr->mnext;
        }
    }
    //irq_restore(mask);
    return (char *)SYSERR;
}

/*------------------------------------------------------------------------
 *  getstk  -  Allocate stack memory, returning highest word address
 *------------------------------------------------------------------------
 */
char    *getstk(
      uint32    nbytes      /* Size of memory requested */
    )
{
    intmask mask;           /* Saved interrupt mask     */
    struct  memblk  *prev, *curr;   /* Walk through memory list */
    struct  memblk  *fits, *fitsprev; /* Record block that fits */

    mask = irq_disable();
    if (nbytes == 0) {
        irq_restore(mask);
        return (char *)SYSERR;
    }

    nbytes = (uint32) roundmb(nbytes);  /* Use mblock multiples */

    prev = &memlist;
    curr = memlist.mnext;
    fits = NULL;
    fitsprev = NULL;  /* Just to avoid a compiler warning */

    while (curr != NULL) {          /* Scan entire list */
        if (curr->mlength >= nbytes) {  /* Record block address */
            fits = curr;        /*   when request fits  */
            fitsprev = prev;
        }
        prev = curr;
        curr = curr->mnext;
    }

    if (fits == NULL) {         /* No block was found   */
        irq_restore(mask);
        return (char *)SYSERR;
    }
    if (nbytes == fits->mlength) {      /* Block is exact match */
        fitsprev->mnext = fits->mnext;
    } else {                /* Remove top section   */
        fits->mlength -= nbytes;
        fits = (struct memblk *)((uint32)fits + fits->mlength);
    }
    memlist.mlength -= nbytes;
    irq_restore(mask);
    return (char *)((uint32) fits + nbytes - sizeof(uint32));
}



void *malloc(size_t length){
    struct memblk *pmem;
    /* we don't allocate 0 bytes. */
    if (0 == length)
    {
        return NULL;
    }

    /* make room for accounting info */
    length += sizeof(struct memblk);

    /* acquire memory from kernel */
    pmem = (struct memblk *)getmem(length);
    if (SYSERR == (uint32)pmem)
    {
        //sem_signal(&sem_malloc);//irq_restore(q);
        return NULL;
    }

    /* set accounting info */
    pmem->mnext = pmem;
    pmem->mlength = length;
    return (void *)(pmem + 1);
}

void free (void *p) {
    struct memblk *block;
    /* block points at the memblock we want to free */
    block = (struct memblk *)p;

    /* back up to accounting information */
    block--;

    /* don't memfree if we fail basic checks */
    if (block->mnext != block)
    {
        return;
    }

    freemem((char *)block, block->mlength);
}
