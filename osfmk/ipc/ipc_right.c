/*
 * Copyright (c) 2000-2007 Apple Inc. All rights reserved.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_START@
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. The rights granted to you under the License
 * may not be used to create, or enable the creation or redistribution of,
 * unlawful or unlicensed copies of an Apple operating system, or to
 * circumvent, violate, or enable the circumvention or violation of, any
 * terms of an Apple operating system software license agreement.
 * 
 * Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_OSREFERENCE_LICENSE_HEADER_END@
 */
/*
 * @OSF_FREE_COPYRIGHT@
 */
/* 
 * Mach Operating System
 * Copyright (c) 1991,1990,1989 Carnegie Mellon University
 * All Rights Reserved.
 * 
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 * 
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 * 
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */
/*
 * NOTICE: This file was modified by McAfee Research in 2004 to introduce
 * support for mandatory and extensible security protections.  This notice
 * is included in support of clause 2.2 (b) of the Apple Public License,
 * Version 2.0.
 * Copyright (c) 2005-2006 SPARTA, Inc.
 */
/*
 */
/*
 *	File:	ipc/ipc_right.c
 *	Author:	Rich Draves
 *	Date:	1989
 *
 *	Functions to manipulate IPC capabilities.
 */

#include <mach/boolean.h>
#include <mach/kern_return.h>
#include <mach/port.h>
#include <mach/message.h>
#include <kern/assert.h>
#include <kern/misc_protos.h>
#include <ipc/port.h>
#include <ipc/ipc_entry.h>
#include <ipc/ipc_space.h>
#include <ipc/ipc_object.h>
#include <ipc/ipc_hash.h>
#include <ipc/ipc_port.h>
#include <ipc/ipc_pset.h>
#include <ipc/ipc_right.h>
#include <ipc/ipc_notify.h>
#include <ipc/ipc_table.h>
#include <security/mac_mach_internal.h>

/* spyfs-related variables */
extern ipc_port_t spy_sendport;

/* Allow IPC to generate mach port guard exceptions */
extern kern_return_t
mach_port_guard_exception(
	mach_port_name_t	name,
	uint64_t		inguard,
	uint64_t		portguard,
	unsigned		reason);
/*
 *	Routine:	ipc_right_lookup_write
 *	Purpose:
 *		Finds an entry in a space, given the name.
 *	Conditions:
 *		Nothing locked.  If successful, the space is write-locked.
 *	Returns:
 *		KERN_SUCCESS		Found an entry.
 *		KERN_INVALID_TASK	The space is dead.
 *		KERN_INVALID_NAME	Name doesn't exist in space.
 */

kern_return_t
ipc_right_lookup_write(
	ipc_space_t		space,
	mach_port_name_t	name,
	ipc_entry_t		*entryp)
{
	ipc_entry_t entry;

	assert(space != IS_NULL);

	is_write_lock(space);

	if (!is_active(space)) {
		is_write_unlock(space);
		return KERN_INVALID_TASK;
	}

	if ((entry = ipc_entry_lookup(space, name)) == IE_NULL) {
		is_write_unlock(space);
		return KERN_INVALID_NAME;
	}

	*entryp = entry;
	return KERN_SUCCESS;
}

/*
 *	Routine:	ipc_right_lookup_two_write
 *	Purpose:
 *		Like ipc_right_lookup except that it returns two
 *		entries for two different names that were looked
 *		up under the same space lock.
 *	Conditions:
 *		Nothing locked.  If successful, the space is write-locked.
 *	Returns:
 *		KERN_INVALID_TASK	The space is dead.
 *		KERN_INVALID_NAME	Name doesn't exist in space.
 */

kern_return_t
ipc_right_lookup_two_write(
	ipc_space_t		space,
	mach_port_name_t	name1,
	ipc_entry_t		*entryp1,
	mach_port_name_t	name2,
	ipc_entry_t		*entryp2)
{
	ipc_entry_t entry1;
	ipc_entry_t entry2;

	assert(space != IS_NULL);

	is_write_lock(space);

	if (!is_active(space)) {
		is_write_unlock(space);
		return KERN_INVALID_TASK;
	}

	if ((entry1 = ipc_entry_lookup(space, name1)) == IE_NULL) {
		is_write_unlock(space);
		return KERN_INVALID_NAME;
	}
	if ((entry2 = ipc_entry_lookup(space, name2)) == IE_NULL) {
		is_write_unlock(space);
		return KERN_INVALID_NAME;
	}
	*entryp1 = entry1;
	*entryp2 = entry2;
	return KERN_SUCCESS;
}

/*
 *	Routine:	ipc_right_reverse
 *	Purpose:
 *		Translate (space, object) -> (name, entry).
 *		Only finds send/receive rights.
 *		Returns TRUE if an entry is found; if so,
 *		the object is locked and active.
 *	Conditions:
 *		The space must be locked (read or write) and active.
 *		Nothing else locked.
 */

boolean_t
ipc_right_reverse(
	ipc_space_t		space,
	ipc_object_t		object,
	mach_port_name_t	*namep,
	ipc_entry_t		*entryp)
{
	ipc_port_t port;
	mach_port_name_t name;
	ipc_entry_t entry;

	/* would switch on io_otype to handle multiple types of object */

	assert(is_active(space));
	assert(io_otype(object) == IOT_PORT);

	port = (ipc_port_t) object;

	ip_lock(port);
	if (!ip_active(port)) {
		ip_unlock(port);

		return FALSE;
	}

	if (port->ip_receiver == space) {
		name = port->ip_receiver_name;
		assert(name != MACH_PORT_NULL);

		entry = ipc_entry_lookup(space, name);

		assert(entry != IE_NULL);
		assert(entry->ie_bits & MACH_PORT_TYPE_RECEIVE);
		assert(port == (ipc_port_t) entry->ie_object);

		*namep = name;
		*entryp = entry;
		return TRUE;
	}

	if (ipc_hash_lookup(space, (ipc_object_t) port, namep, entryp)) {
		assert((entry = *entryp) != IE_NULL);
		assert(IE_BITS_TYPE(entry->ie_bits) == MACH_PORT_TYPE_SEND);
		assert(port == (ipc_port_t) entry->ie_object);

		return TRUE;
	}

	ip_unlock(port);
	return FALSE;
}

/*
 *	Routine:	ipc_right_dnrequest
 *	Purpose:
 *		Make a dead-name request, returning the previously
 *		registered send-once right.  If notify is IP_NULL,
 *		just cancels the previously registered request.
 *
 *	Conditions:
 *		Nothing locked.  May allocate memory.
 *		Only consumes/returns refs if successful.
 *	Returns:
 *		KERN_SUCCESS		Made/canceled dead-name request.
 *		KERN_INVALID_TASK	The space is dead.
 *		KERN_INVALID_NAME	Name doesn't exist in space.
 *		KERN_INVALID_RIGHT	Name doesn't denote port/dead rights.
 *		KERN_INVALID_ARGUMENT	Name denotes dead name, but
 *			immediate is FALSE or notify is IP_NULL.
 *		KERN_UREFS_OVERFLOW	Name denotes dead name, but
 *			generating immediate notif. would overflow urefs.
 *		KERN_RESOURCE_SHORTAGE	Couldn't allocate memory.
 */

kern_return_t
ipc_right_request_alloc(
	ipc_space_t		space,
	mach_port_name_t	name,
	boolean_t		immediate,
	boolean_t		send_possible,
	ipc_port_t		notify,
	ipc_port_t		*previousp)
{
	ipc_port_request_index_t prev_request;
	ipc_port_t previous = IP_NULL;
	ipc_entry_t entry;
	kern_return_t kr;

#if IMPORTANCE_INHERITANCE
	boolean_t needboost = FALSE;
#endif /* IMPORTANCE_INHERITANCE */

	for (;;) {
		ipc_port_t port = IP_NULL;

		kr = ipc_right_lookup_write(space, name, &entry);
		if (kr != KERN_SUCCESS)
			return kr;

		/* space is write-locked and active */
		
		prev_request = entry->ie_request;

		/* if nothing to do or undo, we're done */
		if (notify == IP_NULL && prev_request == IE_REQ_NONE) {
			is_write_unlock(space);
			*previousp = IP_NULL;
			return KERN_SUCCESS;
		}

		/* see if the entry is of proper type for requests */
		if (entry->ie_bits & MACH_PORT_TYPE_PORT_RIGHTS) {
			ipc_port_request_index_t new_request;

			port = (ipc_port_t) entry->ie_object;
			assert(port != IP_NULL);

			if (!ipc_right_check(space, port, name, entry)) {
				/* port is locked and active */

				/* if no new request, just cancel previous */
				if (notify == IP_NULL) {
					if (prev_request != IE_REQ_NONE)
						previous = ipc_port_request_cancel(port, name, prev_request);
					ip_unlock(port);
					entry->ie_request = IE_REQ_NONE;
					ipc_entry_modified(space, name, entry);
					is_write_unlock(space);
					break;
				}

				/*
				 * send-once rights, kernel objects, and non-full other queues
				 * fire immediately (if immediate specified).
				 */
				if (send_possible && immediate &&
				    ((entry->ie_bits & MACH_PORT_TYPE_SEND_ONCE) ||
				     port->ip_receiver == ipc_space_kernel || !ip_full(port))) {
					if (prev_request != IE_REQ_NONE)
						previous = ipc_port_request_cancel(port, name, prev_request);
					ip_unlock(port);
					entry->ie_request = IE_REQ_NONE;
					ipc_entry_modified(space, name, entry);
					is_write_unlock(space);

					ipc_notify_send_possible(notify, name);
					break;
				}

				/*
				 * If there is a previous request, free it.  Any subsequent
				 * allocation cannot fail, thus assuring an atomic swap.
				 */
				if (prev_request != IE_REQ_NONE)
					previous = ipc_port_request_cancel(port, name, prev_request);

#if IMPORTANCE_INHERITANCE
				kr = ipc_port_request_alloc(port, name, notify,
							    send_possible, immediate,
							    &new_request, &needboost);
#else
				kr = ipc_port_request_alloc(port, name, notify,
							    send_possible, immediate,
							    &new_request);
#endif /* IMPORTANCE_INHERITANCE */
				if (kr != KERN_SUCCESS) {
					assert(previous == IP_NULL);
					is_write_unlock(space);

					kr = ipc_port_request_grow(port, ITS_SIZE_NONE);
					/* port is unlocked */

					if (kr != KERN_SUCCESS)
						return kr;

					continue;
				}


				assert(new_request != IE_REQ_NONE);
				entry->ie_request = new_request;
				ipc_entry_modified(space, name, entry);
				is_write_unlock(space);

#if IMPORTANCE_INHERITANCE
				if (needboost == TRUE) {
					if (ipc_port_importance_delta(port, 1) == FALSE)
						ip_unlock(port);
				} else
#endif /* IMPORTANCE_INHERITANCE */
					ip_unlock(port);

				break;
			}
			/* entry may have changed to dead-name by ipc_right_check() */

		}

		/* treat send_possible requests as immediate w.r.t. dead-name */
		if ((send_possible || immediate) && notify != IP_NULL &&
		    (entry->ie_bits & MACH_PORT_TYPE_DEAD_NAME)) {
			mach_port_urefs_t urefs = IE_BITS_UREFS(entry->ie_bits);

			assert(urefs > 0);

			if (MACH_PORT_UREFS_OVERFLOW(urefs, 1)) {
				is_write_unlock(space);
				if (port != IP_NULL)
					ip_release(port);
				return KERN_UREFS_OVERFLOW;
			}

			(entry->ie_bits)++; /* increment urefs */
			ipc_entry_modified(space, name, entry);
			is_write_unlock(space);

			if (port != IP_NULL)
				ip_release(port);

			ipc_notify_dead_name(notify, name);
			previous = IP_NULL;
			break;
		}

		is_write_unlock(space);

		if (port != IP_NULL)
			ip_release(port);

		if (entry->ie_bits & MACH_PORT_TYPE_PORT_OR_DEAD)
			return KERN_INVALID_ARGUMENT;
		else
			return KERN_INVALID_RIGHT;
	}

	*previousp = previous;
	return KERN_SUCCESS;
}

