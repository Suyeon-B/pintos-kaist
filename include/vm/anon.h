#ifndef VM_ANON_H
#define VM_ANON_H
#include "vm/vm.h"
#include "include/lib/kernel/bitmap.h"
struct page;
enum vm_type;

struct anon_page
{
    /* Initiate the contets of the page */
    vm_initializer *init;
    enum vm_type type;
    void *aux;
    /* Initiate the struct page and maps the pa to the va */
    bool (*page_initializer)(struct page *, enum vm_type, void *kva);
};

struct bitmap *swap_table;

void vm_anon_init(void);
bool anon_initializer(struct page *page, enum vm_type type, void *kva);

#endif
