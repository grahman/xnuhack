
/* List of procs being spied upon by spyfs */
#ifndef __SPYLIST__
#define __SPYLIST__

#define MAX_PATH_LENGTH		256
#define MAX_PROC_NAME_LENGTH	128

/* Mode defines */
#define SPY_MODE_OPEN		0
#define SPY_MODE_READ		1
#define SPY_MODE_WRITE		2
#define SPY_MODE_CLOSE		3
#define SPY_MODE_MMAP		4
#define SPY_MODE_PAGEIN		5
#define SPY_MODE_PAGEOUT	6
#define SPY_MODE_MUNMAP		7

#define PROC_FOREACH_DESCENDANT(p, iter)\
	for (iter = p->p_children.lh_first; iter != NULL; iter = iter->p_children.lh_first)
#define PROC_FOREACH_SIBLING(p, iter)\
	LIST_FOREACH(iter, &p->p_pptr->p_children, p_sibling)

typedef struct spy {
	proc_t p;	/* struct proc * that is being tracked */	
	int options;
	LIST_ENTRY(spy) others;
} spy;
LIST_HEAD(spylist, spy); 

struct spy_msg {
	mach_msg_header_t header;
	char proc_name[128];
	char path[MAX_PATH_LENGTH];
	int mode;	/* 0 = read, 1 = write */
};

struct spy_vars {
	mach_port_name_t	port_name;
	ipc_space_t		ipc_space;
	int			set;
};

/* This struct keeps track of memory=mapped files
 * for pageout monitoring. Mmap'ed files are not
 * paged out by the task using them, but by a
 * separate kernel thread(s): vnode_pager. 
 * Also note that we incremented the v_kusecount
 * field for the vnode in mmap syscall */

typedef struct spy_mmap_info {
	struct vnode *vp;	/* Vnode being paged */
	LIST_ENTRY(spy_mmap_info) next_vnode;
} spy_mmap_info;

/* Linked list of mmap'd vnodes  */
LIST_HEAD(spy_mmap_info_list, spy_mmap_info);

/* If we are looking for a proc by name */
typedef char	spy_name[MAX_PROC_NAME_LENGTH];


int proc_is_sibling(proc_t test, proc_t against, int locked);
int proc_is_descendant(proc_t test, proc_t against, int locked);
/* Spylist locks */
extern lck_grp_attr_t *spylist_mtx_grp_attr;
extern lck_grp_t *spylist_mtx_grp;
extern lck_attr_t *spylist_mtx_attr;
extern lck_mtx_t *spylist_mtx;
/* spy_mmap_info_list locks */
extern lck_grp_attr_t *spy_mmap_list_mtx_grp_attr;
extern lck_grp_t *spy_mmap_list_mtx_grp;
extern lck_attr_t *spy_mmap_list_mtx_attr;
extern lck_mtx_t *spy_mmap_list_mtx;

#endif