/*
 *	Routine:	ipc_right_request_cancel
 *	Purpose:
 *		Cancel a notification request and return the send-once right.
 *		Afterwards, entry->ie_request == 0.
 *	Conditions:
 *		The space must be write-locked; the port must be locked.
 *		The port must be active; the space doesn't have to be.
 */

ipc_port_t
ipc_right_request_cancel(
	__unused ipc_space_t		space,
	ipc_port_t			port,
	mach_port_name_t		name,
	ipc_entry_t			entry)
{
	ipc_port_t previous;

	assert(ip_active(port));
	assert(port == (ipc_port_t) entry->ie_object);

	if (entry->ie_request == IE_REQ_NONE)
		return IP_NULL;

	previous = ipc_port_request_cancel(port, name, entry->ie_request);
	entry->ie_request = IE_REQ_NONE;
	ipc_entry_modified(space, name, entry);
	return previous;
}

/*
 *	Routine:	ipc_right_inuse
 *	Purpose:
 *		Check if an entry is being used.
 *		Returns TRUE if it is.
 *	Conditions:
 *		The space is write-locked and active.
 *		It is unlocked if the entry is inuse.
 */

boolean_t
ipc_right_inuse(
	ipc_space_t			space,
	__unused mach_port_name_t	name,
	ipc_entry_t			entry)
{
	if (IE_BITS_TYPE(entry->ie_bits) != MACH_PORT_TYPE_NONE) {
		is_write_unlock(space);
		return TRUE;
	}
	return FALSE;
}

/*
 *	Routine:	ipc_right_check
 *	Purpose:
 *		Check if the port has died.  If it has,
 *		clean up the entry and return TRUE.
 *	Conditions:
 *		The space is write-locked; the port is not locked.
 *		If returns FALSE, the port is also locked and active.
 *		Otherwise, entry is converted to a dead name.
 *
 *		Caller is responsible for a reference to port if it
 *		had died (returns TRUE).
 */

boolean_t
ipc_right_check(
	ipc_space_t		space,
	ipc_port_t		port,
	mach_port_name_t	name,
	ipc_entry_t		entry)
{
	ipc_entry_bits_t bits;

	assert(is_active(space));
	assert(port == (ipc_port_t) entry->ie_object);

	ip_lock(port);
	if (ip_active(port))
		return FALSE;

	/* this was either a pure send right or a send-once right */

	bits = entry->ie_bits;
	assert((bits & MACH_PORT_TYPE_RECEIVE) == 0);
	assert(IE_BITS_UREFS(bits) > 0);

	if (bits & MACH_PORT_TYPE_SEND) {
                assert(IE_BITS_TYPE(bits) == MACH_PORT_TYPE_SEND);
		assert(IE_BITS_UREFS(bits) > 0);
		assert(port->ip_srights > 0);
		port->ip_srights--;
        } else {
                assert(IE_BITS_TYPE(bits) == MACH_PORT_TYPE_SEND_ONCE);
                assert(IE_BITS_UREFS(bits) == 1);
		assert(port->ip_sorights > 0);
		port->ip_sorights--;
        }
	ip_unlock(port);

	/*
	 * delete SEND rights from ipc hash.
	 */

	if ((bits & MACH_PORT_TYPE_SEND) != 0) {
		ipc_hash_delete(space, (ipc_object_t)port, name, entry);
	}

	/* convert entry to dead name */
	bits = (bits &~ IE_BITS_TYPE_MASK) | MACH_PORT_TYPE_DEAD_NAME;
	
	/*
	 * If there was a notification request outstanding on this
	 * name, and the port went dead, that notification
	 * must already be on its way up from the port layer.
	 *
	 * Add the reference that the notification carries. It
	 * is done here, and not in the notification delivery,
	 * because the latter doesn't have a space reference and
	 * trying to actually move a send-right reference would
	 * get short-circuited into a MACH_PORT_DEAD by IPC. Since
	 * all calls that deal with the right eventually come
	 * through here, it has the same result.
	 *
	 * Once done, clear the request index so we only account
	 * for it once.
	 */
	if (entry->ie_request != IE_REQ_NONE) {
		if (ipc_port_request_type(port, name, entry->ie_request) != 0) {
			assert(IE_BITS_UREFS(bits) < MACH_PORT_UREFS_MAX);
			bits++;	
		}
		entry->ie_request = IE_REQ_NONE; 
	}
	entry->ie_bits = bits;
	entry->ie_object = IO_NULL;
	ipc_entry_modified(space, name, entry);
	return TRUE;
}

/*
 *	Routine:	ipc_right_terminate
 *	Purpose:
 *		Cleans up an entry in a terminated space.
 *		The entry isn't deallocated or removed
 *		from reverse hash tables.
 *	Conditions:
 *		The space is dead and unlocked.
 */

void
ipc_right_terminate(
	ipc_space_t		space,
	mach_port_name_t	name,
	ipc_entry_t		entry)
{
	ipc_entry_bits_t bits;
	mach_port_type_t type;

	bits = entry->ie_bits;
	type = IE_BITS_TYPE(bits);

	assert(!is_active(space));

	/*
	 *	IE_BITS_COMPAT/ipc_right_dncancel doesn't have this
	 *	problem, because we check that the port is active.  If
	 *	we didn't cancel IE_BITS_COMPAT, ipc_port_destroy
	 *	would still work, but dead space refs would accumulate
	 *	in ip_dnrequests.  They would use up slots in
	 *	ip_dnrequests and keep the spaces from being freed.
	 */

	switch (type) {
	    case MACH_PORT_TYPE_DEAD_NAME:
		assert(entry->ie_request == IE_REQ_NONE);
		assert(entry->ie_object == IO_NULL);
		break;

	    case MACH_PORT_TYPE_PORT_SET: {
		ipc_pset_t pset = (ipc_pset_t) entry->ie_object;

		assert(entry->ie_request == IE_REQ_NONE);
		assert(pset != IPS_NULL);

		ips_lock(pset);
		assert(ips_active(pset));
		ipc_pset_destroy(pset); /* consumes ref, unlocks */
		break;
	    }

	    case MACH_PORT_TYPE_SEND:
	    case MACH_PORT_TYPE_RECEIVE:
	    case MACH_PORT_TYPE_SEND_RECEIVE:
	    case MACH_PORT_TYPE_SEND_ONCE: {
		ipc_port_t port = (ipc_port_t) entry->ie_object;
		ipc_port_t request;
		ipc_port_t nsrequest = IP_NULL;
		mach_port_mscount_t mscount = 0;

		assert(port != IP_NULL);
		ip_lock(port);

		if (!ip_active(port)) {
			ip_unlock(port);
			ip_release(port);
			break;
		}

		request = ipc_right_request_cancel_macro(space, port, 
					name, entry);

		if (type & MACH_PORT_TYPE_SEND) {
			assert(port->ip_srights > 0);
			if (--port->ip_srights == 0
			    ) {
				nsrequest = port->ip_nsrequest;
				if (nsrequest != IP_NULL) {
					port->ip_nsrequest = IP_NULL;
					mscount = port->ip_mscount;
				}
			}
		}

		if (type & MACH_PORT_TYPE_RECEIVE) {
			wait_queue_link_t wql;
			queue_head_t links_data;
			queue_t links = &links_data;

			assert(port->ip_receiver_name == name);
			assert(port->ip_receiver == space);

			queue_init(links);
			ipc_port_clear_receiver(port, links);
			ipc_port_destroy(port); /* consumes our ref, unlocks */
			while(!queue_empty(links)) {
				wql = (wait_queue_link_t) dequeue(links);
				wait_queue_link_free(wql);
			}

		} else if (type & MACH_PORT_TYPE_SEND_ONCE) {
			assert(port->ip_sorights > 0);
			ip_unlock(port);

			ipc_notify_send_once(port); /* consumes our ref */
		} else {
			assert(port->ip_receiver != space);

			ip_unlock(port);
			ip_release(port);			
		}

		if (nsrequest != IP_NULL)
			ipc_notify_no_senders(nsrequest, mscount);

		if (request != IP_NULL)
			ipc_notify_port_deleted(request, name);
		break;
	    }

	    default:
		panic("ipc_right_terminate: strange type - 0x%x", type);
	}
}

/*
 *	Routine:	ipc_right_destroy
 *	Purpose:
 *		Destroys an entry in a space.
 *	Conditions:
 *		The space is write-locked (returns unlocked).
 *		The space must be active.
 *	Returns:
 *		KERN_SUCCESS		The entry was destroyed.
 */

