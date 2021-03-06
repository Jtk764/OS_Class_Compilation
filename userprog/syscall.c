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
#include "filesys/filesys.h" 
#include <string.h>
#include "devices/block.h"

static void syscall_handler (struct intr_frame *);




bool sys_readdir(struct file* file, char *path);
bool sys_isdir(struct file* file);
int sys_inumber(struct file* file);




struct fdesc* findFD(struct list *l, int fd){
    struct list_elem *e;
    for (e = list_begin(l); e != list_end(l); e = list_next(e))
    {
        int temp = list_entry(e, struct fdesc, elem)->fd;
        if(temp == fd){
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
check_string(char *start){
    char temp;
    int i = 0;
    if(start >= PHYS_BASE) return false;
    temp = get_user(start + i);
    while(temp != -1 && temp != NULL){
        i++;
        if((start + i)>= PHYS_BASE) return false;
        temp = get_user(start+i);
    }
    if(temp == NULL) return true;
    return false;
}



static bool
check_string2(uint8_t *start, int length){
    char temp;
    int i = 0;
    if(start >= PHYS_BASE) return false;
    while( i < length ){
        if ( get_user(start + i) == -1) return false;
        i++;
        if((start + i)>= PHYS_BASE) return false;
    }
    return true;
}

//GLOBAL static semaphore for file-sys
static struct semaphore sema;

void
syscall_init (void)
{
  sema_init(&sema, 1);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

bool check_sp(char * esp, int num){
    if ((uint32_t*)esp+num > PHYS_BASE) return false;
    return true;
}

static void
syscall_handler (struct intr_frame *f UNUSED)
{
    struct list_elem *e;
    //TODO VERIFY POINTERS ARE ABOVE PHYSBASE
    uint32_t* call = f->esp;          //pointer to the stack, points to syscall number, then each argument
    struct thread *current = thread_current();
    char result[50];
    char exitcode[] = ": exit(";
    char codeEnd[] = ")";
    int length;
    int test=-1;
    int ftemp;
    char* name = thread_name();
    struct file *ret;
    struct inode* inode;
    if ( f->esp > PHYS_BASE ){  
        printf("%s: exit(%d)\n", name, test);
        thread_current ()->c->status=-1;
        thread_exit(); 
        return;}
    if (get_user(call) == -1){  
        printf("%s: exit(%d)\n", name, test);
        thread_current ()->c->status=-1;
        thread_exit(); 
        return;}
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

            if (!check_sp(call, 8)) {printf("%s: exit(%d)\n", name, test); thread_current ()->c->status=-1;}
            else printf("%s%s%i%s\n", name, exitcode, *(call+1), codeEnd);
            thread_exit();
            break;

        case SYS_WAIT:
           (f->eax)=process_wait((tid_t) *(call + 1));
            break;
        case SYS_EXEC:
            //check the command line string for seg faults
            if(!check_string((uint8_t *)*(call + 1))) {  printf("%s: exit(%d)\n", name, test);thread_current ()->c->status=-1;thread_exit(); }

            //process execute should handle the token parsing for the argument to exec            
            tid_t thd;
            sema_down(&sema);
            thd = process_execute(*(call+1));
            if(thd == TID_ERROR)
                (f->eax) = -1;
            else{
                for(e = list_begin(&current->children); e != list_end(&current->children); e = list_next(e)){
                    if(list_entry(e, struct child_sema, childelem)->tid == thd){
                        sema_down(&list_entry(e, struct child_sema, childelem)->p_sema);
                        if (list_entry(e, struct child_sema, childelem)->status != -1) (f->eax) = (uint32_t)thd;
                        else f->eax=-1;
                        break;
                    }
                }
            //wait on semaphore to be updated from child when it executes
            }
            sema_up(&sema);
            break;
        case SYS_CREATE:
            if(!check_string((char*)*(call + 1))) {  printf("%s: exit(%d)\n", name, test);thread_current ()->c->status=-1;thread_exit(); }
            sema_down(&sema);
            (f->eax) = filesys_create(*(call+1), *(call+2), false);
            sema_up(&sema);
            break;
        case SYS_REMOVE:
            if(!check_string((uint8_t *)*(call + 1))) {  printf("%s: exit(%d)\n", name, test);thread_current ()->c->status=-1;thread_exit(); }
            sema_down(&sema);
            (f->eax) = filesys_remove(*(call+1));
            sema_up(&sema);
            break;
        case SYS_OPEN:
            if(!check_string((uint8_t *)*(call + 1))) {  printf("%s: exit(%d)\n", name, test);thread_current ()->c->status=-1;thread_exit(); }
            sema_down(&sema);
            ret = filesys_open(*(call+1));
            if(ret == NULL) (f->eax) = -1;
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

                        if(list_entry(e, struct fdesc, elem)->fd != 2 && 
                            list_entry(list_prev(e), struct fdesc, elem)->fd != (list_entry(e, struct fdesc, elem)->fd - 1)){
                            filed->fd = list_entry(e, struct fdesc, elem)->fd - 1;
                            list_insert(e, &(filed->elem));
                            (f->eax) = list_entry(e, struct fdesc, elem)->fd - 1;
                            break;
                        }
                        //got through to last element in list
                        else if(list_entry(e, struct fdesc, elem)->fd == (list_size(&(current->fdt)) + 1)){
                            filed->fd = list_size(&(current->fdt)) + 2;
                            list_push_back(&(current->fdt), &(filed->elem));
                            (f->eax) = filed->fd;
                            break;
                        }


                    }
                }
            }
            sema_up(&sema);
            break;
        case SYS_FILESIZE:
            sema_down(&sema);
            struct fdesc *fl;
            fl = findFD(&(current->fdt), *(call + 1));
            if(fl == NULL){
                printf("%s: exit(%d)\n", name, test);
                thread_current ()->c->status=-1;
                sema_up(&sema);
                thread_exit();
                break;
            }
            (f->eax) = file_length(fl->f);
            sema_up(&sema);
            break;
        case SYS_READ:
            if(!check_string2((uint8_t *)*(call + 2), (uint8_t *)*(call + 3))) {  printf("%s: exit(%d)\n", name, test);thread_current ()->c->status=-1;thread_exit(); }
            sema_down(&sema);
            if(*(call + 1) == 0){
                int i;
                for(i = 0; i < *(call + 3); i++){
                    *(uint8_t *)(*(call+2) + i) = input_getc();
                }
                (f->eax) = *(call + 3);
                break;
            }
            fl = findFD(&(current->fdt), *(call + 1));
            if(fl == NULL){
                printf("%s: exit(%d)\n", name, test);
                thread_current ()->c->status=-1;
                sema_up(&sema);
                thread_exit();
                break;
            }
            else if (inode_is_dir(file_get_inode(fl->f))) (f->eax)=-1;
            else (f->eax) = file_read(fl->f, *(call + 2), *(call + 3));
            sema_up(&sema);
            break;
        case SYS_WRITE:
            if(!check_string2((uint8_t *)*(call + 2), (uint8_t *)*(call + 3))) {  printf("%s: exit(%d)\n", name, test);thread_current ()->c->status=-1;thread_exit(); }
            sema_down(&sema);
            if(*(call + 1) == 1){
                int i;
                for(i = 0; i + 250< *(call + 3); i = i+250){
                    putbuf(*(uint8_t*)(*(call+2) + i), (size_t) 250);
                }
                putbuf((uint8_t *)(*(call + 2) + i), (size_t)(*(call+3) - i));
                (f->eax) = *(call + 3);
            }
            else{
                fl = findFD(&(current->fdt), *(call + 1));
                if(fl == NULL){
                    printf("%s: exit(%d)\n", name, test);
                    thread_current ()->c->status=-1;
                    sema_up(&sema);
                    thread_exit();
                    break;
                }
                else if (inode_is_dir(file_get_inode(fl->f))) (f->eax)=-1;
                else (f->eax) = file_write(fl->f, *(call + 2), *(call + 3));
            }
            sema_up(&sema);
            break;
        case SYS_SEEK:
            sema_down(&sema);
            fl = findFD(&(current->fdt), *(call + 1));
            if(fl == NULL){
                printf("%s: exit(%d)\n", name, test);
                thread_current ()->c->status=-1;
                sema_up(&sema);
                thread_exit();
                break;
            }
            file_seek(fl->f, *(call + 2));
            sema_up(&sema);

        case SYS_TELL:
            sema_down(&sema);
            fl = findFD(&(current->fdt), *(call + 1));
            if(fl == NULL){
                printf("%s: exit(%d)\n", name, test);
                thread_current ()->c->status=-1;
                sema_up(&sema);
                thread_exit();
                break;
            }
            (f->eax) = file_tell(fl->f);
            sema_up(&sema);

            break;
        case SYS_CLOSE:
            sema_down(&sema);
            fl = findFD(&(current->fdt), *(call + 1));
            if(fl == NULL){
 //               printf("uh oh");
                printf("%s: exit(%d)\n", name, test);
                thread_current ()->c->status=-1;
                sema_up(&sema);
                thread_exit();
                break;
            }
            inode = file_get_inode(fl->f);
            if(inode == NULL) return;
            if(inode_is_dir(inode)) dir_close(fl->f);
            else file_close(fl->f);
            list_remove(&fl->elem);
            free(fl);
            sema_up(&sema); 
            break;
case SYS_CHDIR:
     if(!check_string((uint8_t *)*(call + 1))) {  printf("%s: exit(%d)\n", name, test);thread_current ()->c->status=-1;thread_exit(); }
    f->eax=filesys_chdir((uint8_t *)*(call + 1));
    break;
  case SYS_MKDIR:
  if(!check_string((uint8_t *)*(call + 1))) {  printf("%s: exit(%d)\n", name, test);thread_current ()->c->status=-1;thread_exit(); }
    f->eax=filesys_create((uint8_t *)*(call + 1), 0, true);
    break;
  case SYS_READDIR:
  if(!check_string((uint8_t *)*(call + 2))) {  printf("%s: exit(%d)\n", name, test);thread_current ()->c->status=-1;thread_exit(); }
                fl = findFD(&(current->fdt), *(call + 1));
            if(fl == NULL){
                printf("%s: exit(%d)\n", name, test);
                thread_current ()->c->status=-1;
                sema_up(&sema);
                thread_exit();
                break;
            }
    f->eax=sys_readdir(fl->f, (char*)*((int *)f->esp + 2));
    break;
  case SYS_ISDIR:
                fl = findFD(&(current->fdt), *(call + 1));
            if(fl == NULL){
                printf("%s: exit(%d)\n", name, test);
                thread_current ()->c->status=-1;
                sema_up(&sema);
                thread_exit();
                break;
            }
    f->eax=sys_isdir(fl->f);
    break;
  case SYS_INUMBER:
            fl = findFD(&(current->fdt), *(call + 1));
            if(fl == NULL){
                printf("%s: exit(%d)\n", name, test);
                thread_current ()->c->status=-1;
                sema_up(&sema);
                thread_exit();
                break;
            }
    f->eax=sys_inumber(fl->f);
    break;

    }

}





bool sys_readdir(struct file* file, char* path)
{
    if (file == NULL) return false;
    
    struct inode* inode = file_get_inode(file);
    if(inode == NULL) return false;
    if(!inode_is_dir(inode)) return false;
    
    // struct dir* dir = dir_open(inode);
    struct dir* dir = (struct dir*) file;
    // if(dir == NULL) return false;
    if(!dir_readdir(dir, path)) return false;
    
    return true;
}

bool sys_isdir(struct file* file)
{
    if (file == NULL) return false;

    struct inode* inode = file_get_inode(file);
    if(inode == NULL) return false;
    if(!inode_is_dir(inode)) return false;
    
    return true;
}

int sys_inumber(struct file* file)
{
    if (file == NULL) return false;

    struct inode* inode = file_get_inode(file);
    if(inode == NULL) return false;

    block_sector_t inumber = inode_get_inumber(inode);
    return inumber;
}
