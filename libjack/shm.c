/* This module provides a set of abstract shared memory interfaces
 * with support using both System V and POSIX shared memory
 * implementations.  The code is divided into three sections:
 *
 *	- common (interface-independent) code
 *	- POSIX implementation
 *	- System V implementation
 *
 * The implementation used is determined by whether USE_POSIX_SHM was
 * set in the ./configure step.
 */

/*
    Copyright (C) 2001-2003 Paul Davis
    
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation; either version 2.1 of the License, or
    (at your option) any later version.
    
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.
    
    You should have received a copy of the GNU Lesser General Public License
    along with this program; if not, write to the Free Software 
    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
    
*/

#include <config.h>

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <limits.h>
#include <errno.h>
#include <dirent.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sysdeps/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sysdeps/ipc.h>

#include <jack/shm.h>
#include <jack/internal.h>

#ifdef USE_POSIX_SHM
static jack_shmtype_t jack_shmtype = shm_POSIX;
#else
static jack_shmtype_t jack_shmtype = shm_SYSV;
#endif

/* interface-dependent forward declarations */
static int	jack_access_registry (jack_shm_info_t *ri);
static int	jack_create_registry (jack_shm_info_t *ri);
static void	jack_remove_shm (jack_shm_id_t *id);

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * common interface-independent section
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/* The JACK SHM registry is a chunk of memory for keeping track of the
 * shared memory used by each active JACK server.  This allows the
 * server to clean up shared memory when it exits.  To avoid memory
 * leakage due to kill -9, crashes or debugger-driven exits, this
 * cleanup is also done when a new instance of that server starts.
 */

/* per-process global data for the SHM interfaces */
static jack_shm_id_t   registry_id;	/* SHM id for the registry */
static jack_shm_info_t registry_info = { /* SHM info for the registry */
	.index = JACK_SHM_NULL_INDEX,
	.attached_at = MAP_FAILED
};

/* pointers to registry header and array */
static jack_shm_header_t   *jack_shm_header = NULL;
static jack_shm_registry_t *jack_shm_registry = NULL;
static char jack_shm_server_prefix[JACK_SERVER_NAME_SIZE] = "";

/* jack_shm_lock_registry() serializes updates to the shared memory
 * segment JACK uses to keep track of the SHM segements allocated to
 * all its processes, including multiple servers.
 *
 * This is not a high-contention lock, but it does need to work across
 * multiple processes.  High transaction rates and realtime safety are
 * not required.  Any solution needs to at least be portable to POSIX
 * and POSIX-like systems.
 *
 * We must be particularly careful to ensure that the lock be released
 * if the owning process terminates abnormally.  Otherwise, a segfault
 * or kill -9 at the wrong moment could prevent JACK from ever running
 * again on that machine until after a reboot.
 */

#ifndef USE_POSIX_SHM
#define JACK_SHM_REGISTRY_KEY JACK_SEMAPHORE_KEY
#endif

static int semid = -1;

/* all semaphore errors are fatal -- issue message, but do not return */
static void
semaphore_error (char *msg)
{
	jack_error ("Fatal JACK semaphore error: %s (%s)",
		    msg, strerror (errno));
	abort ();
}

static void
semaphore_init ()
{
	key_t semkey = JACK_SEMAPHORE_KEY;
	struct sembuf sbuf;
	int create_flags = IPC_CREAT | IPC_EXCL
		| S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;

	/* Get semaphore ID associated with this key. */
	if ((semid = semget(semkey, 0, 0)) == -1) {

		/* Semaphore does not exist - Create. */
		if ((semid = semget(semkey, 1, create_flags)) != -1) {

			/* Initialize the semaphore, allow one owner. */
			sbuf.sem_num = 0;
			sbuf.sem_op = 1;
			sbuf.sem_flg = 0;
			if (semop(semid, &sbuf, 1) == -1) {
				semaphore_error ("semop");
			}

		} else if (errno == EEXIST) {
			if ((semid = semget(semkey, 0, 0)) == -1) {
				semaphore_error ("semget");
			}

		} else {
			semaphore_error ("semget creation"); 
		}
	}
}

