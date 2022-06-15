#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include "threads/thread.h"
#include <stdbool.h>
void syscall_init(void);

/* --- PROJECT 2 : system call ------------------------------ */
struct lock file_lock; /* proventing race condition against  */
void syscall_entry(void);
void syscall_handler(struct intr_frame *);

struct page *check_address(void *addr);
void halt(void);
void exit(int status);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int wait(tid_t pid);
int exec(const char *cmd_line);
tid_t fork(const char *thread_name, struct intr_frame *f);
int open(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);
int add_file_to_fdt(struct file *file);
void check_valid_buffer(void *buffer, unsigned size, bool is_read);
/* ---------------------------------------------------------- */

#endif /* userprog/syscall.h */