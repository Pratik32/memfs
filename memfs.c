#include<linux/init.h>
#include<linux/fs.h>
#include<linux/module.h>
#include<asm/errno.h>
#include<linux/mm.h>
#include<linux/statfs.h>
#include<linux/sched.h>
#include<linux/kernel.h>
#include<linux/writeback.h>
#include<linux/pagemap.h>
#include<linux/uio.h>

#include "internal.h"

MODULE_LICENSE("GPL"); 
unsigned long root_ino;
#define MEMFS_MAGIC 0xabcd
#define  MEMFS_DEFAULT_FILEMODE  0755

static struct inode* memfs_root_inode;
// Functions called during mount/umount.
static struct dentry* memfs_mount(struct file_system_type*,int,const char*,
                                void*);
static void memfs_kill_sb(struct super_block*);
static int memfs_fill_super(struct super_block*,void*,int);
// Super block operations.
static int memfs_write_inode(struct inode*, struct writeback_control*);
//inode operations.
static struct dentry* memfs_lookup(struct inode*,struct dentry*,unsigned int);
static int memfs_create(struct inode*,struct dentry*,umode_t,bool);
//file operations.
static int memfs_open(struct inode*,struct file*);
static struct inode* memfs_iget(struct super_block*,const struct inode*,
                                unsigned long,umode_t);
static ssize_t memfs_read(struct file*, char __user*, size_t, loff_t*);
static ssize_t memfs_write_iter(struct kiocb*, struct iov_iter*);
static int memfs_readdir(struct file*, struct dir_context*);
// address space operations
static int memfs_readpage(struct file*, struct page*);
static int memfs_writepage(struct page*, struct writeback_control*);
static int memfs_write_begin(struct file*, struct address_space*, loff_t, unsigned,
                        unsigned, struct page**, void**);
static int memfs_write_end(struct file*, struct address_space*, loff_t , unsigned,
                            unsigned, struct page*, void*);
static int memfs_set_page_dirty(struct page*);
struct file_system_type memfs = {
    .name     = "memfs",
    .mount    = memfs_mount,
    .kill_sb  = memfs_kill_sb,
    .owner    = THIS_MODULE,
    .fs_flags = FS_USERNS_MOUNT
};
struct super_operations memfs_super_operations = {
    .write_inode = memfs_write_inode,
};

/* Seperating the dir and reg file inode functions.
 * Because some operations are not applicable on 
 * reg file inode.
 */
struct inode_operations memfs_dir_inode_operations = {
    .lookup = memfs_lookup,
    .create = memfs_create
};
struct inode_operations memfs_file_inode_operations = {
    .setattr = simple_setattr,
    .getattr = simple_getattr
};

struct address_space_operations memfs_aops = {
    .readpage    = simple_readpage,
    .write_begin = memfs_write_begin,
    .write_end   = memfs_write_end,
    .set_page_dirty = memfs_set_page_dirty
};
// Using generic read ops for now.
// Will write new ones soon.
struct file_operations memfs_file_operations = {
    .open       = memfs_open,
    .read_iter  = generic_file_read_iter,
    .write_iter = generic_file_write_iter,
    .llseek     = generic_file_llseek,
    .fsync      = noop_fsync
};

/* Keeping dir operations seperate from reg file ops.
 * Dir file ops needs different code handling.
 */
const struct file_operations memfs_dir_operations = {
    .open = dcache_dir_open,
    .read = generic_read_dir,
    /* iterate_shared is getting called when we 'ls'
     * on a directory(weird!).
     * hence Attaching our readdir function here.
     */
    //.iterate_shared = memfs_readdir
    .iterate_shared  = memfs_readdir
};
/*
 * called when filesystem is mounted.
 */
static struct dentry* memfs_mount(struct file_system_type* fs,int flags,
                                   const char* devname,void* data) {
    printk("Inside: %s\n",__FUNCTION__);
    // Here we are passing superblock filler function.
    return mount_nodev(fs,flags,data,&memfs_fill_super);
}

/*  Filesystem filler function which reads superblock from disk
 *  and fills the super_block passed.
 *  data: mount options passed to superblock filler function.
 */