static inline void
semaphore_add (int value)
{
	struct sembuf sbuf;

	sbuf.sem_num = 0;
	sbuf.sem_op = value;
	sbuf.sem_flg = SEM_UNDO;
	if (semop(semid, &sbuf, 1) == -1) {
		semaphore_error ("semop");
	}
}

static void 
jack_shm_lock_registry (void)
{
	if (semid == -1)
		semaphore_init ();

	semaphore_add (-1);
}

static void 
jack_shm_unlock_registry (void)
{
	semaphore_add (1);
}

static void
jack_shm_init_registry ()
{
	/* registry must be locked */
	int i;

	memset (jack_shm_header, 0, JACK_SHM_REGISTRY_SIZE);

	jack_shm_header->magic = JACK_SHM_MAGIC;
	jack_shm_header->protocol = jack_protocol_version;
	jack_shm_header->type = jack_shmtype;
	jack_shm_header->size = JACK_SHM_REGISTRY_SIZE;
	jack_shm_header->hdr_len = sizeof (jack_shm_header_t);
	jack_shm_header->entry_len = sizeof (jack_shm_registry_t);

	for (i = 0; i < MAX_SHM_ID; ++i) {
		jack_shm_registry[i].index = i;
	}
}

static int
jack_shm_validate_registry ()
{
	/* registry must be locked */

	if ((jack_shm_header->magic == JACK_SHM_MAGIC)
	    && (jack_shm_header->protocol == jack_protocol_version)
	    && (jack_shm_header->type == jack_shmtype)
	    && (jack_shm_header->size == JACK_SHM_REGISTRY_SIZE)
	    && (jack_shm_header->hdr_len == sizeof (jack_shm_header_t))
	    && (jack_shm_header->entry_len == sizeof (jack_shm_registry_t))) {

		return 0;		/* registry OK */
	}

	return -1;
}

/* set a unique per-user, per-server shm prefix string
 *
 * According to the POSIX standard:
 *
 *   "The name argument conforms to the construction rules for a
 *   pathname. If name begins with the slash character, then processes
 *   calling shm_open() with the same value of name refer to the same
 *   shared memory object, as long as that name has not been
 *   removed. If name does not begin with the slash character, the
 *   effect is implementation-defined. The interpretation of slash
 *   characters other than the leading slash character in name is
 *   implementation-defined."
 *
 * Since the Linux implementation does not allow slashes *within* the
 * name, in the interest of portability we use colons instead.
 */
static void
jack_set_server_prefix (const char *server_name)
{
	snprintf (jack_shm_server_prefix, sizeof (jack_shm_server_prefix),
		  "/jack-%d:%s:", getuid (), server_name);
}

/* gain server addressability to shared memory registration segment
 *
 * returns: 0 if successful
 */
static int
jack_server_initialize_shm (int new_registry)
{
	int rc;

	if (jack_shm_header)
		return 0;		/* already initialized */

	jack_shm_lock_registry ();

	rc = jack_access_registry (&registry_info);

	if (new_registry) {
		jack_remove_shm (&registry_id);
		rc = ENOENT;
	}

	switch (rc) {
	case ENOENT:			/* registry does not exist */
		rc = jack_create_registry (&registry_info);
		break;
	case 0:				/* existing registry */
		if (jack_shm_validate_registry () == 0)
			break;
		/* else it was invalid, so fall through */
	case EINVAL:			/* bad registry */
		/* Apparently, this registry was created by an older
		 * JACK version.  Delete it so we can try again. */
		jack_release_shm (&registry_info);
		jack_remove_shm (&registry_id);
		if ((rc = jack_create_registry (&registry_info)) != 0) {
			jack_error ("incompatible shm registry (%s)",
				    strerror (errno));
#ifndef USE_POSIX_SHM
			jack_error ("to delete, use `ipcrm -M 0x%0.8x'",
				    JACK_SHM_REGISTRY_KEY);
#endif
		}
		break;
	default:			/* failure return code */
		break;
	}

	jack_shm_unlock_registry ();
	return rc;
}

/* gain client addressability to shared memory registration segment
 *
 * NOTE: this function is no longer used for server initialization,
 * instead it calls jack_register_server().
 *
 * returns: 0 if successful
 */