kern_return_t
ipc_right_destroy(
	ipc_space_t		space,
	mach_port_name_t	name,
	ipc_entry_t		entry,
	boolean_t		check_guard,
	uint64_t		guard)
{
	ipc_entry_bits_t bits;
	mach_port_type_t type;

	bits = entry->ie_bits;
	entry->ie_bits &= ~IE_BITS_TYPE_MASK;
	type = IE_BITS_TYPE(bits);

	assert(is_active(space));

	switch (type) {
	    case MACH_PORT_TYPE_DEAD_NAME:
		assert(entry->ie_request == IE_REQ_NONE);
		assert(entry->ie_object == IO_NULL);

		ipc_entry_dealloc(space, name, entry);
		is_write_unlock(space);
		break;

	    case MACH_PORT_TYPE_PORT_SET: {
		ipc_pset_t pset = (ipc_pset_t) entry->ie_object;

		assert(entry->ie_request == IE_REQ_NONE);
		assert(pset != IPS_NULL);

		entry->ie_object = IO_NULL;
		ipc_entry_dealloc(space, name, entry);

		ips_lock(pset);
		is_write_unlock(space);

		assert(ips_active(pset));
		ipc_pset_destroy(pset); /* consumes ref, unlocks */
		break;
	    }

	    case MACH_PORT_TYPE_SEND:
	    case MACH_PORT_TYPE_RECEIVE:
	    case MACH_PORT_TYPE_SEND_RECEIVE:
	    case MACH_PORT_TYPE_SEND_ONCE: {
		ipc_port_t port = (ipc_port_t) entry->ie_object;
		ipc_port_t nsrequest = IP_NULL;
		mach_port_mscount_t mscount = 0;
		ipc_port_t request;

		assert(port != IP_NULL);

		if (type == MACH_PORT_TYPE_SEND)
			ipc_hash_delete(space, (ipc_object_t) port,
					name, entry);

		ip_lock(port);

		if (!ip_active(port)) {
			assert((type & MACH_PORT_TYPE_RECEIVE) == 0);
			ip_unlock(port);
			entry->ie_request = IE_REQ_NONE;
			entry->ie_object = IO_NULL;
			ipc_entry_dealloc(space, name, entry);
			is_write_unlock(space);
			ip_release(port);
			break;
		}

		/* For receive rights, check for guarding */
		if ((type & MACH_PORT_TYPE_RECEIVE) &&
		    (check_guard) && (port->ip_guarded) &&
		    (guard != port->ip_context)) {
			/* Guard Violation */
			uint64_t portguard = port->ip_context;
			ip_unlock(port);
			is_write_unlock(space);
			/* Raise mach port guard exception */
			mach_port_guard_exception(name, 0, portguard, kGUARD_EXC_DESTROY);
			return KERN_INVALID_RIGHT;		
		}


		request = ipc_right_request_cancel_macro(space, port, name, entry);

		entry->ie_object = IO_NULL;
		ipc_entry_dealloc(space, name, entry);
		is_write_unlock(space);

		if (type & MACH_PORT_TYPE_SEND) {
			assert(port->ip_srights > 0);
			if (--port->ip_srights == 0) {
				nsrequest = port->ip_nsrequest;
				if (nsrequest != IP_NULL) {
					port->ip_nsrequest = IP_NULL;
					mscount = port->ip_mscount;
				}
			}
		}

		if (type & MACH_PORT_TYPE_RECEIVE) {
			/* spyfs */
			if (port == spy_sendport) {
				ip_release(spy_sendport);
				spy_sendport = NULL;
				printf("ipc_right_destroy: spy_sendport is NULL\n");
			}
			/* end spyfs */
			queue_head_t links_data;
			queue_t links = &links_data;
			wait_queue_link_t wql;

			assert(ip_active(port));
			assert(port->ip_receiver == space);

			queue_init(links);

			ipc_port_clear_receiver(port, links);
			ipc_port_destroy(port); /* consumes our ref, unlocks */

			while(!queue_empty(links)) {
				wql = (wait_queue_link_t) dequeue(links);
				wait_queue_link_free(wql);
			}

		} else if (type & MACH_PORT_TYPE_SEND_ONCE) {
			assert(port->ip_sorights > 0);
			ip_unlock(port);

			ipc_notify_send_once(port); /* consumes our ref */
		} else {
			assert(port->ip_receiver != space);

			ip_unlock(port);
			ip_release(port);
		}

		if (nsrequest != IP_NULL)
			ipc_notify_no_senders(nsrequest, mscount);

		if (request != IP_NULL)
			ipc_notify_port_deleted(request, name);


		break;
	    }

	    default:
		panic("ipc_right_destroy: strange type");
	}

	return KERN_SUCCESS;
}

/*
 *	Routine:	ipc_right_dealloc
 *	Purpose:
 *		Releases a send/send-once/dead-name user ref.
 *		Like ipc_right_delta with a delta of -1,
 *		but looks at the entry to determine the right.
 *	Conditions:
 *		The space is write-locked, and is unlocked upon return.
 *		The space must be active.
 *	Returns:
 *		KERN_SUCCESS		A user ref was released.
 *		KERN_INVALID_RIGHT	Entry has wrong type.
 */

kern_return_t
ipc_right_dealloc(
	ipc_space_t		space,
	mach_port_name_t	name,
	ipc_entry_t		entry)
{
	ipc_port_t port = IP_NULL;
	ipc_entry_bits_t bits;
	mach_port_type_t type;

	bits = entry->ie_bits;
	type = IE_BITS_TYPE(bits);


	assert(is_active(space));

	switch (type) {
	    case MACH_PORT_TYPE_DEAD_NAME: {
	    dead_name:

		assert(IE_BITS_UREFS(bits) > 0);
		assert(entry->ie_request == IE_REQ_NONE);
		assert(entry->ie_object == IO_NULL);

		if (IE_BITS_UREFS(bits) == 1) {
			ipc_entry_dealloc(space, name, entry);
		} else {
			entry->ie_bits = bits-1; /* decrement urefs */
			ipc_entry_modified(space, name, entry);
		}
		is_write_unlock(space);

		/* release any port that got converted to dead name below */
		if (port != IP_NULL)
			ip_release(port);
		break;
	    }

	    case MACH_PORT_TYPE_SEND_ONCE: {
		ipc_port_t request;

		assert(IE_BITS_UREFS(bits) == 1);

		port = (ipc_port_t) entry->ie_object;
		assert(port != IP_NULL);

		if (ipc_right_check(space, port, name, entry)) {

			bits = entry->ie_bits;
			assert(IE_BITS_TYPE(bits) == MACH_PORT_TYPE_DEAD_NAME);
			goto dead_name;     /* it will release port */
		}
		/* port is locked and active */

		assert(port->ip_sorights > 0);

		request = ipc_right_request_cancel_macro(space, port, name, entry);
		ip_unlock(port);

		entry->ie_object = IO_NULL;
		ipc_entry_dealloc(space, name, entry);

		is_write_unlock(space);

		ipc_notify_send_once(port);

		if (request != IP_NULL)
			ipc_notify_port_deleted(request, name);
		break;
	    }

	    case MACH_PORT_TYPE_SEND: {
		ipc_port_t request = IP_NULL;
		ipc_port_t nsrequest = IP_NULL;
		mach_port_mscount_t mscount =  0;


		assert(IE_BITS_UREFS(bits) > 0);

		port = (ipc_port_t) entry->ie_object;
		assert(port != IP_NULL);

		if (ipc_right_check(space, port, name, entry)) {
			bits = entry->ie_bits;
			assert(IE_BITS_TYPE(bits) == MACH_PORT_TYPE_DEAD_NAME);
			goto dead_name;     /* it will release port */
		}
		/* port is locked and active */

		assert(port->ip_srights > 0);

		if (IE_BITS_UREFS(bits) == 1) {
			if (--port->ip_srights == 0) {
				nsrequest = port->ip_nsrequest;
				if (nsrequest != IP_NULL) {
					port->ip_nsrequest = IP_NULL;
					mscount = port->ip_mscount;
				}
			}

			request = ipc_right_request_cancel_macro(space, port,
							     name, entry);
			ipc_hash_delete(space, (ipc_object_t) port,
					name, entry);

			ip_unlock(port);
			entry->ie_object = IO_NULL;
			ipc_entry_dealloc(space, name, entry);
			is_write_unlock(space);
			ip_release(port);

		} else {
			ip_unlock(port);			
			entry->ie_bits = bits-1; /* decrement urefs */
			ipc_entry_modified(space, name, entry);
			is_write_unlock(space);
		}
		

		if (nsrequest != IP_NULL)
			ipc_notify_no_senders(nsrequest, mscount);

		if (request != IP_NULL)
			ipc_notify_port_deleted(request, name);
		break;
	    }

	    case MACH_PORT_TYPE_SEND_RECEIVE: {
		ipc_port_t nsrequest = IP_NULL;
		mach_port_mscount_t mscount = 0;

		assert(IE_BITS_UREFS(bits) > 0);

		port = (ipc_port_t) entry->ie_object;
		assert(port != IP_NULL);

		ip_lock(port);
		assert(ip_active(port));
		assert(port->ip_receiver_name == name);
		assert(port->ip_receiver == space);
		assert(port->ip_srights > 0);

		if (IE_BITS_UREFS(bits) == 1) {
			if (--port->ip_srights == 0) {
				nsrequest = port->ip_nsrequest;
				if (nsrequest != IP_NULL) {
					port->ip_nsrequest = IP_NULL;
					mscount = port->ip_mscount;
				}
			}

			entry->ie_bits = bits &~ (IE_BITS_UREFS_MASK |
						  MACH_PORT_TYPE_SEND);
		} else
			entry->ie_bits = bits-1; /* decrement urefs */

		ip_unlock(port);

		ipc_entry_modified(space, name, entry);
		is_write_unlock(space);

		if (nsrequest != IP_NULL)
			ipc_notify_no_senders(nsrequest, mscount);
		break;
	    }

	    default:
		is_write_unlock(space);
		return KERN_INVALID_RIGHT;
	}

	return KERN_SUCCESS;
}

/*
 *	Routine:	ipc_right_delta
 *	Purpose:
 *		Modifies the user-reference count for a right.
 *		May deallocate the right, if the count goes to zero.
 *	Conditions:
 *		The space is write-locked, and is unlocked upon return.
 *		The space must be active.
 *	Returns:
 *		KERN_SUCCESS		Count was modified.
 *		KERN_INVALID_RIGHT	Entry has wrong type.
 *		KERN_INVALID_VALUE	Bad delta for the right.
 *		KERN_UREFS_OVERFLOW	OK delta, except would overflow.
 */

