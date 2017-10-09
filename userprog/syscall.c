#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "lib/kernel/list.h"
#include <stdlib.h>

static void syscall_handler (struct intr_frame *);

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

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

static void
syscall_handler (struct intr_frame *f UNUSED)
{
    //TODO VERIFY POINTERS ARE ABOVE PHYSBASE
    uint8_t* call = f->esp;          //pointer to the stack, points to syscall number, then each argument
    switch(*(uint8_t *)call) {
        case SYS_HALT:
            shutdown_power_off();

        case SYS_EXIT:
            //all you got to do?
            thread_exit();
            
        case SYS_EXEC:
            //process execute should handle the token parsing for the argument to exec
            ;
            tid_t thd;
            if((thd = process_execute(*(call+1))) == TID_ERROR)
                (f->eax) = -1;
            else
                (f->eax) = (uint32_t)thd;

        case SYS_CREATE:
            (f->eax) = filesys_create(*(call+1), *(call+2));

        case SYS_REMOVE:
            (f->eax) = filesys_remove(*(call+1));

        case SYS_OPEN:
            ;
            struct file *ret;
            if((ret = filesys_open(*(call+1)) == NULL)){
                (f->eax) = -1;
            }
            else{
                struct fdesc *filed = (struct fdesc*)malloc(sizeof(struct fdesc));
                filed->f = ret;
                struct thread *current = thread_current();
                struct list_elem *e;
                //add to empty list
                if(list_empty((current->fdt))){
                    filed->fd = 2;
                    list_insert(list_end((current->fdt)), &(filed->elem));
                    (f->eax) = 2;
                }
                //list not empty
                else{
                    for(e=list_begin((current->fdt)); e != list_end((current->fdt)); e = list_next(e)){
                        //skipped an fd number
                        if(list_entry(list_prev(e), struct fdesc, elem)->fd != (list_entry(e, struct fdesc, elem)->fd - 1)){
                            filed->fd = list_entry(e, struct fdesc, elem)->fd - 1;
                            list_insert(e, &(filed->elem));
                            (f->eax) = list_entry(e, struct fdesc, elem)->fd - 1;
                        }
                        //got through to last element in list
                        if(list_entry(e, struct fdesc, elem)->fd == (list_size((current->fdt)) + 1)){
                            filed->fd = list_size((current->fdt)) + 2;
                            list_push_back((current->fdt), &(filed->elem));
                            (f->eax) = list_size((current->fdt)) + 2;
                        }


                    }
                }
            }

        case SYS_FILESIZE:
            ;
            struct file *fl;
            struct thread *current = thread_current();
            if((fl = findFD((current->fdt), *(call + 1))) == NULL){
                printf("uh oh");
                break;
            }
            (f->eax) = file_length(fl);

        case SYS_READ:
            if(*(call + 1) == 0){
                int i;
                for(i = 0; i < *(call + 3); i++){
                    *(uint8_t *)(*(call+2) + i) = input_getc();
                }
                (f->eax) = *(call + 3);
                break;
            }
            current = thread_current();
            if(fl = findFD((current->fdt), *(call + 1)) == NULL){
                printf("uh oh");
                break;
            }
            (f->eax) = file_read(fl, *(call + 2), *(call + 3));

        case SYS_WRITE:
            if(*(call + 1) == 1){
                int i;
                for(i = 0; i + 250< *(call + 3); i = i+250){
                    putbuf(*(uint8_t*)(*(call+2) + i), (size_t) 250);
                }
                putbuf(*(uint8_t*)(*(call + 2) + i), (size_t)(*(call+3) - i));
                (f->eax) = *(call + 3);
            }
            else{
                current = thread_current();
                if(fl = findFD((current->fdt), *(call + 1)) == NULL){
                    printf("uh oh");
                    break;
                }
                (f->eax) = file_write(fl, *(call + 2), *(call + 3));
            }

        case SYS_SEEK:
            ;
            current = thread_current();
            if(fl = findFD((current->fdt), *(call + 1)) == NULL){
                printf("uh oh");
                break;
            }
            file_seek(fl, (call + 2));

        case SYS_TELL:
            ;
            current = thread_current();
            if(fl = findFD((current->fdt), *(call + 1)) == NULL){
                printf("uh oh");
                break;
            }
            (f->eax) = file_tell(fl);


        case SYS_CLOSE:
            ;
            current = thread_current();
            if(fl = findFD((current->fdt), *(call + 1)) == NULL){
                printf("uh oh");
                break;
            }
            file_close(fl);

    }

}