int
jack_initialize_shm (const char *server_name)
{
	int rc;

	if (jack_shm_header)
		return 0;		/* already initialized */

	jack_set_server_prefix (server_name);

	jack_shm_lock_registry ();

	if ((rc = jack_access_registry (&registry_info)) == 0) {
		if ((rc = jack_shm_validate_registry ()) != 0) {
			jack_error ("Incompatible shm registry, "
				    "are jackd and libjack in sync?");
		}
	}
	jack_shm_unlock_registry ();

	return rc;
}

void
jack_destroy_shm (jack_shm_info_t* si)
{
	/* must NOT have the registry locked */
	if (si->index == JACK_SHM_NULL_INDEX)
		return;			/* segment not allocated */

	jack_remove_shm (&jack_shm_registry[si->index].id);
	jack_release_shm_info (si->index);
}

jack_shm_registry_t *
jack_get_free_shm_info ()
{
	/* registry must be locked */
	jack_shm_registry_t* si = NULL;
	int i;

	for (i = 0; i < MAX_SHM_ID; ++i) {
		if (jack_shm_registry[i].size == 0) {
			break;
		}
	}
	
	if (i < MAX_SHM_ID) {
		si = &jack_shm_registry[i];
	}

	return si;
}

static inline void
jack_release_shm_entry (jack_shm_registry_index_t index)
{
	/* the registry must be locked */
	jack_shm_registry[index].size = 0;
	jack_shm_registry[index].allocator = 0;
	memset (&jack_shm_registry[index].id, 0,
		sizeof (jack_shm_registry[index].id));
}

void
jack_release_shm_info (jack_shm_registry_index_t index)
{
	/* must NOT have the registry locked */
	if (jack_shm_registry[index].allocator == getpid()) {
		jack_shm_lock_registry ();
		jack_release_shm_entry (index);
		jack_shm_unlock_registry ();
	}
}

/* Claim server_name for this process.  
 *
 * returns 0 if successful
 *	   EEXIST if server_name was already active for this user
 *	   ENOSPC if server registration limit reached
 *	   ENOMEM if unable to access shared memory registry
 */
int
jack_register_server (const char *server_name, int new_registry)
{
	int i;
	pid_t my_pid = getpid ();

	jack_set_server_prefix (server_name);

	jack_info ("JACK compiled with %s SHM support.", JACK_SHM_TYPE);

	if (jack_server_initialize_shm (new_registry))
		return ENOMEM;

	jack_shm_lock_registry ();

	/* See if server_name already registered.  Since server names
	 * are per-user, we register the unique server prefix string.
	 */
	for (i = 0; i < MAX_SERVERS; i++) {

		if (strncmp (jack_shm_header->server[i].name,
			     jack_shm_server_prefix,
			     JACK_SERVER_NAME_SIZE) != 0)
			continue;	/* no match */

		if (jack_shm_header->server[i].pid == my_pid)
			return 0;	/* it's me */

		/* see if server still exists */
		if (kill (jack_shm_header->server[i].pid, 0) == 0) {
			return EEXIST;	/* other server running */
		}

		/* it's gone, reclaim this entry */
		memset (&jack_shm_header->server[i], 0,
			sizeof (jack_shm_server_t));
	}

	/* find a free entry */
	for (i = 0; i < MAX_SERVERS; i++) {
		if (jack_shm_header->server[i].pid == 0)
			break;
	}

	if (i >= MAX_SERVERS)
		return ENOSPC;		/* out of space */

	/* claim it */
	jack_shm_header->server[i].pid = my_pid;
	strncpy (jack_shm_header->server[i].name,
		 jack_shm_server_prefix,
		 JACK_SERVER_NAME_SIZE);

	jack_shm_unlock_registry ();

	return 0;
}

/* release server_name registration */
void
jack_unregister_server (const char *server_name /* unused */)
{
	int i;
	pid_t my_pid = getpid ();

	jack_shm_lock_registry ();

	for (i = 0; i < MAX_SERVERS; i++) {
		if (jack_shm_header->server[i].pid == my_pid) {
			memset (&jack_shm_header->server[i], 0,
				sizeof (jack_shm_server_t));
		}
	}

	jack_shm_unlock_registry ();
}

