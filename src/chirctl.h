/* Copyright 2005-2008 Luis Furquim
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */





#ifdef _CHIRON_CTL_H_

ctlfs_entry_t   ctlfs[2];
//ctlfs_entry_t   ctl_state_file;
int             max_replica          = 0;
char           *mount_point          = NULL;
char           *chironctl_mountpoint = NULL;
path_t         *paths                = NULL;
unsigned long   inode_count          = 0;
char           *fuse_options         = "-oallow_other,debug";
char           *chironctl_parentdir  = NULL;
FILE           *tochironfs           = NULL;
FILE           *fromchironfs         = NULL;
pthread_mutex_t comm                 = PTHREAD_MUTEX_INITIALIZER;
char            status_fname[]       = { "/status" };
#define         NAGIOS_FNAME           "check_chironfs.sh"
char            nagios_fname[]       = { "/" NAGIOS_FNAME };
char            nagios_script[]      = {
   "#!/bin/sh\n"
   "\n"
   "print_version() {\n"
   "   echo 'This is the ChironFS Nagios check plugin version " PACKAGE_VERSION ".'\n"
   "}\n"
   "\n"
   "print_usage() {\n"
   "   echo 'Usage: " NAGIOS_FNAME "'\n"
   "}\n"
   "\n"
   "print_help() {\n"
   "   print_version\n"
   "   echo ''\n"
   "   print_usage\n"
   "   echo ''\n"
   "   echo 'This plugin checks the status of ChironFS replicas.'\n"
   "   echo ''\n"
   "   exit 0\n"
   "}\n"
   "\n"
   "case \"$1\" in\n"
   "   --help)\n"
   "      print_help\n"
   "      exit 0\n"
   "      ;;\n"
   "   -h)\n"
   "      print_help\n"
   "      exit 0\n"
   "      ;;\n"
   "   --version)\n"
   "      print_version\n"
   "      exit 0\n"
   "      ;;\n"
   "   -V)\n"
   "      print_version\n"
   "      exit 0\n"
   "      ;;\n"
   "   *)\n"
   "      echo                                                     \n"
   "      exit  \n"
   "      ;;\n"
   "esac\n"
};

char            status_msgs[3][53] = {
   { "OK - ChironFS replica enabled                       " },
   { "WARNING - ChironFS replica enabled, but inconsistent" },
   { "CRITICAL - ChironFS replica disabled                " }
};


#else

extern ctlfs_entry_t  *ctlfs;
//extern ctlfs_entry_t   ctl_state_file;
extern struct          fuse_operations chironctl_oper;
extern int             max_replica;
extern char           *mount_point;
extern char           *chironctl_mountpoint;
extern path_t         *paths;
extern unsigned long   inode_count;
extern char           *chironctl_parentdir;
extern FILE           *tochironfs, *fromchironfs;
extern pthread_mutex_t comm;
extern char           *status_fname;
extern char           *nagios_fname;
extern char           *nagios_script;

#endif


int mkctlfs(void);
ctlfs_entry_t mkstatnod(char *path, unsigned long mode, unsigned short uid, unsigned short gid);
ctlfs_search_t find_path(const char *path, ctlfs_entry_t *c, int deep);
void free_ctlnode(ctlfs_entry_t *ctlroot);
void free_vars(void);
int get_perm(uid_t uid, gid_t gid, struct stat st);
int get_path_perm(const char *path);
char *get_daddy(const char *path);