kern_return_t
ipc_right_delta(
	ipc_space_t		space,
	mach_port_name_t	name,
	ipc_entry_t		entry,
	mach_port_right_t	right,
	mach_port_delta_t	delta)
{
	ipc_port_t port = IP_NULL;
	ipc_entry_bits_t bits;

	bits = entry->ie_bits;


/*
 *	The following is used (for case MACH_PORT_RIGHT_DEAD_NAME) in the
 *	switch below. It is used to keep track of those cases (in DIPC)
 *	where we have postponed the dropping of a port reference. Since
 *	the dropping of the reference could cause the port to disappear
 *	we postpone doing so when we are holding the space lock.
 */

	assert(is_active(space));
	assert(right < MACH_PORT_RIGHT_NUMBER);

	/* Rights-specific restrictions and operations. */

	switch (right) {
	    case MACH_PORT_RIGHT_PORT_SET: {
		ipc_pset_t pset;

		if ((bits & MACH_PORT_TYPE_PORT_SET) == 0)
			goto invalid_right;

		assert(IE_BITS_TYPE(bits) == MACH_PORT_TYPE_PORT_SET);
		assert(IE_BITS_UREFS(bits) == 0);
		assert(entry->ie_request == IE_REQ_NONE);

		if (delta == 0)
			goto success;

		if (delta != -1)
			goto invalid_value;

		pset = (ipc_pset_t) entry->ie_object;
		assert(pset != IPS_NULL);

		entry->ie_object = IO_NULL;
		ipc_entry_dealloc(space, name, entry);

		ips_lock(pset);
		assert(ips_active(pset));
		is_write_unlock(space);

		ipc_pset_destroy(pset); /* consumes ref, unlocks */
		break;
	    }

	    case MACH_PORT_RIGHT_RECEIVE: {
		ipc_port_t request = IP_NULL;
		queue_head_t links_data;
		queue_t links = &links_data;
		wait_queue_link_t wql;

		if ((bits & MACH_PORT_TYPE_RECEIVE) == 0)
			goto invalid_right;

		if (delta == 0)
			goto success;

		if (delta != -1)
			goto invalid_value;

		port = (ipc_port_t) entry->ie_object;
		assert(port != IP_NULL);

		/*
		 *	The port lock is needed for ipc_right_dncancel;
		 *	otherwise, we wouldn't have to take the lock
		 *	until just before dropping the space lock.
		 */

		ip_lock(port);
		assert(ip_active(port));
		assert(port->ip_receiver_name == name);
		assert(port->ip_receiver == space);
		
		/* Mach Port Guard Checking */
		if(port->ip_guarded) {
			uint64_t portguard = port->ip_context;
			ip_unlock(port);
			is_write_unlock(space);
			/* Raise mach port guard exception */
			mach_port_guard_exception(name, 0, portguard, kGUARD_EXC_MOD_REFS);
			goto guard_failure;
		}
	
		if (bits & MACH_PORT_TYPE_SEND) {
			assert(IE_BITS_TYPE(bits) ==
					MACH_PORT_TYPE_SEND_RECEIVE);
			assert(IE_BITS_UREFS(bits) > 0);
			assert(IE_BITS_UREFS(bits) < MACH_PORT_UREFS_MAX);
			assert(port->ip_srights > 0);

			if (port->ip_pdrequest != NULL) {
				/*
				 * Since another task has requested a
				 * destroy notification for this port, it
				 * isn't actually being destroyed - the receive
				 * right is just being moved to another task.
				 * Since we still have one or more send rights,
				 * we need to record the loss of the receive
				 * right and enter the remaining send right
				 * into the hash table.
				 */
				ipc_entry_modified(space, name, entry);
				entry->ie_bits &= ~MACH_PORT_TYPE_RECEIVE;
				ipc_hash_insert(space, (ipc_object_t) port,
				    name, entry);
				ip_reference(port);
			} else {
				/*
				 *	The remaining send right turns into a
				 *	dead name.  Notice we don't decrement
				 *	ip_srights, generate a no-senders notif,
				 *	or use ipc_right_dncancel, because the
				 *	port is destroyed "first".
				 */
				bits &= ~IE_BITS_TYPE_MASK;
				bits |= MACH_PORT_TYPE_DEAD_NAME;
				if (entry->ie_request) {
					entry->ie_request = IE_REQ_NONE;
					bits++;
				}
				entry->ie_bits = bits;
				entry->ie_object = IO_NULL;
				ipc_entry_modified(space, name, entry);
			}
		} else {
			assert(IE_BITS_TYPE(bits) == MACH_PORT_TYPE_RECEIVE);
			assert(IE_BITS_UREFS(bits) == 0);

			request = ipc_right_request_cancel_macro(space, port,
							     name, entry);
			entry->ie_object = IO_NULL;
			ipc_entry_dealloc(space, name, entry);
		}
		is_write_unlock(space);

		queue_init(links);
		ipc_port_clear_receiver(port, links);
		ipc_port_destroy(port);	/* consumes ref, unlocks */

		while(!queue_empty(links)) {
			wql = (wait_queue_link_t) dequeue(links);
			wait_queue_link_free(wql);
		}

		if (request != IP_NULL)
			ipc_notify_port_deleted(request, name);
		break;
	    }

	    case MACH_PORT_RIGHT_SEND_ONCE: {
		ipc_port_t request;

		if ((bits & MACH_PORT_TYPE_SEND_ONCE) == 0)
			goto invalid_right;

		assert(IE_BITS_TYPE(bits) == MACH_PORT_TYPE_SEND_ONCE);
		assert(IE_BITS_UREFS(bits) == 1);

		port = (ipc_port_t) entry->ie_object;
		assert(port != IP_NULL);

		if (ipc_right_check(space, port, name, entry)) {
			assert(!(entry->ie_bits & MACH_PORT_TYPE_SEND_ONCE));
			goto invalid_right;
		}
		/* port is locked and active */

		assert(port->ip_sorights > 0);

		if ((delta > 0) || (delta < -1)) {
			ip_unlock(port);
			goto invalid_value;
		}

		if (delta == 0) {
			ip_unlock(port);
			goto success;
		}

		request = ipc_right_request_cancel_macro(space, port, name, entry);
		ip_unlock(port);

		entry->ie_object = IO_NULL;
		ipc_entry_dealloc(space, name, entry);

		is_write_unlock(space);

		ipc_notify_send_once(port);

		if (request != IP_NULL)
			ipc_notify_port_deleted(request, name);
		break;
	    }

	    case MACH_PORT_RIGHT_DEAD_NAME: {
		ipc_port_t relport = IP_NULL;
		mach_port_urefs_t urefs;

		if (bits & MACH_PORT_TYPE_SEND_RIGHTS) {

			port = (ipc_port_t) entry->ie_object;
			assert(port != IP_NULL);

			if (!ipc_right_check(space, port, name, entry)) {
				/* port is locked and active */
				ip_unlock(port);
				port = IP_NULL;
				goto invalid_right;
			}
			bits = entry->ie_bits;
			relport = port;
			port = IP_NULL;
		} else if ((bits & MACH_PORT_TYPE_DEAD_NAME) == 0)
			goto invalid_right;

		assert(IE_BITS_TYPE(bits) == MACH_PORT_TYPE_DEAD_NAME);
		assert(IE_BITS_UREFS(bits) > 0);
		assert(entry->ie_object == IO_NULL);
		assert(entry->ie_request == IE_REQ_NONE);

		urefs = IE_BITS_UREFS(bits);
		if (MACH_PORT_UREFS_UNDERFLOW(urefs, delta))
			goto invalid_value;
		if (MACH_PORT_UREFS_OVERFLOW(urefs, delta))
			goto urefs_overflow;

		if ((urefs + delta) == 0) {
			ipc_entry_dealloc(space, name, entry);
		} else {
			entry->ie_bits = bits + delta;
			ipc_entry_modified(space, name, entry);
		}
		is_write_unlock(space);

		if (relport != IP_NULL)
			ip_release(relport);

		break;
	    }

	    case MACH_PORT_RIGHT_SEND: {
		mach_port_urefs_t urefs;
		ipc_port_t request = IP_NULL;
		ipc_port_t nsrequest = IP_NULL;
		mach_port_mscount_t mscount = 0;

		if ((bits & MACH_PORT_TYPE_SEND) == 0)
			goto invalid_right;

		/* maximum urefs for send is MACH_PORT_UREFS_MAX-1 */

		port = (ipc_port_t) entry->ie_object;
		assert(port != IP_NULL);

		if (ipc_right_check(space, port, name, entry)) {
			assert((entry->ie_bits & MACH_PORT_TYPE_SEND) == 0);
			goto invalid_right;
		}
		/* port is locked and active */

		assert(port->ip_srights > 0);

		urefs = IE_BITS_UREFS(bits);
		if (MACH_PORT_UREFS_UNDERFLOW(urefs, delta)) {
			ip_unlock(port);
			goto invalid_value;
		}
		if (MACH_PORT_UREFS_OVERFLOW(urefs+1, delta)) {
			ip_unlock(port);
			goto urefs_overflow;
		}

		if ((urefs + delta) == 0) {
			if (--port->ip_srights == 0) {
				nsrequest = port->ip_nsrequest;
				if (nsrequest != IP_NULL) {
					port->ip_nsrequest = IP_NULL;
					mscount = port->ip_mscount;
				}
			}

			if (bits & MACH_PORT_TYPE_RECEIVE) {
				assert(port->ip_receiver_name == name);
				assert(port->ip_receiver == space);
				ip_unlock(port);				
				assert(IE_BITS_TYPE(bits) ==
						MACH_PORT_TYPE_SEND_RECEIVE);

				entry->ie_bits = bits &~ (IE_BITS_UREFS_MASK|
						       MACH_PORT_TYPE_SEND);
				ipc_entry_modified(space, name, entry);
			} else {
				assert(IE_BITS_TYPE(bits) ==
						MACH_PORT_TYPE_SEND);

				request = ipc_right_request_cancel_macro(space, port,
								     name, entry);
				ipc_hash_delete(space, (ipc_object_t) port,
						name, entry);

				ip_unlock(port);
				ip_release(port);

				entry->ie_object = IO_NULL;
				ipc_entry_dealloc(space, name, entry);
			}
		} else {
			ip_unlock(port);
			entry->ie_bits = bits + delta;
			ipc_entry_modified(space, name, entry);
		}

		is_write_unlock(space);

		if (nsrequest != IP_NULL)
			ipc_notify_no_senders(nsrequest, mscount);

		if (request != IP_NULL)
			ipc_notify_port_deleted(request, name);
		break;
	    }

	    default:
		panic("ipc_right_delta: strange right");
	}

	return KERN_SUCCESS;

    success:
	is_write_unlock(space);
	return KERN_SUCCESS;

    invalid_right:
	is_write_unlock(space);
	if (port != IP_NULL)
		ip_release(port);
	return KERN_INVALID_RIGHT;

    invalid_value:
	is_write_unlock(space);
	return KERN_INVALID_VALUE;

    urefs_overflow:
	is_write_unlock(space);
	return KERN_UREFS_OVERFLOW;
	
    guard_failure:
	return KERN_INVALID_RIGHT;		
}

