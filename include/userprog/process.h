#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_create_initd(const char *file_name);
tid_t process_fork(const char *name, struct intr_frame *if_);
int process_exec(void *f_name);
int process_wait(tid_t);
void process_exit(void);
void process_activate(struct thread *next);
void argument_stack(int argc, char **argv, struct intr_frame *if_);

// file descriptor
int add_file_to_fdt(struct file *file);
struct file *process_get_file(int fd);
void remove_file_from_fdt(int fd);
void process_close_file(int fd);
static void process_cleanup(void);
static bool load(const char *file_name, struct intr_frame *if_);
static void initd(void *f_name);
static void __do_fork(void *);
static void process_init(void);

// PJ3
bool lazy_load_segment(struct page *page, void *aux);

#endif /* userprog/process.h */
