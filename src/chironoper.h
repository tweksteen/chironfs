// to avoid the initialization from incompatible pointer type message on the .init line...

#if defined __GNUC__
#pragma GCC system_header
#elif defined __SUNPRO_CC
#pragma disable_warn
#endif


static struct fuse_operations chiron_oper = {
    .destroy      = chiron_destroy,
    .init         = chiron_init,
    .getattr      = chiron_getattr,
    .access       = chiron_access,
    .readlink     = chiron_readlink,
    .readdir      = chiron_readdir,
    .mknod        = chiron_mknod,
    .mkdir        = chiron_mkdir,
    .symlink      = chiron_symlink,
    .unlink       = chiron_unlink,
    .rmdir        = chiron_rmdir,
    .rename       = chiron_rename,
    .link         = chiron_link,
    .chmod        = chiron_chmod,
    .chown        = chiron_chown,
    .truncate     = chiron_truncate,
    .utime        = chiron_utime,
    .open         = chiron_open,
    .read         = chiron_read,
    .write        = chiron_write,
    .statfs       = chiron_statfs,
    .release      = chiron_release,
    .fsync        = chiron_fsync,
    .flush        = chiron_flush,
#ifdef HAVE_SETXATTR
    .setxattr     = chiron_setxattr,
    .getxattr     = chiron_getxattr,
    .listxattr    = chiron_listxattr,
    .removexattr  = chiron_removexattr,
#endif
};

#if defined __SUNPRO_CC
#pragma enable_warn
#endif
