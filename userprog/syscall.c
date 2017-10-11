#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "lib/kernel/list.h"
#include <stdlib.h>
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "filesys/file.h" 

static void syscall_handler (struct intr_frame *);

struct fdesc* findFD(struct list *l, int fd){
    struct list_elem *e;
    for (e = list_begin(l); e != list_end(l); e = list_next(e))
    {
        if(list_entry(e, struct fdesc, elem)->fd == fd){
            return list_entry(e, struct fdesc, elem);
        }
    }
    return NULL;
}


/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int
get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
       : "=&a" (result) : "m" (*uaddr));
  return result;
}
 
/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
       : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}

static bool
check_string(uint8_t *start){
    char temp;
    int i = 0;
    if(start > PHYS_BASE) return false;
    while(temp = get_user(start + i)){
        i++;
        if((start + i)> PHYS_BASE) return false;
    }
    if(temp = NULL) return true;
    if(temp = -1) return false;
    return false;
}

//GLOBAL static semaphore for file-sys
static struct semaphore sema;

void
syscall_init (void)
{
  sema_init(&sema, 1);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}



static void
syscall_handler (struct intr_frame *f UNUSED)
{
    struct list_elem *e;
    //TODO VERIFY POINTERS ARE ABOVE PHYSBASE
    uint32_t* call = f->esp;          //pointer to the stack, points to syscall number, then each argument
    struct thread *current = thread_current();
    switch(*call) {
        case SYS_HALT:
            shutdown_power_off();
            break;
        case SYS_EXIT:
            //set exit status of child struct for current thread 
            
            for(e = list_begin(&current->parent->children); e != list_end(&current->parent->children); e = list_next(e)){
                if(list_entry(e, struct child_sema, childelem)->tid == current->tid){
                    list_entry(e, struct child_sema, childelem)->status = *(call + 1);
                }
            }
            thread_exit();

        case SYS_WAIT:
            process_wait((tid_t) *(call + 1));
            break;
        case SYS_EXEC:
            //check the command line string for seg faults
            if(!check_string((uint8_t *)(call + 1))) thread_exit();

            //process execute should handle the token parsing for the argument to exec            
            tid_t thd;
            sema_down(&sema);
            if((thd = process_execute(*(call+1))) == TID_ERROR)
                (f->eax) = -1;
            else{
                for(e = list_begin(&current->children); e != list_end(&current->children); e = list_next(e)){
                    if(list_entry(e, struct child_sema, childelem)->tid == thd){
                        sema_down(&list_entry(e, struct child_sema, childelem)->p_sema);
                    }
                }
                (f->eax) = (uint32_t)thd;
            //wait on semaphore to be updated from child when it executes
            }
            sema_up(&sema);
            break;
        case SYS_CREATE:
            if(!check_string((uint8_t *)(call + 1))) thread_exit();
            sema_down(&sema);
            (f->eax) = filesys_create(*(call+1), *(call+2));
            sema_up(&sema);
            break;
        case SYS_REMOVE:
            if(!check_string((uint8_t *)(call + 1))) thread_exit();
            sema_down(&sema);
            (f->eax) = filesys_remove(*(call+1));
            sema_up(&sema);
            break;
        case SYS_OPEN:
            if(!check_string((uint8_t *)(call + 1))) thread_exit();
            sema_down(&sema);
            struct file *ret;
            if((ret = filesys_open(*(call+1)) == NULL)) (f->eax) = -1;
                        else{
                struct fdesc *filed = (struct fdesc*)malloc(sizeof(struct fdesc));
                filed->f = ret;
                struct thread *current = thread_current();
               
                //add to empty list
                if(list_empty(&(current->fdt))){
                    filed->fd = 2;
                    list_insert(list_end(&(current->fdt)), &(filed->elem));
                    (f->eax) = 2;
                }
                //list not empty
                else{
                    for(e=list_begin(&(current->fdt)); e != list_end(&(current->fdt)); e = list_next(e)){
                        //skipped an fd number
                        if(list_entry(list_prev(e), struct fdesc, elem)->fd != (list_entry(e, struct fdesc, elem)->fd - 1)){
                            filed->fd = list_entry(e, struct fdesc, elem)->fd - 1;
                            list_insert(e, &(filed->elem));
                            (f->eax) = list_entry(e, struct fdesc, elem)->fd - 1;
                        }
                        //got through to last element in list
                        if(list_entry(e, struct fdesc, elem)->fd == (list_size(&(current->fdt)) + 1)){
                            filed->fd = list_size(&(current->fdt)) + 2;
                            list_push_back(&(current->fdt), &(filed->elem));
                            (f->eax) = list_size(&(current->fdt)) + 2;
                        }


                    }
                }
            }
            sema_up(&sema);
            break;
        case SYS_FILESIZE:
            sema_down(&sema);
            struct fdesc *fl;

            if((fl = findFD(&(current->fdt), *(call + 1))) == NULL){
                printf("uh oh");
                break;
            }
            (f->eax) = file_length(fl->f);
            sema_up(&sema);
            break;
        case SYS_READ:
            if(!check_string((uint8_t *)(call + 2))) thread_exit();
            sema_down(&sema);
            if(*(call + 1) == 0){
                int i;
                for(i = 0; i < *(call + 3); i++){
                    *(uint8_t *)(*(call+2) + i) = input_getc();
                }
                (f->eax) = *(call + 3);
                break;
            }
            if(fl = findFD(&(current->fdt), *(call + 1)) == NULL){
                printf("uh oh");
                break;
            }
            (f->eax) = file_read(fl->f, *(call + 2), *(call + 3));
            sema_up(&sema);
            break;
        case SYS_WRITE:
            if(!check_string((uint8_t *)(call + 2))) thread_exit();
            sema_down(&sema);
            if(*(call + 1) == 1){
                int i;
                for(i = 0; i + 250< *(call + 3); i = i+250){
                    putbuf(*(uint8_t*)(*(call+2) + i), (size_t) 250);
                }
                putbuf(*(uint8_t*)(*(call + 2) + i), (size_t)(*(call+3) - i));
                (f->eax) = *(call + 3);
            }
            else{
                if(fl = findFD(&(current->fdt), *(call + 1)) == NULL){
                    printf("uh oh");
                    break;
                }
                (f->eax) = file_write(fl->f, *(call + 2), *(call + 3));
            }
            sema_up(&sema);
            break;
        case SYS_SEEK:
            sema_down(&sema);
            if(fl = findFD(&(current->fdt), *(call + 1)) == NULL){
                printf("uh oh");
                break;
            }
            file_seek(&fl->f, (call + 2));
            sema_up(&sema);

        case SYS_TELL:
            sema_down(&sema);
            if(fl = findFD(&(current->fdt), *(call + 1)) == NULL){
                printf("uh oh");
                break;
            }
            (f->eax) = file_tell(fl->f);
            sema_up(&sema);

            break;
        case SYS_CLOSE:
            sema_down(&sema);
            if(fl = findFD(&(current->fdt), *(call + 1)) == NULL){
                printf("uh oh");
                break;
            }
            file_close(fl->f);
            list_remove(&fl->elem);
            free(fl);
            sema_up(&sema);
            break;
    }

}