/* called for server startup and termination */
int
jack_cleanup_shm ()
{
	int i;
	int destroy;
	jack_shm_info_t copy;
	pid_t my_pid = getpid ();

	jack_shm_lock_registry ();
		
	for (i = 0; i < MAX_SHM_ID; i++) {
		jack_shm_registry_t* r;

		r = &jack_shm_registry[i];
		memcpy (&copy, r, sizeof (jack_shm_info_t));
		destroy = FALSE;

		/* ignore unused entries */
		if (r->allocator == 0)
			continue;

		/* is this my shm segment? */
		if (r->allocator == my_pid) {

			/* allocated by this process, so unattach 
			   and destroy. */
			jack_release_shm (&copy);
			destroy = TRUE;

		} else {

			/* see if allocator still exists */
			if (kill (r->allocator, 0)) {
				if (errno == ESRCH) {
					/* allocator no longer exists,
					 * so destroy */
					destroy = TRUE;
				}
			}
		}

		if (destroy) {

			int index = copy.index;

			if ((index >= 0)  && (index < MAX_SHM_ID)) {
				jack_remove_shm (&jack_shm_registry[index].id);
				jack_release_shm_entry (index);
			}
			r->size = 0;
			r->allocator = 0;
		}
	}

	jack_shm_unlock_registry ();

	return TRUE;
}

/* resize a shared memory segment
 *
 * There is no way to resize a System V shm segment.  Resizing is
 * possible with POSIX shm, but not with the non-conformant Mac OS X
 * implementation.  Since POSIX shm is mainly used on that platform,
 * it's simpler to treat them both the same.
 *
 * So, we always resize by deleting and reallocating.  This is
 * tricky, because the old segment will not disappear until
 * all the clients have released it.  We only do what we can
 * from here.
 *
 * This is not done under a single lock.  I don't even want to think
 * about all the things that could possibly go wrong if multple
 * processes tried to resize the same segment concurrently.  That
 * probably doesn't happen.
 */
int
jack_resize_shm (jack_shm_info_t* si, jack_shmsize_t size)
{
	jack_release_shm (si);
	jack_destroy_shm (si);

	if (jack_shmalloc (size, si)) {
		return -1;
	}

	return jack_attach_shm (si);
}

#ifdef USE_POSIX_SHM

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * POSIX interface-dependent functions
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/* gain addressability to existing SHM registry segment
 *
 * sets up global registry pointers, if successful
 *
 * returns: 0 if existing registry accessed successfully
 *          ENOENT if registry does not exist
 *          EINVAL if registry exists, but has the wrong size
 */
static int
jack_access_registry (jack_shm_info_t *ri)
{
	/* registry must be locked */
	int shm_fd;

	strncpy (registry_id, "/jack-shm-registry", sizeof (registry_id));

	/* try to open an existing segment */
	if ((shm_fd = shm_open (registry_id, O_RDWR, 0666)) < 0) {
		int rc = errno;
		if (errno != ENOENT) {
			jack_error ("cannot open existing shm registry segment"
				    " (%s)", strerror (errno));
		}
		close (shm_fd);
		return rc;
	}

	if ((ri->attached_at = mmap (0, JACK_SHM_REGISTRY_SIZE,
				     PROT_READ|PROT_WRITE,
				     MAP_SHARED, shm_fd, 0)) == MAP_FAILED) {
		jack_error ("cannot mmap shm registry segment (%s)",
			    strerror (errno));
		close (shm_fd);
		return EINVAL;
	}

	/* set up global pointers */
	ri->index = JACK_SHM_REGISTRY_INDEX;
	jack_shm_header = ri->attached_at;
	jack_shm_registry = (jack_shm_registry_t *) (jack_shm_header + 1);

	return 0;
}

/* create a new SHM registry segment
 *
 * sets up global registry pointers, if successful
 *
 * returns: 0 if registry created successfully
 *          nonzero error code if unable to allocate a new registry
 */