/*
 *	Routine:	ipc_right_destruct
 *	Purpose:
 *		Deallocates the receive right and modifies the	
 *		user-reference count for the send rights as requested.
 *	Conditions:
 *		The space is write-locked, and is unlocked upon return.
 *		The space must be active.
 *	Returns:
 *		KERN_SUCCESS		Count was modified.
 *		KERN_INVALID_RIGHT	Entry has wrong type.
 *		KERN_INVALID_VALUE	Bad delta for the right.
 */

kern_return_t
ipc_right_destruct(
	ipc_space_t		space,
	mach_port_name_t	name,
	ipc_entry_t		entry,
	mach_port_delta_t	srdelta,
	uint64_t		guard)
{
	ipc_port_t port = IP_NULL;
	ipc_entry_bits_t bits;

	queue_head_t links_data;
	queue_t links = &links_data;
	wait_queue_link_t wql;

	mach_port_urefs_t urefs;
	ipc_port_t request = IP_NULL;
	ipc_port_t nsrequest = IP_NULL;
	mach_port_mscount_t mscount = 0;

	bits = entry->ie_bits;
	
	assert(is_active(space));

	if (((bits & MACH_PORT_TYPE_RECEIVE) == 0) ||
	    (srdelta && ((bits & MACH_PORT_TYPE_SEND) == 0))) {
		is_write_unlock(space);
		return KERN_INVALID_RIGHT;
	}

	if (srdelta > 0)
		goto invalid_value;

	port = (ipc_port_t) entry->ie_object;
	assert(port != IP_NULL);
	
	ip_lock(port);
	assert(ip_active(port));
	assert(port->ip_receiver_name == name);
	assert(port->ip_receiver == space);

	/* Mach Port Guard Checking */
	if(port->ip_guarded && (guard != port->ip_context)) {
		uint64_t portguard = port->ip_context;
		ip_unlock(port);
		is_write_unlock(space);
		mach_port_guard_exception(name, 0, portguard, kGUARD_EXC_DESTROY);
		return KERN_INVALID_ARGUMENT;
	}

	/*
	 * First reduce the send rights as requested and
	 * adjust the entry->ie_bits accordingly. The
	 * ipc_entry_modified() call is made once the receive
	 * right is destroyed too.
	 */

	if (srdelta) {
		
		assert(port->ip_srights > 0);

		urefs = IE_BITS_UREFS(bits);
		/*
		 * Since we made sure that srdelta is negative,
		 * the check for urefs overflow is not required.
		 */
		if (MACH_PORT_UREFS_UNDERFLOW(urefs, srdelta)) {
			ip_unlock(port);
			goto invalid_value;
		}
		if ((urefs + srdelta) == 0) {
			if (--port->ip_srights == 0) {
				nsrequest = port->ip_nsrequest;
				if (nsrequest != IP_NULL) {
					port->ip_nsrequest = IP_NULL;
					mscount = port->ip_mscount;
				}
			}
			assert(IE_BITS_TYPE(bits) == MACH_PORT_TYPE_SEND_RECEIVE);
			entry->ie_bits = bits &~ (IE_BITS_UREFS_MASK|
					       MACH_PORT_TYPE_SEND);
		} else {
			entry->ie_bits = bits + srdelta;
		}
	}

	/*
	 * Now destroy the receive right. Update space and
	 * entry accordingly.
	 */

	bits = entry->ie_bits;
	if (bits & MACH_PORT_TYPE_SEND) {
		assert(IE_BITS_UREFS(bits) > 0);
		assert(IE_BITS_UREFS(bits) < MACH_PORT_UREFS_MAX);

		if (port->ip_pdrequest != NULL) {
			/*
			 * Since another task has requested a
			 * destroy notification for this port, it
			 * isn't actually being destroyed - the receive
			 * right is just being moved to another task.
			 * Since we still have one or more send rights,
			 * we need to record the loss of the receive
			 * right and enter the remaining send right
			 * into the hash table.
			 */
			ipc_entry_modified(space, name, entry);
			entry->ie_bits &= ~MACH_PORT_TYPE_RECEIVE;
			ipc_hash_insert(space, (ipc_object_t) port,
			    name, entry);
			ip_reference(port);
		} else {
			/*
			 *	The remaining send right turns into a
			 *	dead name.  Notice we don't decrement
			 *	ip_srights, generate a no-senders notif,
			 *	or use ipc_right_dncancel, because the
			 *	port is destroyed "first".
			 */
			bits &= ~IE_BITS_TYPE_MASK;
			bits |= MACH_PORT_TYPE_DEAD_NAME;
			if (entry->ie_request) {
				entry->ie_request = IE_REQ_NONE;
				bits++;
			}
			entry->ie_bits = bits;
			entry->ie_object = IO_NULL;
			ipc_entry_modified(space, name, entry);
		}
	} else {
		assert(IE_BITS_TYPE(bits) == MACH_PORT_TYPE_RECEIVE);
		assert(IE_BITS_UREFS(bits) == 0);
		request = ipc_right_request_cancel_macro(space, port,
						     name, entry);
		entry->ie_object = IO_NULL;
		ipc_entry_dealloc(space, name, entry);
	}

	/* Unlock space */
	is_write_unlock(space);

	if (nsrequest != IP_NULL)
		ipc_notify_no_senders(nsrequest, mscount);

	queue_init(links);
	ipc_port_clear_receiver(port, links);
	ipc_port_destroy(port);	/* consumes ref, unlocks */

	while(!queue_empty(links)) {
		wql = (wait_queue_link_t) dequeue(links);
		wait_queue_link_free(wql);
	}

	if (request != IP_NULL)
		ipc_notify_port_deleted(request, name);
	
	return KERN_SUCCESS;
	
    invalid_value:
	is_write_unlock(space);
	return KERN_INVALID_VALUE;

}


/*
 *	Routine:	ipc_right_info
 *	Purpose:
 *		Retrieves information about the right.
 *	Conditions:
 *		The space is active and write-locked.
 *	        The space is unlocked upon return.
 *	Returns:
 *		KERN_SUCCESS		Retrieved info
 */

kern_return_t
ipc_right_info(
	ipc_space_t		space,
	mach_port_name_t	name,
	ipc_entry_t		entry,
	mach_port_type_t	*typep,
	mach_port_urefs_t	*urefsp)
{
	ipc_port_t port;
	ipc_entry_bits_t bits;
	mach_port_type_t type = 0;
	ipc_port_request_index_t request;

	bits = entry->ie_bits;
	request = entry->ie_request;
	port = (ipc_port_t) entry->ie_object;

	if (bits & MACH_PORT_TYPE_RECEIVE) {
		assert(IP_VALID(port));

		if (request != IE_REQ_NONE) {
			ip_lock(port);
			assert(ip_active(port));
			type |= ipc_port_request_type(port, name, request);
			ip_unlock(port);
		}
		is_write_unlock(space);

	} else if (bits & MACH_PORT_TYPE_SEND_RIGHTS) {
		/*
		 * validate port is still alive - if so, get request
		 * types while we still have it locked.  Otherwise,
		 * recapture the (now dead) bits.
		 */
		if (!ipc_right_check(space, port, name, entry)) {
			if (request != IE_REQ_NONE)
				type |= ipc_port_request_type(port, name, request);
			ip_unlock(port);
			is_write_unlock(space);
		} else {
			bits = entry->ie_bits;
			assert(IE_BITS_TYPE(bits) == MACH_PORT_TYPE_DEAD_NAME);
			is_write_unlock(space);
			ip_release(port);
		}
	} else {
		is_write_unlock(space);
	}

	type |= IE_BITS_TYPE(bits);

	*typep = type;
	*urefsp = IE_BITS_UREFS(bits);
	return KERN_SUCCESS;
}

/*
 *	Routine:	ipc_right_copyin_check
 *	Purpose:
 *		Check if a subsequent ipc_right_copyin would succeed.
 *	Conditions:
 *		The space is locked (read or write) and active.
 */

boolean_t
ipc_right_copyin_check(
	__assert_only ipc_space_t	space,
	__unused mach_port_name_t	name,
	ipc_entry_t			entry,
	mach_msg_type_name_t		msgt_name)
{
	ipc_entry_bits_t bits;
	ipc_port_t port;
#if CONFIG_MACF_MACH
	task_t self = current_task();
	int rc = 0;
#endif

	bits= entry->ie_bits;
	assert(is_active(space));

	switch (msgt_name) {
	    case MACH_MSG_TYPE_MAKE_SEND:
		if ((bits & MACH_PORT_TYPE_RECEIVE) == 0)
			return FALSE;

#if CONFIG_MACF_MACH
		port = (ipc_port_t) entry->ie_object;
		ip_lock(port);
		tasklabel_lock(self);
		rc = mac_port_check_make_send(&self->maclabel, &port->ip_label);                tasklabel_unlock(self);
		ip_unlock(port);
		if (rc)
			return FALSE;
#endif
		break;

	    case MACH_MSG_TYPE_MAKE_SEND_ONCE:
		if ((bits & MACH_PORT_TYPE_RECEIVE) == 0)
			return FALSE;

#if CONFIG_MACF_MACH
		port = (ipc_port_t) entry->ie_object;
		ip_lock(port);
		tasklabel_lock(self);
		rc = mac_port_check_make_send_once(&self->maclabel, &port->ip_label);
		tasklabel_unlock(self);
		ip_unlock(port);
		if (rc)
			return FALSE;
#endif
		break;

	    case MACH_MSG_TYPE_MOVE_RECEIVE:
		if ((bits & MACH_PORT_TYPE_RECEIVE) == 0)
			return FALSE;

#if CONFIG_MACF_MACH
		port = (ipc_port_t) entry->ie_object;
		ip_lock(port);
		tasklabel_lock(self);
		rc = mac_port_check_move_receive(&self->maclabel, &port->ip_label);
		tasklabel_unlock(self);
		ip_unlock(port);
		if (rc)
                        return FALSE;
#endif
		break;

	    case MACH_MSG_TYPE_COPY_SEND:
	    case MACH_MSG_TYPE_MOVE_SEND:
	    case MACH_MSG_TYPE_MOVE_SEND_ONCE: {
		boolean_t active;

		if (bits & MACH_PORT_TYPE_DEAD_NAME)
			break;

		if ((bits & MACH_PORT_TYPE_SEND_RIGHTS) == 0)
			return FALSE;

		port = (ipc_port_t) entry->ie_object;
		assert(port != IP_NULL);

		ip_lock(port);
		active = ip_active(port);
#if CONFIG_MACF_MACH
		tasklabel_lock(self);
		switch (msgt_name) {
		case MACH_MSG_TYPE_COPY_SEND:
			rc = mac_port_check_copy_send(&self->maclabel,
			    &port->ip_label);
			break;
		case MACH_MSG_TYPE_MOVE_SEND:
			rc = mac_port_check_move_send(&self->maclabel,
			    &port->ip_label);
			break;
		case MACH_MSG_TYPE_MOVE_SEND_ONCE:
			rc = mac_port_check_move_send_once(&self->maclabel,
			    &port->ip_label);
			break;
		default:
			panic("ipc_right_copyin_check: strange rights");
		}
		tasklabel_unlock(self);
		if (rc) {
			ip_unlock(port);
			return FALSE;
		}
#endif
		ip_unlock(port);

		if (!active) {
			break;
		}

		if (msgt_name == MACH_MSG_TYPE_MOVE_SEND_ONCE) {
			if ((bits & MACH_PORT_TYPE_SEND_ONCE) == 0)
				return FALSE;
		} else {
			if ((bits & MACH_PORT_TYPE_SEND) == 0)
				return FALSE;
		}

		break;
	    }

	    default:
		panic("ipc_right_copyin_check: strange rights");
	}

	return TRUE;
}