static int memfs_fill_super(struct super_block* sb,void* data,int flags) {
    unsigned long i_ino;
    printk("Inside memfs_fill_super\n");
    sb->s_blocksize      = PAGE_SIZE;
    sb->s_blocksize_bits = PAGE_SHIFT;
    sb->s_type           = &memfs;
    sb->s_magic          = MEMFS_MAGIC;
    sb->s_op             = &memfs_super_operations;
    i_ino                = get_next_ino();
    root_ino             = i_ino;
    DEBUG("Inode number assigned to root: %lu \n", i_ino);
    memfs_root_inode     = memfs_iget(sb, NULL, i_ino,
                            S_IFDIR|MEMFS_DEFAULT_FILEMODE);

    if(!(sb->s_root = d_make_root(memfs_root_inode))) {
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
                                        unsigned long i_no,umode_t flags) {
    struct inode *ip;
    DEBUG("Inside:%s i_no = %lu\n", __FUNCTION__, i_no);
    ip = iget_locked(sb,i_no);
    if(!ip) {
        printk("%s:Error allocating the inode\n", __FUNCTION__);
        return ERR_PTR(-ENOMEM);
    } else if(!(ip->i_state & I_NEW)) {
        printk("%s:Returning an existing inode\n", __FUNCTION__);
        return ip;
    }
    inode_init_owner(ip,dir,flags);
    printk("iget_locked excuted successfully.\n");
    printk("Inode number assigned is: %lu",i_no);
    ip->i_atime = ip->i_mtime = ip->i_ctime = current_time(ip);
    if((flags & S_IFMT) == S_IFDIR) {
        DEBUG("Filling a directory inode %lu\n", ip->i_ino);
        ip->i_fop = &memfs_dir_operations;
        ip->i_op = &memfs_dir_inode_operations;
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
 * If the required inode is not found, We attach NULL to the dentry.(which tells
 * VFS that required file does not exist.)
 * Returns the same dentry object.
 * Note:Calling memfs_iget from here is not proper,as it always allocates an inode,
 * here, we have to find out if any such inode exist or not based on filename.
 */
static struct dentry* memfs_lookup(struct inode* dir,struct dentry* entry,
                                    unsigned int flags) {
    struct inode *ip;
    DEBUG("Inside: %s\n", __FUNCTION__);
    DEBUG("Looking for file with name %s \n", entry->d_iname);
    if(dir->i_ino != root_ino) {
        printk("%s:Error in memfs_lookup\n", __FUNCTION__);
    }
    if(!entry->d_sb->s_d_op) {
        DEBUG("%s not a valid entry: %s", entry->d_name.name);
        d_set_d_op(entry, &simple_dentry_operations);
    }
    /*Here I have to figure out,how to find out if file exist
     *exist or not.(maybe a list of char array?).
     *For now just returning 'not found'.
     */
    d_add(entry, NULL);
    return NULL;
}

/*create an inode and attach it to the dentry object.
 *In our case we are just creating and 'inode' and attaching it to dentry.
 *In actual,FS must create an entry in given dir inode.
 *Note: new_inode(super_block*) creates a new inode, and adds it to the
 *inode list of FS represented by given super_block.
 *d_instantiate():adds given inode to dentry.
 *Note:d_add Vs d_instantiate
 *d_add calls d_instantiate for initializing d_inode field and adds dentry
 * to global hash table - dentryhashtable.
 */
static int memfs_create(struct inode *dir,struct dentry *entry,umode_t mode,
                        bool excl) {
    struct inode *ip;
    printk("Inside: %s\n",__FUNCTION__);
    ip = new_inode(dir->i_sb);
    DEBUG("mode is %o \n", mode);
    if(ip) {
        ip->i_ino = get_next_ino();
        mapping_set_gfp_mask(ip->i_mapping, GFP_HIGHUSER);
        inode_init_owner(ip, dir, mode | S_IFREG);
        DEBUG("%s: Filling a regular file inode %lu \n",__FUNCTION__, ip->i_ino);
        ip->i_op = &memfs_file_inode_operations;
        ip->i_fop = &memfs_file_operations;
        ip->i_mapping->a_ops = &memfs_aops;
        d_instantiate(entry,ip);
        dget(entry);
        dir->i_mtime = dir->i_ctime = current_time(dir);
    } else {
       printk("%s:Error creating inode\n",__FUNCTION__);
       return -ENOMEM;
    }
    return 0;
}
/* 
 * Called during unmounting of  FS.
 */
static void memfs_kill_sb(struct super_block* sb) {
    printk("Inside: %s\n",__FUNCTION__);
}

static int memfs_write_inode(struct inode* ip,struct writeback_control* wbc) {
    printk("Inside: %s\n",__FUNCTION__);
    return 0;
}

/*
 *Called when a file is about to open.
 *We are not supporting largefiles, hence will return overflow message.
 */
static int memfs_open(struct inode *ip,struct file *file) {
    DEBUG("Name of the file is : %s \n", file->f_path.dentry->d_iname);
    DEBUG("Inode number of its parent is %lu \n", parent_ino(file->f_path.dentry));
    DEBUG("%s: opening file with inode: %lu\n", __FUNCTION__, ip->i_ino);
    if(!(file->f_flags & O_LARGEFILE)) {
        DEBUG("Memory overflow! Large file.%lu\n", ip->i_ino);
        return -EOVERFLOW;
    }
    return 0;
}

static ssize_t memfs_read(struct file *file, char __user *user, size_t size,
                          loff_t *off) {
    DEBUG("Inside %s:\n",__FUNCTION__);
    return 0;
}
/*
 * readdir function for memfs.
 * file : directory file that need to be read.
 * dir_context: this struct consist of a position that represents
 * file descriptor offset & it contains filldir func,which a kern
 * function for filling up dentries and copy_to_user.
 * 
 * Inshort, this function is called by kernel when it wants to read/'ls'
 * a dir.On successfully reading an entry, ctx->pos must be increamented
 * by 1.
 */
static int memfs_readdir(struct file *file, struct dir_context *ctx) {
    struct dentry *curr;
    struct dentry *thedentry = file->f_path.dentry;
    loff_t pos = ctx->pos;
    struct list_head *p = &thedentry->d_subdirs, *q;
    DEBUG("position is %lld \n", ctx->pos);
    DEBUG("Reading %s directory \n", file->f_path.dentry->d_name.name);
   /* list_for_each_entry(curr, &thedentry->d_subdirs, d_child) {
        DEBUG("Name: %s \n", curr->d_name.name);
    }*/

    /*
     * This if cond is hacky, we return if we already provided 
     * all the dentries.(i.e pos != 0).
     * */
    if(pos) {
        DEBUG("All data filled in pos %lld \n", pos);
        return 0;
    }
    if(!dir_emit_dots(file, ctx)) {
        DEBUG("Error in dir_emit_dots\n");
        return 0;
    }
    for(q = p->next; q != &thedentry->d_subdirs; q = q->next) {
        curr = list_entry(q, struct dentry, d_child);
        if(!dir_emit(ctx, curr->d_name.name, curr->d_name.len,
                    curr->d_inode->i_ino, DT_UNKNOWN)) {
            DEBUG("Error in dir_emit()\n");
            break;
        }
        if (q == NULL ) {
            break;
            DEBUG("%s: returning", __FUNCTION__);
        }
        ctx->pos++;
    }
    return 0;
}

/* 
 * readpage handle for FS.
 * through readpage handler kernel expects FS to query blk device
 * and read file data in specified page.
 * As we are keeping data in memory, we are doing following things:
 *     1.Check PG_Uptodate flags, if it is unset,
 *       we are here for this page for the first time.Hence we zero 
 *       out the page and set it as uptodate.If page is upto date
 *       we do nothing and just return.
 *     2.As we are keeping data in memory we have to make sure page
 *       is not flushed by flusher threads.
 * kmap() function takes struct page and returns its logical address.
 * internally it calls page_address().Remember all the address are 
 * logical.
 * */
static int memfs_readpage(struct file *file , struct page *page) {
    void *addr = NULL;
    DEBUG("Inside %s inode: %lu \n", __FUNCTION__, file->f_inode->i_ino);
    if(!PageUptodate(page)) {
        DEBUG("Page is not upto date\n");
        addr = kmap(page);
        memset(addr, 0, PAGE_SIZE);
        kunmap(page);
        SetPageUptodate(page);
    }
    unlock_page(page);
    return 0;
}

static int memfs_writepage(struct page *page, struct writeback_control *wbc) {
    DEBUG("Inside %s \n", __FUNCTION__);
    return 0;
}

static int memfs_write_begin(struct file *file, struct address_space *mapping,
                        loff_t pos, unsigned len, unsigned flags,
                        struct page **pagep, void **fsdata) {
    pgoff_t index;
    struct page *page;
    DEBUG("inside %s\n",  __FUNCTION__);
    DEBUG("pos = %llu len = %u \n", pos, len);
    index = pos >> PAGE_SHIFT;
    page = grab_cache_page_write_begin(mapping, index, flags);
    if(!page) {
        DEBUG("%s no memory for page", __FUNCTION__);
        return -ENOMEM;
    }
    *pagep = page;

    if (!PageUptodate(page) && len != PAGE_SIZE) {
        unsigned from = (PAGE_SIZE - 1) & pos;
        zero_user_segments(page, 0, from, from+len, PAGE_SIZE);
    }
    return 0;
}

static int memfs_write_end(struct file *file, struct address_space *mapping,
                        loff_t pos, unsigned len, unsigned copied,
                        struct page *page, void *fsdata) {

    struct inode *ip = page->mapping->host;
    DEBUG("Inside %s", __FUNCTION__);
    DEBUG("inode = %lu pos = %llu len = %u copied = %u \n", ip->i_ino,
            pos, len, copied);
    if(!PageUptodate(page)) {
        DEBUG("Page was not upto date\n");
        SetPageUptodate(page);
    }
    if(ip->i_size < (pos + len)) {
        i_size_write(ip, pos + len);
    }
    set_page_dirty(page);
    unlock_page(page);
    put_page(page);
    return copied;
}

static int memfs_set_page_dirty(struct page* page) {
    if (!PageDirty(page)) {
        return !TestSetPageDirty(page);
    }
    return 0;
}

static ssize_t memfs_write_iter(struct kiocb *kiocb, struct iov_iter *i) {
    DEBUG("Inside %s \n", __FUNCTION__);
    DEBUG("Number of vectors %lu \n", i->count);
    return generic_file_write_iter(kiocb, i);
}

static int init_memfs_module(void) {
    int err;
    DEBUG("Inside: %s registering filesystem\n",__FUNCTION__);
    err = register_filesystem(&memfs);
    printk("memfs: err: %d\n",err);
    return err;
}

static void exit_memfs_module(void) {
    printk("Inside: %s\n",__FUNCTION__);
    unregister_filesystem(&memfs);
    printk("exiting... \n");
}

module_init(init_memfs_module);
module_exit(exit_memfs_module);
