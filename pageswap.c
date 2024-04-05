#include "types.h"
#include "param.h"
#include "defs.h"
#include "mmu.h"
#include "memlayout.h"
#include "x86.h"
#include "proc.h"
#include "fs.h"

struct swapslot slots[NSWAPSLOTS];

void swapinit()
{
    for(int i=0;i<NSWAPSLOTS;i++)
    {
        slots[i].is_free = 1;
        slots[i].page_perm = 0;
        slots[i].num = 2+8*i;
    }
}

void free_slot(int num)
{
    slots[num].is_free = 1;
}

// Return the address of the PTE in page table pgdir
// that corresponds to virtual address va.  If alloc!=0,
// create any required page table pages.
static pte_t *
walkpgdir(pde_t *pgdir, const void *va, int alloc)
{
  pde_t *pde;
  pte_t *pgtab;

  pde = &pgdir[PDX(va)];
  if(*pde & PTE_P){
    pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
  } else {
    if(!alloc || (pgtab = (pte_t*)kalloc()) == 0)
      return 0;
    // Make sure all those PTE_P bits are zero.
    memset(pgtab, 0, PGSIZE);
    // The permissions here are overly generous, but they can
    // be further restricted by the permissions in the page table
    // entries, if necessary.
    *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;
  }
  return &pgtab[PTX(va)];
}

void
swapout()
{
    struct proc* victim = victim_process();
    // cprintf("Swapping out victim %d\n", victim->pid);
    pte_t* page = page_to_swap(victim);
    // cprintf("Fetched victim page\n");
    victim->rss -= PGSIZE;

    int i;
    for(i=0;i<NSWAPSLOTS;i++)
    {
        if(slots[i].is_free)
            break;
    }

    if(i == NSWAPSLOTS)
        panic("swapout: no free slots\n");

    slots[i].is_free = 0;
    slots[i].page_perm = PTE_FLAGS(*page);
    
    uint phy_addr = PTE_ADDR(*page);
    write_page_to_disk(ROOTDEV, (char*)P2V(phy_addr), slots[i].num);
    kfree((char*)P2V(phy_addr));

    *page = (slots[i].num << PTXSHIFT) | PTE_FLAGS(*page);
    *page &= ~PTE_P;
    *page |= PTE_SW;
}

void page_fault()
{
    // swapout();

    struct proc* p = myproc();
    uint addr = rcr2();

    pte_t* pte = walkpgdir(p->pgdir, (char*)addr, 0);
    int block_no = PTE_ADDR(*pte) >> PTXSHIFT;

    char* new_page = kalloc();
    p->rss += PGSIZE;
    read_page_from_disk(ROOTDEV, new_page, block_no);

    int swap_slot_no = (block_no - 2)/8;
    *pte = V2P(new_page) | slots[swap_slot_no].page_perm;
    *pte |= PTE_P;
    *pte &= ~PTE_SW; 
    slots[swap_slot_no].is_free = 1;
}