static int
jack_create_registry (jack_shm_info_t *ri)
{
	/* registry must be locked */
	int shm_fd;

	strncpy (registry_id, "/jack-shm-registry", sizeof (registry_id));

	if ((shm_fd = shm_open (registry_id, O_RDWR|O_CREAT, 0666)) < 0) {
		int rc = errno;
		jack_error ("cannot create shm registry segment (%s)",
			    strerror (errno));
		return rc;
	}

	/* Set the desired segment size.  NOTE: the non-conformant Mac
	 * OS X POSIX shm only allows ftruncate() on segment creation.
	 */
	if (ftruncate (shm_fd, JACK_SHM_REGISTRY_SIZE) < 0) {
		int rc = errno;
		jack_error ("cannot set registry size (%s)", strerror (errno));
		jack_remove_shm (&registry_id);
		close (shm_fd);
		return rc;
	}

	if ((ri->attached_at = mmap (0, JACK_SHM_REGISTRY_SIZE,
				     PROT_READ|PROT_WRITE,
				     MAP_SHARED, shm_fd, 0)) == MAP_FAILED) {
		jack_error ("cannot mmap shm registry segment (%s)",
			    strerror (errno));
		jack_remove_shm (&registry_id);
		close (shm_fd);
		return EINVAL;
	}

	/* set up global pointers */
	ri->index = JACK_SHM_REGISTRY_INDEX;
	jack_shm_header = ri->attached_at;
	jack_shm_registry = (jack_shm_registry_t *) (jack_shm_header + 1);

	/* initialize registry contents */
	jack_shm_init_registry ();

	return 0;
}

static void
jack_remove_shm (jack_shm_id_t *id)
{
	/* registry may or may not be locked */
	/* note that in many cases the client has already removed
	   the shm segment, so this failing is not an error.
	   XXX it would be good to differentiate between these
	   two conditions.
	*/
	shm_unlink ((char *) id);
}

void
jack_release_shm (jack_shm_info_t* si)
{
	/* registry may or may not be locked */
	if (si->attached_at != MAP_FAILED) {
		munmap (si->attached_at, jack_shm_registry[si->index].size);
	}
}

/* allocate a POSIX shared memory segment */
int
jack_shmalloc (jack_shmsize_t size, jack_shm_info_t* si)
{
	jack_shm_registry_t* registry;
	int shm_fd;
	int rc = -1;
	char name[SHM_NAME_MAX+1];

	jack_shm_lock_registry ();

	if ((registry = jack_get_free_shm_info ()) == NULL) {
		jack_error ("shm registry full");
		goto unlock;
	}

	/* On Mac OS X, the maximum length of a shared memory segment
	 * name is SHM_NAME_MAX (instead of NAME_MAX or PATH_MAX as
	 * defined by the standard).  Unfortunately, Apple sets this
	 * value so small (about 31 bytes) that it is useless for
	 * actual names.  So, we construct a short name from the
	 * registry index for uniqueness.
	 */
	snprintf (name, sizeof (name), "/jack-%d", registry->index);

	if (strlen (name) >= sizeof (registry->id)) {
		jack_error ("shm segment name too long %s", name);
		goto unlock;
	}

	if ((shm_fd = shm_open (name, O_RDWR|O_CREAT, 0666)) < 0) {
		jack_error ("cannot create shm segment %s (%s)",
			    name, strerror (errno));
		goto unlock;
	}

	if (ftruncate (shm_fd, size) < 0) {
		jack_error ("cannot set size of engine shm "
			    "registry 0 (%s)",
			    strerror (errno));
		close (shm_fd);
		goto unlock;
	}

	close (shm_fd);
	registry->size = size;
	strncpy (registry->id, name, sizeof (registry->id));
	registry->allocator = getpid();
	si->index = registry->index;
	si->attached_at = MAP_FAILED;	/* not attached */
	rc = 0;				/* success */

 unlock:
	jack_shm_unlock_registry ();
	return rc;
}

int
jack_attach_shm (jack_shm_info_t* si)
{
	int shm_fd;
	jack_shm_registry_t *registry = &jack_shm_registry[si->index];

	if ((shm_fd = shm_open (registry->id,
				O_RDWR, 0666)) < 0) {
		jack_error ("cannot open shm segment %s (%s)", registry->id,
			    strerror (errno));
		return -1;
	}

	if ((si->attached_at = mmap (0, registry->size, PROT_READ|PROT_WRITE,
				     MAP_SHARED, shm_fd, 0)) == MAP_FAILED) {
		jack_error ("cannot mmap shm segment %s (%s)", 
			    registry->id,
			    strerror (errno));
		close (shm_fd);
		return -1;
	}

	close (shm_fd);

	return 0;
}

