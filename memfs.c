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

#define  MEMFS_DEFAULT_FILEMODE  0755
static struct inode* memfs_root_inode;
// Functions called during mount/umount.
static struct dentry* memfs_mount(struct file_system_type*,int,const char*,void*);
static void memfs_kill_sb(struct super_block*);
static int memfs_fill_super(struct super_block*,void*,int);


// Super block operations.
static int memfs_write_inode(struct inode*, struct writeback_control*);

//inode operations.
static struct dentry* memfs_lookup(struct inode*,struct dentry*,unsigned int);
static int memfs_create(struct inode*,struct dentry*,umode_t,bool);

//file operations.
static int memfs_open(struct inode*,struct file*);

static struct inode* memfs_iget(struct super_block*,const struct inode*,unsigned long,umode_t);

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

/* Seperating the dir and reg file inode functions.
 * Because some operations are not applicable on 
 * reg file inode.
 */
struct inode_operations memfs_dir_inode_operations={
    .lookup=simple_lookup,
    .create=memfs_create
};
struct inode_operations memfs_file_inode_operations={
    .setattr=simple_setattr,
    .getattr=simple_getattr
};

// Using generic read ops for now.
// Will write new ones soon.
struct file_operations memfs_file_operations={
    .read_iter=generic_file_read_iter,
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
 *  data: mount options passed to superblock filler function.
 */
static int memfs_fill_super(struct super_block* sb,void* data,int flags){
    printk("Inside memfs_fill_super\n");

    sb->s_blocksize=PAGE_SIZE;
    sb->s_blocksize_bits=PAGE_SHIFT;
    sb->s_type=&memfs;
    sb->s_magic=MEMFS_MAGIC;
    sb->s_op=&memfs_super_operations;
    memfs_root_inode=memfs_iget(sb,NULL,1,S_IFDIR|MEMFS_DEFAULT_FILEMODE);

    if(!(sb->s_root=d_make_root(memfs_root_inode))){
        iput(memfs_root_inode);
        printk("%sfailed to allocate dentry\n",__FUNCTION__);
        return -ENOMEM;
    }

    return 0;
}

/* iget function of memfs.
 * Supports two types of files:directory special & regular file.
 * iget_locked() will check if inode is present in icache, if not
 * it allocates a new 'inode' pointer.
 * In actual iget creates  the inode structure associated with the 
 * given filesystem and hooks it in 'inode' pointer returned by 
 * iget_locked.Filesystem may maintain an internal inode cache for
 * peformance increase.
 */
static struct inode* memfs_iget(struct super_block* sb,const struct inode *dir,
                                        unsigned long i_no,umode_t flags){
    struct inode *ip;
    printk("Inside:%s i_no=%lu\n",__FUNCTION__,i_no);
    ip=iget_locked(sb,i_no);

    if(!ip){
        printk("%s:Error allocating the inode\n",__FUNCTION__);
        return ERR_PTR(-ENOMEM);
    }else if(!(ip->i_state & I_NEW)){
        printk("%s:Returning an existing inode\n",__FUNCTION__);
        return ip;
    }

    inode_init_owner(ip,dir,flags);
    printk("iget_locked excuted successfully.\n");
    printk("Inode number assigned is: %lu",i_no);

    ip->i_atime=ip->i_mtime=ip->i_ctime=current_time(ip);

    if((flags & S_IFMT) == S_IFDIR){
        printk("%s: Filling a directory file inode\n",__FUNCTION__);    
        ip->i_op=&memfs_dir_inode_operations;
        ip->i_fop=&simple_dir_operations;
    }else if((flags & S_IFMT)== S_IFREG){
        printk("%s: Filling a regular file inode\n",__FUNCTION__);
        ip->i_op=&memfs_file_inode_operations;
        ip->i_fop=&memfs_file_operations;
    }
    unlock_new_inode(ip);
    return ip;
}

/* memfs_lookup:inode lookup function
 * first argument is directory inode.Second argument is dentry object to which
 * we attach our lookedup inode based on filename.
 * lookup internally calls iget of  the given filesystem to get the 'inode'
 * structure and attach it to dentry object.Once we get the dentry filled,
 * we add it to the dcache.
 * Returns the same dentry object.
 */

static char filename[]="hello.txt";
static int filename_len=sizeof(filename)-1;
static struct dentry* memfs_lookup(struct inode* dir,struct dentry* entry,unsigned int flags){
    struct inode *ip;
    printk("Inside: %s\n",__FUNCTION__);
    if(dir->i_ino!=ROOT_INODE || entry->d_name.len!=filename_len){
        printk("%s:Error in memfs_lookup\n",__FUNCTION__);
    }else{
        ip=memfs_iget(dir->i_sb,dir,2,S_IFREG);
        if(!ip){
            printk("%s:Inode allocation/read failed.\n",__FUNCTION__);
            return ERR_PTR(-ENOMEM);
        }else{
            //ip->i_count+=1;
            d_add(entry,ip);
        }
    }

    return NULL;
}

/*
 *create an inode and attach it to the dentry object.
 *In our case we are just creating and 'inode' and attaching it to dentry.
 *In actual,FS must create an entry in given dir inode.
 */
static int memfs_create(struct inode *dir,struct dentry *entry,umode_t mode,bool excl){
    struct inode *ip;
    printk("Inside: %s\n",__FUNCTION__);
    ip=memfs_iget(dir->i_sb,dir,2,mode);
    if(ip){
        d_instantiate(entry,ip);
        dget(entry);
        dir->i_mtime=dir->i_ctime=current_time(dir);
    }else{
       printk("%s:Error creating inode\n",__FUNCTION__);
       return -1;
    }
    return 0;
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