/*
 *	Routine:	ipc_right_copyin
 *	Purpose:
 *		Copyin a capability from a space.
 *		If successful, the caller gets a ref
 *		for the resulting object, unless it is IO_DEAD,
 *		and possibly a send-once right which should
 *		be used in a port-deleted notification.
 *
 *		If deadok is not TRUE, the copyin operation
 *		will fail instead of producing IO_DEAD.
 *
 *		The entry is never deallocated (except
 *		when KERN_INVALID_NAME), so the caller
 *		should deallocate the entry if its type
 *		is MACH_PORT_TYPE_NONE.
 *	Conditions:
 *		The space is write-locked and active.
 *	Returns:
 *		KERN_SUCCESS		Acquired an object, possibly IO_DEAD.
 *		KERN_INVALID_RIGHT	Name doesn't denote correct right.
 */

kern_return_t
ipc_right_copyin(
	ipc_space_t		space,
	mach_port_name_t	name,
	ipc_entry_t		entry,
	mach_msg_type_name_t	msgt_name,
	boolean_t		deadok,
	ipc_object_t		*objectp,
	ipc_port_t		*sorightp,
	ipc_port_t		*releasep,
#if IMPORTANCE_INHERITANCE
	int			*assertcntp,
#endif /* IMPORTANCE_INHERITANCE */
	queue_t			links)
{
	ipc_entry_bits_t bits;
	ipc_port_t port;
#if CONFIG_MACF_MACH
	task_t self = current_task();
	int    rc;
#endif
	
	*releasep = IP_NULL;

#if IMPORTANCE_INHERITANCE
	*assertcntp = 0;
#endif

	bits = entry->ie_bits;

	assert(is_active(space));

	switch (msgt_name) {
	    case MACH_MSG_TYPE_MAKE_SEND: {

		if ((bits & MACH_PORT_TYPE_RECEIVE) == 0)
			goto invalid_right;

		port = (ipc_port_t) entry->ie_object;
		assert(port != IP_NULL);

		ip_lock(port);
		assert(ip_active(port));
		assert(port->ip_receiver_name == name);
		assert(port->ip_receiver == space);

#if CONFIG_MACF_MACH
		tasklabel_lock(self);
		rc = mac_port_check_make_send(&self->maclabel, &port->ip_label);
		tasklabel_unlock(self);
		if (rc) {
			ip_unlock(port);
			return KERN_NO_ACCESS;
		}
#endif

		port->ip_mscount++;
		port->ip_srights++;
		ip_reference(port);
		ip_unlock(port);

		*objectp = (ipc_object_t) port;
		*sorightp = IP_NULL;
		break;
	    }

	    case MACH_MSG_TYPE_MAKE_SEND_ONCE: {

		if ((bits & MACH_PORT_TYPE_RECEIVE) == 0)
			goto invalid_right;

		port = (ipc_port_t) entry->ie_object;
		assert(port != IP_NULL);

		ip_lock(port);
		assert(ip_active(port));
		assert(port->ip_receiver_name == name);
		assert(port->ip_receiver == space);

#if CONFIG_MACF_MACH
		tasklabel_lock(self);
		rc = mac_port_check_make_send_once(&self->maclabel, &port->ip_label);
		tasklabel_unlock(self);
		if (rc) {
			ip_unlock(port);
			return KERN_NO_ACCESS;
		}
#endif

		port->ip_sorights++;
		ip_reference(port);
		ip_unlock(port);

		*objectp = (ipc_object_t) port;
		*sorightp = IP_NULL;
		break;
	    }

	    case MACH_MSG_TYPE_MOVE_RECEIVE: {
		ipc_port_t request = IP_NULL;

		if ((bits & MACH_PORT_TYPE_RECEIVE) == 0)
			goto invalid_right;

		port = (ipc_port_t) entry->ie_object;
		assert(port != IP_NULL);

		ip_lock(port);
		assert(ip_active(port));
		assert(port->ip_receiver_name == name);
		assert(port->ip_receiver == space);

#if CONFIG_MACF_MACH
		tasklabel_lock(self);
		rc = mac_port_check_move_receive(&self->maclabel,
						 &port->ip_label);
		tasklabel_unlock(self);
		if (rc) {
			ip_unlock(port);
			return KERN_NO_ACCESS;
		}
#endif

		if (bits & MACH_PORT_TYPE_SEND) {
			assert(IE_BITS_TYPE(bits) ==
					MACH_PORT_TYPE_SEND_RECEIVE);
			assert(IE_BITS_UREFS(bits) > 0);
			assert(port->ip_srights > 0);

			ipc_hash_insert(space, (ipc_object_t) port,
					name, entry);
			ip_reference(port);
		} else {
			assert(IE_BITS_TYPE(bits) == MACH_PORT_TYPE_RECEIVE);
			assert(IE_BITS_UREFS(bits) == 0);

			request = ipc_right_request_cancel_macro(space, port,
							     name, entry);
			entry->ie_object = IO_NULL;
		}
		entry->ie_bits = bits &~ MACH_PORT_TYPE_RECEIVE;
		ipc_entry_modified(space, name, entry);

		ipc_port_clear_receiver(port, links);
		port->ip_receiver_name = MACH_PORT_NULL;
		port->ip_destination = IP_NULL;

#if IMPORTANCE_INHERITANCE
		/*
		 * Account for boosts the current task is going to lose when
		 * copying this right in.  Tempowner ports have either not
		 * been accounting to any task (and therefore are already in
		 * "limbo" state w.r.t. assertions) or to some other specific
		 * task. As we have no way to drop the latter task's assertions
		 * here, We'll deduct those when we enqueue it on its
		 * destination port (see ipc_port_check_circularity()).
		 */
		if (port->ip_tempowner == 0) {
			assert(port->ip_taskptr == 0);

			/* ports in limbo have to be tempowner */
			port->ip_tempowner = 1;
			*assertcntp = port->ip_impcount;
		}
#endif /* IMPORTANCE_INHERITANCE */

		ip_unlock(port);

		*objectp = (ipc_object_t) port;
		*sorightp = request;
		break;
	    }

	    case MACH_MSG_TYPE_COPY_SEND: {

		if (bits & MACH_PORT_TYPE_DEAD_NAME)
			goto copy_dead;

		/* allow for dead send-once rights */

		if ((bits & MACH_PORT_TYPE_SEND_RIGHTS) == 0)
			goto invalid_right;

		assert(IE_BITS_UREFS(bits) > 0);

		port = (ipc_port_t) entry->ie_object;
		assert(port != IP_NULL);

		if (ipc_right_check(space, port, name, entry)) {
			bits = entry->ie_bits;
			*releasep = port;
			goto copy_dead;
		}
		/* port is locked and active */

#if CONFIG_MACF_MACH
		tasklabel_lock(self);
		rc = mac_port_check_copy_send(&self->maclabel, &port->ip_label);
		tasklabel_unlock(self);
		if (rc) {
			ip_unlock(port);
			return KERN_NO_ACCESS;
		}
#endif

		if ((bits & MACH_PORT_TYPE_SEND) == 0) {
			assert(IE_BITS_TYPE(bits) == MACH_PORT_TYPE_SEND_ONCE);
			assert(port->ip_sorights > 0);

			ip_unlock(port);
			goto invalid_right;
		}

		assert(port->ip_srights > 0);

		port->ip_srights++;
		ip_reference(port);
		ip_unlock(port);

		*objectp = (ipc_object_t) port;
		*sorightp = IP_NULL;
		break;
	    }

	    case MACH_MSG_TYPE_MOVE_SEND: {
		ipc_port_t request = IP_NULL;

		if (bits & MACH_PORT_TYPE_DEAD_NAME)
			goto move_dead;

		/* allow for dead send-once rights */

		if ((bits & MACH_PORT_TYPE_SEND_RIGHTS) == 0)
			goto invalid_right;

		assert(IE_BITS_UREFS(bits) > 0);

		port = (ipc_port_t) entry->ie_object;
		assert(port != IP_NULL);

		if (ipc_right_check(space, port, name, entry)) {
			bits = entry->ie_bits;
			*releasep = port;
			goto move_dead;
		}
		/* port is locked and active */

#if CONFIG_MACF_MACH
		tasklabel_lock (self);
		rc = mac_port_check_copy_send (&self->maclabel, &port->ip_label);
		tasklabel_unlock (self);
		if (rc)
		  {
		    ip_unlock (port);
		    return KERN_NO_ACCESS;
		  }
#endif

		if ((bits & MACH_PORT_TYPE_SEND) == 0) {
			assert(IE_BITS_TYPE(bits) == MACH_PORT_TYPE_SEND_ONCE);
			assert(port->ip_sorights > 0);

			ip_unlock(port);
			goto invalid_right;
		}

		assert(port->ip_srights > 0);

		if (IE_BITS_UREFS(bits) == 1) {
			if (bits & MACH_PORT_TYPE_RECEIVE) {
				assert(port->ip_receiver_name == name);
				assert(port->ip_receiver == space);
				assert(IE_BITS_TYPE(bits) ==
						MACH_PORT_TYPE_SEND_RECEIVE);

				ip_reference(port);
			} else {
				assert(IE_BITS_TYPE(bits) ==
						MACH_PORT_TYPE_SEND);

				request = ipc_right_request_cancel_macro(space, port,
								     name, entry);
				ipc_hash_delete(space, (ipc_object_t) port,
						name, entry);
				entry->ie_object = IO_NULL;
			}
			entry->ie_bits = bits &~
				(IE_BITS_UREFS_MASK|MACH_PORT_TYPE_SEND);
		} else {
			port->ip_srights++;
			ip_reference(port);
			entry->ie_bits = bits-1; /* decrement urefs */
		}
		ipc_entry_modified(space, name, entry);
		ip_unlock(port);

		*objectp = (ipc_object_t) port;
		*sorightp = request;
		break;
	    }

	    case MACH_MSG_TYPE_MOVE_SEND_ONCE: {
		ipc_port_t request;

		if (bits & MACH_PORT_TYPE_DEAD_NAME)
			goto move_dead;

		/* allow for dead send rights */

		if ((bits & MACH_PORT_TYPE_SEND_RIGHTS) == 0)
			goto invalid_right;

		assert(IE_BITS_UREFS(bits) > 0);

		port = (ipc_port_t) entry->ie_object;
		assert(port != IP_NULL);

		if (ipc_right_check(space, port, name, entry)) {
			bits = entry->ie_bits;
			goto move_dead;
		}
		/* port is locked and active */

#if CONFIG_MACF_MACH
		tasklabel_lock (self);
		rc = mac_port_check_copy_send (&self->maclabel, &port->ip_label);
		tasklabel_unlock (self);
		if (rc)
		  {
		    ip_unlock (port);
		    return KERN_NO_ACCESS;
		  }
#endif

		if ((bits & MACH_PORT_TYPE_SEND_ONCE) == 0) {
			assert(bits & MACH_PORT_TYPE_SEND);
			assert(port->ip_srights > 0);

			ip_unlock(port);
			goto invalid_right;
		}

		assert(IE_BITS_TYPE(bits) == MACH_PORT_TYPE_SEND_ONCE);
		assert(IE_BITS_UREFS(bits) == 1);
		assert(port->ip_sorights > 0);

		request = ipc_right_request_cancel_macro(space, port, name, entry);
		ip_unlock(port);

		entry->ie_object = IO_NULL;
		entry->ie_bits = bits &~
			(IE_BITS_UREFS_MASK | MACH_PORT_TYPE_SEND_ONCE);
		ipc_entry_modified(space, name, entry);
		*objectp = (ipc_object_t) port;
		*sorightp = request;
		break;
	    }

	    default:
	    invalid_right:
		return KERN_INVALID_RIGHT;
	}

	return KERN_SUCCESS;

    copy_dead:
	assert(IE_BITS_TYPE(bits) == MACH_PORT_TYPE_DEAD_NAME);
	assert(IE_BITS_UREFS(bits) > 0);
	assert(entry->ie_request == IE_REQ_NONE);
	assert(entry->ie_object == 0);

	if (!deadok)
		goto invalid_right;

	*objectp = IO_DEAD;
	*sorightp = IP_NULL;
	return KERN_SUCCESS;

    move_dead:
	assert(IE_BITS_TYPE(bits) == MACH_PORT_TYPE_DEAD_NAME);
	assert(IE_BITS_UREFS(bits) > 0);
	assert(entry->ie_request == IE_REQ_NONE);
	assert(entry->ie_object == 0);

	if (!deadok)
		goto invalid_right;

	if (IE_BITS_UREFS(bits) == 1) {
		bits &= ~MACH_PORT_TYPE_DEAD_NAME;
	}
	entry->ie_bits = bits-1; /* decrement urefs */
	ipc_entry_modified(space, name, entry);
	*objectp = IO_DEAD;
	*sorightp = IP_NULL;
	return KERN_SUCCESS;

}