#else

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * System V interface-dependent functions
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/* gain addressability to existing SHM registry segment
 *
 * sets up global registry pointers, if successful
 *
 * returns: 0 if existing registry accessed successfully
 *          ENOENT if registry does not exist
 *          EINVAL if registry exists, but has the wrong size
 *          other nonzero error code if unable to access registry
 */
static int
jack_access_registry (jack_shm_info_t *ri)
{
	/* registry must be locked */

	/* try without IPC_CREAT to get existing segment */
	if ((registry_id = shmget (JACK_SHM_REGISTRY_KEY,
				   JACK_SHM_REGISTRY_SIZE, 0666)) < 0) {

		switch (errno) {

		case ENOENT:		/* segment does not exist */
			return ENOENT;

		case EINVAL:		/* segment exists, but too small */
			/* attempt minimum size access */
			registry_id = shmget (JACK_SHM_REGISTRY_KEY, 1, 0666);
			return EINVAL;

		default:		/* or other error */
			jack_error ("unable to access shm registry (%s)",
				    strerror (errno));
			return errno;
		}
	}

	if ((ri->attached_at = shmat (registry_id, 0, 0)) < 0) {
		jack_error ("cannot attach shm registry segment (%s)",
			    strerror (errno));
		return EINVAL;
	}

	/* set up global pointers */
	ri->index = JACK_SHM_REGISTRY_INDEX;
	jack_shm_header = ri->attached_at;
	jack_shm_registry = (jack_shm_registry_t *) (jack_shm_header + 1);

	return 0;
}

/* create a new SHM registry segment
 *
 * sets up global registry pointers, if successful
 *
 * returns: 0 if registry created successfully
 *          nonzero error code if unable to allocate a new registry
 */
static int
jack_create_registry (jack_shm_info_t *ri)
{
	/* registry must be locked */
	if ((registry_id = shmget (JACK_SHM_REGISTRY_KEY,
				   JACK_SHM_REGISTRY_SIZE,
				   0666|IPC_CREAT)) < 0) {
		jack_error ("cannot create shm registry segment (%s)",
			    strerror (errno));
		return errno;
	}

	if ((ri->attached_at = shmat (registry_id, 0, 0)) < 0) {
		jack_error ("cannot attach shm registry segment (%s)",
			    strerror (errno));
		return EINVAL;
	}

	/* set up global pointers */
	ri->index = JACK_SHM_REGISTRY_INDEX;
	jack_shm_header = ri->attached_at;
	jack_shm_registry = (jack_shm_registry_t *) (jack_shm_header + 1);

	/* initialize registry contents */
	jack_shm_init_registry ();

	return 0;
}

static void
jack_remove_shm (jack_shm_id_t *id)
{
	/* registry may or may not be locked */
	/* this call can fail if we are attempting to 
	   remove a segment that was already deleted
	   by the client. XXX i suppose the 
	   function should take a "canfail" argument.
	*/
	shmctl (*id, IPC_RMID, NULL);
}

void
jack_release_shm (jack_shm_info_t* si)
{
	/* registry may or may not be locked */
	if (si->attached_at != MAP_FAILED) {
		shmdt (si->attached_at);
	}
}

int
jack_shmalloc (jack_shmsize_t size, jack_shm_info_t* si) 
{
	int shmflags;
	int shmid;
	int rc = -1;
	jack_shm_registry_t* registry;

	jack_shm_lock_registry ();

	if ((registry = jack_get_free_shm_info ())) {

		shmflags = 0666 | IPC_CREAT | IPC_EXCL;

		if ((shmid = shmget (IPC_PRIVATE, size, shmflags)) >= 0) {

			registry->size = size;
			registry->id = shmid;
			registry->allocator = getpid();
			si->index = registry->index;
			si->attached_at = MAP_FAILED; /* not attached */
			rc = 0;

		} else {
			jack_error ("cannot create shm segment (%s)",
				    strerror (errno));
		}
	}

	jack_shm_unlock_registry ();

	return rc;
}

int
jack_attach_shm (jack_shm_info_t* si)
{
	if ((si->attached_at = shmat (jack_shm_registry[si->index].id,
				      0, 0)) < 0) {
		jack_error ("cannot attach shm segment (%s)",
			    strerror (errno));
		jack_release_shm_info (si->index);
		return -1;
	}
	return 0;
}

#endif /* !USE_POSIX_SHM */
