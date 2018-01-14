#include<linux/init.h>
#include<linux/fs.h>
#include<linux/module.h>
#include<asm/errno.h>
#include<linux/mm.h>
#include<linux/statfs.h>
#include<linux/sched.h>
#include<linux/kernel.h>

MODULE_LICENSE("GPL"); 
#define ROOT_INODE 1
#define MEMFS_MAGIC 0xabcd
static struct inode* memfs_root_inode;
// Functions called during mount/umount.
static struct dentry* memfs_mount(struct file_system_type*,int,const char*,void*);
static void memfs_kill_sb(struct super_block*);
static int memfs_fill_super(struct super_block*,void*,int);


// Super block operations.
static int memfs_write_inode(struct inode*, struct writeback_control*);

//inode operations.
static struct dentry* memfs_lookup(struct inode*,struct dentry*,unsigned int);


//file operations.
static int memfs_open(struct inode*,struct file*);


static struct inode* memfs_iget(struct super_block*,unsigned long,umode_t);

struct file_system_type memfs={
    .name="memfs",
    .mount=memfs_mount,
    .kill_sb=memfs_kill_sb,
    .owner=THIS_MODULE,
    .fs_flags=FS_USERNS_MOUNT
};

struct super_operations memfs_super_operations={
    .write_inode=memfs_write_inode
};

struct inode_operations memfs_inode_operations={
    .lookup=memfs_lookup
};

struct file_operations memfs_file_operations={
    .open=memfs_open,
    //.read=new_sync_read,
    .read_iter=generic_file_read_iter,
    //.write=new_sync_write,
    .write_iter=generic_file_write_iter,
    .llseek=generic_file_llseek
};
/*
 * called when filesystem is mounted.
 */
static struct dentry* memfs_mount(struct file_system_type* fs,int flags,
                                   const char* devname,void* data ){

    printk("Inside: %s\n",__FUNCTION__);
    // Here we are passing superblock filler function.
    return mount_nodev(fs,flags,data,&memfs_fill_super);
}

/*  Filesystem filler function which reads superblock from disk
 *  and fills the super_block passed.
 */
static int memfs_fill_super(struct super_block* sb,void* data,int flags){
    printk("Inside memfs_fill_super\n");

    sb->s_blocksize=PAGE_SIZE;
    sb->s_blocksize_bits=PAGE_SHIFT;
    sb->s_type=&memfs;
    sb->s_magic=MEMFS_MAGIC;
    sb->s_op=&memfs_super_operations;
    memfs_root_inode=memfs_iget(sb,1,S_IFDIR);

    if(!(sb->s_root=d_make_root(memfs_root_inode))){
        iput(memfs_root_inode);
        printk("%sfailed to allocate dentry\n",__FUNCTION__);
        return -ENOMEM;
    }

    return 0;
}

/* iget function of memfs.
 * Supports two types of files:directory special & regular file.
 */
static struct inode* memfs_iget(struct super_block* sb,unsigned long i_no,umode_t flags){
    struct inode* ip;
    printk("Inside:%s i_no=%lu\n",__FUNCTION__,i_no);
    ip=iget_locked(sb,i_no);

    if(!ip){
        printk("%s:Error allocating the inode\n",__FUNCTION__);
        return ERR_PTR(-ENOMEM);
    }else if(!(ip->i_state & I_NEW)){
        printk("%s:Returning an existing inode\n",__FUNCTION__);
        return ip;
    }

    printk("iget_locked excuted successfully.\n");
    printk("Inode number assigned is: %lu",i_no);

    ip->i_op=&memfs_inode_operations;
    ip->i_fop=&memfs_file_operations;
    ip->i_atime=ip->i_mtime=ip->i_ctime=current_time(ip);

    if(flags & S_IFDIR){
        printk("%s: Filling a directory file inode\n",__FUNCTION__);
        ip->i_mode=S_IFDIR|S_IRWXU;
    }else if(flags & S_IFREG){
        printk("%s: Filling a regular file inode\n",__FUNCTION__);
        ip->i_mode=S_IFREG|S_IRWXU;
    }
    unlock_new_inode(ip);
    return ip;
}

/* memfs_lookup:inode lookup function
 * first argument is directory inode.Second argument is dentry object to which
 * we attach our lookedup inode based on filename.
 * Returns the same dentry object.
 */

static char filename[]="hello.txt";
static int filename_len=sizeof(filename)-1;
static struct dentry* memfs_lookup(struct inode* dir,struct dentry* entry,unsigned int flags){
    struct inode* ip;
    printk("Inside: %s\n",__FUNCTION__);
    if(dir->i_ino!=ROOT_INODE || entry->d_name.len!=filename_len){
        printk("%s:Error in memfs_lookup\n",__FUNCTION__);
    }else{
        ip=memfs_iget(dir->i_sb,2,S_IFREG);
        if(!ip){
            printk("%s:Inode allocation/read failed.\n",__FUNCTION__);
            return ERR_PTR(-ENOMEM);
        }else{
            ip->i_count+=1;
            d_add(entry,ip);
        }
    }

    return NULL;
}
/* Called during unmounting of FS.
 *
 */
static void memfs_kill_sb(struct super_block* sb){
    printk("Inside: %s\n",__FUNCTION__);
}

static int memfs_write_inode(struct inode* ip,struct writeback_control* wbc){
    printk("Inside: %s\n",__FUNCTION__);
    return 1;

}

static int memfs_open(struct inode* ip,struct file* file){
    printk("%s: opening file with inode: %lu\n",__FUNCTION__,ip->i_ino);
    return generic_file_open(ip,file);
}
static int init_memfs_module(void){
    int err;
    printk("Inside: %s registering filesystem\n",__FUNCTION__);
    err=register_filesystem(&memfs);
    printk("memfs: err: %d\n",err);
    return err;
}
static void exit_memfs_module(void){
    printk("Inside: %s\n",__FUNCTION__);
    unregister_filesystem(&memfs);
    printk("exiting...");
}

module_init(init_memfs_module);
module_exit(exit_memfs_module);