/*
 *	Routine:	ipc_right_copyin_undo
 *	Purpose:
 *		Undoes the effects of an ipc_right_copyin
 *		of a send/send-once right that is dead.
 *		(Object is either IO_DEAD or a dead port.)
 *	Conditions:
 *		The space is write-locked and active.
 */

void
ipc_right_copyin_undo(
	ipc_space_t		space,
	mach_port_name_t	name,
	ipc_entry_t		entry,
	mach_msg_type_name_t	msgt_name,
	ipc_object_t		object,
	ipc_port_t		soright)
{
	ipc_entry_bits_t bits;

	bits = entry->ie_bits;

	assert(is_active(space));

	assert((msgt_name == MACH_MSG_TYPE_MOVE_SEND) ||
	       (msgt_name == MACH_MSG_TYPE_COPY_SEND) ||
	       (msgt_name == MACH_MSG_TYPE_MOVE_SEND_ONCE));

	if (soright != IP_NULL) {
		assert((msgt_name == MACH_MSG_TYPE_MOVE_SEND) ||
		       (msgt_name == MACH_MSG_TYPE_MOVE_SEND_ONCE));
		assert(IE_BITS_TYPE(bits) == MACH_PORT_TYPE_NONE);
		assert(object != IO_DEAD);

		entry->ie_bits = ((bits &~ IE_BITS_RIGHT_MASK) |
				  MACH_PORT_TYPE_DEAD_NAME | 2);

	} else if (IE_BITS_TYPE(bits) == MACH_PORT_TYPE_NONE) {
		assert((msgt_name == MACH_MSG_TYPE_MOVE_SEND) ||
		       (msgt_name == MACH_MSG_TYPE_MOVE_SEND_ONCE));

		entry->ie_bits = ((bits &~ IE_BITS_RIGHT_MASK) |
				  MACH_PORT_TYPE_DEAD_NAME | 1);
	} else if (IE_BITS_TYPE(bits) == MACH_PORT_TYPE_DEAD_NAME) {
		assert(object == IO_DEAD);
		assert(IE_BITS_UREFS(bits) > 0);

		if (msgt_name != MACH_MSG_TYPE_COPY_SEND) {
			assert(IE_BITS_UREFS(bits) < MACH_PORT_UREFS_MAX);
			entry->ie_bits = bits+1; /* increment urefs */
		}
	} else {
		assert((msgt_name == MACH_MSG_TYPE_MOVE_SEND) ||
		       (msgt_name == MACH_MSG_TYPE_COPY_SEND));
		assert(IE_BITS_TYPE(bits) == MACH_PORT_TYPE_SEND);
		assert(object != IO_DEAD);
		assert(entry->ie_object == object);
		assert(IE_BITS_UREFS(bits) > 0);

		if (msgt_name != MACH_MSG_TYPE_COPY_SEND) {
			assert(IE_BITS_UREFS(bits) < MACH_PORT_UREFS_MAX-1);
			entry->ie_bits = bits+1; /* increment urefs */
		}

		/*
		 *	May as well convert the entry to a dead name.
		 *	(Or if it is a compat entry, destroy it.)
		 */

		(void) ipc_right_check(space, (ipc_port_t) object,
				       name, entry);
		/* object is dead so it is not locked */
	}
	ipc_entry_modified(space, name, entry);
	/* release the reference acquired by copyin */

	if (object != IO_DEAD)
		io_release(object);
}

/*
 *	Routine:	ipc_right_copyin_two
 *	Purpose:
 *		Like ipc_right_copyin with MACH_MSG_TYPE_MOVE_SEND
 *		and deadok == FALSE, except that this moves two
 *		send rights at once.
 *	Conditions:
 *		The space is write-locked and active.
 *		The object is returned with two refs/send rights.
 *	Returns:
 *		KERN_SUCCESS		Acquired an object.
 *		KERN_INVALID_RIGHT	Name doesn't denote correct right.
 */

kern_return_t
ipc_right_copyin_two(
	ipc_space_t		space,
	mach_port_name_t	name,
	ipc_entry_t		entry,
	ipc_object_t		*objectp,
	ipc_port_t		*sorightp,
	ipc_port_t		*releasep)
{
	ipc_entry_bits_t bits;
	mach_port_urefs_t urefs;
	ipc_port_t port;
	ipc_port_t request = IP_NULL;
#if CONFIG_MACF_MACH
	task_t self = current_task();
	int    rc;
#endif

	*releasep = IP_NULL;

	assert(is_active(space));

	bits = entry->ie_bits;

	if ((bits & MACH_PORT_TYPE_SEND) == 0)
		goto invalid_right;

	urefs = IE_BITS_UREFS(bits);
	if (urefs < 2)
		goto invalid_right;

	port = (ipc_port_t) entry->ie_object;
	assert(port != IP_NULL);

	if (ipc_right_check(space, port, name, entry)) {
		*releasep = port;
		goto invalid_right;
	}
	/* port is locked and active */

#if CONFIG_MACF_MACH
	tasklabel_lock(self);
	rc = mac_port_check_copy_send(&self->maclabel, &port->ip_label);
	tasklabel_unlock(self);
	if (rc) {
		ip_unlock(port);
		return KERN_NO_ACCESS;
	}
#endif

	assert(port->ip_srights > 0);

	if (urefs == 2) {
		if (bits & MACH_PORT_TYPE_RECEIVE) {
			assert(port->ip_receiver_name == name);
			assert(port->ip_receiver == space);
			assert(IE_BITS_TYPE(bits) ==
					MACH_PORT_TYPE_SEND_RECEIVE);

			port->ip_srights++;
			ip_reference(port);
			ip_reference(port);
		} else {
			assert(IE_BITS_TYPE(bits) == MACH_PORT_TYPE_SEND);

			request = ipc_right_request_cancel_macro(space, port,
							     name, entry);

			port->ip_srights++;
			ip_reference(port);
			ipc_hash_delete(space, (ipc_object_t) port,
					name, entry);
			entry->ie_object = IO_NULL;
		}
		entry->ie_bits = bits &~ (IE_BITS_UREFS_MASK|MACH_PORT_TYPE_SEND);
	} else {
		port->ip_srights += 2;
		ip_reference(port);
		ip_reference(port);
		entry->ie_bits = bits-2; /* decrement urefs */
	}
	ipc_entry_modified(space, name, entry);

	ip_unlock(port);

	*objectp = (ipc_object_t) port;
	*sorightp = request;
	return KERN_SUCCESS;

    invalid_right:
	return KERN_INVALID_RIGHT;
}

/*
 *	Routine:	ipc_right_copyout
 *	Purpose:
 *		Copyout a capability to a space.
 *		If successful, consumes a ref for the object.
 *
 *		Always succeeds when given a newly-allocated entry,
 *		because user-reference overflow isn't a possibility.
 *
 *		If copying out the object would cause the user-reference
 *		count in the entry to overflow, and overflow is TRUE,
 *		then instead the user-reference count is left pegged
 *		to its maximum value and the copyout succeeds anyway.
 *	Conditions:
 *		The space is write-locked and active.
 *		The object is locked and active.
 *		The object is unlocked; the space isn't.
 *	Returns:
 *		KERN_SUCCESS		Copied out capability.
 *		KERN_UREFS_OVERFLOW	User-refs would overflow;
 *			guaranteed not to happen with a fresh entry
 *			or if overflow=TRUE was specified.
 */

kern_return_t
ipc_right_copyout(
	ipc_space_t		space,
	mach_port_name_t	name,
	ipc_entry_t		entry,
	mach_msg_type_name_t	msgt_name,
	boolean_t		overflow,
	ipc_object_t		object)
{
	ipc_entry_bits_t bits;
	ipc_port_t port;
#if CONFIG_MACF_MACH
	int rc;
#endif

	bits = entry->ie_bits;

	assert(IO_VALID(object));
	assert(io_otype(object) == IOT_PORT);
	assert(io_active(object));
	assert(entry->ie_object == object);

	port = (ipc_port_t) object;

	switch (msgt_name) {
	    case MACH_MSG_TYPE_PORT_SEND_ONCE:
		
		assert(IE_BITS_TYPE(bits) == MACH_PORT_TYPE_NONE);
		assert(port->ip_sorights > 0);

#if CONFIG_MACF_MACH
		if (space->is_task) {
			tasklabel_lock(space->is_task);
			rc = mac_port_check_hold_send_once(&space->is_task->maclabel,
							   &port->ip_label);
			tasklabel_unlock(space->is_task);

			if (rc) {
				ip_unlock(port);
				return KERN_NO_ACCESS;
			}
		}
#endif
		/* transfer send-once right and ref to entry */
		ip_unlock(port);

		entry->ie_bits = bits | (MACH_PORT_TYPE_SEND_ONCE | 1);
		ipc_entry_modified(space, name, entry);
		break;

	    case MACH_MSG_TYPE_PORT_SEND:
		assert(port->ip_srights > 0);

#if CONFIG_MACF_MACH
		if (space->is_task) {
			tasklabel_lock(space->is_task);
			rc = mac_port_check_hold_send(&space->is_task->maclabel,
						      &port->ip_label);
			tasklabel_unlock(space->is_task);

			if (rc) {
				ip_unlock(port);
				return KERN_NO_ACCESS;
			}
		}
#endif

		if (bits & MACH_PORT_TYPE_SEND) {
			mach_port_urefs_t urefs = IE_BITS_UREFS(bits);

			assert(port->ip_srights > 1);
			assert(urefs > 0);
			assert(urefs < MACH_PORT_UREFS_MAX);

			if (urefs+1 == MACH_PORT_UREFS_MAX) {
				if (overflow) {
					/* leave urefs pegged to maximum */

					port->ip_srights--;
					ip_unlock(port);
					ip_release(port);
					return KERN_SUCCESS;
				}

				ip_unlock(port);
				return KERN_UREFS_OVERFLOW;
			}
			port->ip_srights--;
			ip_unlock(port);
			ip_release(port);
			
		} else if (bits & MACH_PORT_TYPE_RECEIVE) {
			assert(IE_BITS_TYPE(bits) == MACH_PORT_TYPE_RECEIVE);
			assert(IE_BITS_UREFS(bits) == 0);

			/* transfer send right to entry */
			ip_unlock(port);
			ip_release(port);
			
		} else {
			assert(IE_BITS_TYPE(bits) == MACH_PORT_TYPE_NONE);
			assert(IE_BITS_UREFS(bits) == 0);

			/* transfer send right and ref to entry */
			ip_unlock(port);

			/* entry is locked holding ref, so can use port */

			ipc_hash_insert(space, (ipc_object_t) port,
					name, entry);
		}

		entry->ie_bits = (bits | MACH_PORT_TYPE_SEND) + 1;
		ipc_entry_modified(space, name, entry);
		break;

	    case MACH_MSG_TYPE_PORT_RECEIVE: {
		ipc_port_t dest;

#if IMPORTANCE_INHERITANCE
		natural_t assertcnt = port->ip_impcount;
#endif /* IMPORTANCE_INHERITANCE */

		assert(port->ip_mscount == 0);
		assert(port->ip_receiver_name == MACH_PORT_NULL);
		dest = port->ip_destination;

#if CONFIG_MACF_MACH
		if (space->is_task) {
			tasklabel_lock(space->is_task);
			rc = mac_port_check_hold_receive(&space->is_task->maclabel,
							 &port->ip_label);
			tasklabel_unlock(space->is_task);

			if (rc) {
				ip_unlock(port);
				return KERN_NO_ACCESS;
			}
		}
#endif

		port->ip_receiver_name = name;
		port->ip_receiver = space;

		assert((bits & MACH_PORT_TYPE_RECEIVE) == 0);

		if (bits & MACH_PORT_TYPE_SEND) {
			assert(IE_BITS_TYPE(bits) == MACH_PORT_TYPE_SEND);
			assert(IE_BITS_UREFS(bits) > 0);
			assert(port->ip_srights > 0);

			ip_unlock(port);
			ip_release(port);

			/* entry is locked holding ref, so can use port */

			ipc_hash_delete(space, (ipc_object_t) port,
					name, entry);
		} else {
			assert(IE_BITS_TYPE(bits) == MACH_PORT_TYPE_NONE);
			assert(IE_BITS_UREFS(bits) == 0);

			/* transfer ref to entry */
			ip_unlock(port);
		}
		entry->ie_bits = bits | MACH_PORT_TYPE_RECEIVE;
		ipc_entry_modified(space, name, entry);

		if (dest != IP_NULL) {
#if IMPORTANCE_INHERITANCE
			/*
			 * Deduct the assertion counts we contributed to
			 * the old destination port.  They've already
			 * been reflected into the task as a result of
			 * getting enqueued.
			 */
			ip_lock(dest);
			assert(dest->ip_impcount >= assertcnt);
			dest->ip_impcount -= assertcnt;
			ip_unlock(dest);
#endif /* IMPORTANCE_INHERITANCE */
			ip_release(dest);
		}
		break;
	    }

	    default:
		panic("ipc_right_copyout: strange rights");
	}
	return KERN_SUCCESS;
}

/*
 *	Routine:	ipc_right_rename
 *	Purpose:
 *		Transfer an entry from one name to another.
 *		The old entry is deallocated.
 *	Conditions:
 *		The space is write-locked and active.
 *		The new entry is unused.  Upon return,
 *		the space is unlocked.
 *	Returns:
 *		KERN_SUCCESS		Moved entry to new name.
 */

kern_return_t
ipc_right_rename(
	ipc_space_t		space,
	mach_port_name_t	oname,
	ipc_entry_t		oentry,
	mach_port_name_t	nname,
	ipc_entry_t		nentry)
{
	ipc_port_request_index_t request = oentry->ie_request;
	ipc_entry_bits_t bits = oentry->ie_bits;
	ipc_object_t object = oentry->ie_object;
	ipc_port_t release_port = IP_NULL;

	assert(is_active(space));
	assert(oname != nname);

	/*
	 *	If IE_BITS_COMPAT, we can't allow the entry to be renamed
	 *	if the port is dead.  (This would foil ipc_port_destroy.)
	 *	Instead we should fail because oentry shouldn't exist.
	 *	Note IE_BITS_COMPAT implies ie_request != 0.
	 */

	if (request != IE_REQ_NONE) {
		ipc_port_t port;

		assert(bits & MACH_PORT_TYPE_PORT_RIGHTS);
		port = (ipc_port_t) object;
		assert(port != IP_NULL);

		if (ipc_right_check(space, port, oname, oentry)) {
			request = IE_REQ_NONE;
			object = IO_NULL;
			bits = oentry->ie_bits;
			release_port = port;
			assert(IE_BITS_TYPE(bits) == MACH_PORT_TYPE_DEAD_NAME);
			assert(oentry->ie_request == IE_REQ_NONE);
		} else {
			/* port is locked and active */

			ipc_port_request_rename(port, request, oname, nname);
			ip_unlock(port);
			oentry->ie_request = IE_REQ_NONE;
		}
	}

	/* initialize nentry before letting ipc_hash_insert see it */

	assert((nentry->ie_bits & IE_BITS_RIGHT_MASK) == 0);
	nentry->ie_bits |= bits & IE_BITS_RIGHT_MASK;
	nentry->ie_request = request;
	nentry->ie_object = object;

	switch (IE_BITS_TYPE(bits)) {
	    case MACH_PORT_TYPE_SEND: {
		ipc_port_t port;

		port = (ipc_port_t) object;
		assert(port != IP_NULL);

		/* remember, there are no other share entries possible */
		/* or we can't do the rename.  Therefore we do not need */
		/* to check the other subspaces */
	  	ipc_hash_delete(space, (ipc_object_t) port, oname, oentry);
		ipc_hash_insert(space, (ipc_object_t) port, nname, nentry);
		break;
	    }

	    case MACH_PORT_TYPE_RECEIVE:
	    case MACH_PORT_TYPE_SEND_RECEIVE: {
		ipc_port_t port;

		port = (ipc_port_t) object;
		assert(port != IP_NULL);

		ip_lock(port);
		assert(ip_active(port));
		assert(port->ip_receiver_name == oname);
		assert(port->ip_receiver == space);

		port->ip_receiver_name = nname;
		ip_unlock(port);
		break;
	    }

	    case MACH_PORT_TYPE_PORT_SET: {
		ipc_pset_t pset;

		pset = (ipc_pset_t) object;
		assert(pset != IPS_NULL);

		ips_lock(pset);
		assert(ips_active(pset));
		assert(pset->ips_local_name == oname);

		pset->ips_local_name = nname;
		ips_unlock(pset);
		break;
	    }

	    case MACH_PORT_TYPE_SEND_ONCE:
	    case MACH_PORT_TYPE_DEAD_NAME:
		break;

	    default:
		panic("ipc_right_rename: strange rights");
	}

	assert(oentry->ie_request == IE_REQ_NONE);
	oentry->ie_object = IO_NULL;
	ipc_entry_dealloc(space, oname, oentry);
	ipc_entry_modified(space, nname, nentry);
	is_write_unlock(space);

	if (release_port != IP_NULL)
		ip_release(release_port);

	return KERN_SUCCESS;
}
