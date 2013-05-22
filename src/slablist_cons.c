/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at src/LIBSLABLIST.LICENSE
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at src/LIBSLABLIST.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2012 Nicholas Zivkovic. All rights reserved.
 * Use is subject to license terms.
 */

#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <strings.h>
#include <stdio.h>
#include "slablist_impl.h"
#include "slablist_find.h"
#include "slablist_test.h"
#include "slablist_provider.h"

static int init = 0;
static int nlists = 0;
static slablist_t *lst_sl = NULL;
/* static pthread_mutex_t lst_sl_lk; */
extern int slablist_umem_init();

int
lock_list(slablist_t *sl)
{
	int e = pthread_mutex_lock(&sl->sl_mutex);
	return (e);
}

int
trylock_list(slablist_t *sl)
{
	int e = pthread_mutex_trylock(&sl->sl_mutex);
	return (e);
}

int
unlock_list(slablist_t *sl)
{
	int e = pthread_mutex_unlock(&sl->sl_mutex);
	return (e);
}

slablist_t *
slablist_create(
	char *name,		/* descriptive name */
	size_t obj_size,	/* size of elem */
	slablist_cmp_t cmpfun,	/* comparison function callback */
	uint16_t sublayer_req,	/* slabs needed to attach sublayer */
	uint64_t mslabs,	/* minimum number of slabs to reap */
	uint8_t mpslabs,	/* minimum percentage of slabs to reap */
	uint8_t brk,		/* elems needed to use bin search */
	uint8_t fl)		/* flags */
{
	if (init == 0) {
		/*
		 * If this is the first use of libslablist, we initialize the
		 * umem caches.
		 */
		slablist_umem_init();
		init = 1;
	}
	slablist_t *list = mk_slablist();
	/* TODO: add this list to the master slablist */
	/* link_slablist(master_list, list); */
	size_t namesize = strlen(name);
	list->sl_name = mk_buf(namesize);
	strcpy(list->sl_name, name);
	list->sl_cmp_elem = cmpfun;
	list->sl_obj_sz = obj_size;
	list->sl_flags = fl;
	/*
	 * Set the mpslabs, which can never be larger than 99.
	 */
	if (mpslabs > 99) {
		list->sl_mpslabs = 99;
	} else {
		list->sl_mpslabs = mpslabs;
	}

	list->sl_mslabs = mslabs;

	/*
	 * Set the brk value, which can never be larger than the number of
	 * elems that can fit in a slab.
	 */
	if (brk <= SELEM_MAX) {
		list->sl_brk = brk;
	} else {
		list->sl_brk = SELEM_MAX;
	}

	/*
	 * Set the sublayer_req value, which can never be larger than the
	 * number of elems that can fit in a slab.
	 */
	if (sublayer_req <= SELEM_MAX) {
		list->sl_req_sublayer = sublayer_req;
	} else {
		list->sl_req_sublayer = SELEM_MAX;
	}

	SLABLIST_CREATE(list);
	/*
	 * Add the newly created list to the head of the doubly linked list of
	 * slab-lists. In libslablist, all slab lists are linked to each other.
	 */
	if (lst_sl == NULL) {
		lst_sl = list;
	} else {
		list->sl_next = lst_sl;
		lst_sl->sl_prev = list;
		lst_sl = list;
	}
	nlists++;
	return (list);
}

/*
 * This function allows the user to set a new minimum capacity.
 */
void
slablist_setmpslabs(slablist_t *sl, uint8_t new)
{
	lock_list(sl);
	if (new <= 99) {
		sl->sl_mpslabs = new;
	} else {
		sl->sl_mpslabs = 99;
	}
	unlock_list(sl);
}

void
slablist_setmslabs(slablist_t *sl, uint64_t new)
{
	lock_list(sl);
	sl->sl_mslabs = new;
	unlock_list(sl);
}

char *
slablist_getname(slablist_t *sl)
{
	return (sl->sl_name);
}

uint8_t
slablist_getmpslabs(slablist_t *sl)
{
	return (sl->sl_mpslabs);
}

uint64_t
slablist_getmslabs(slablist_t *sl)
{
	return (sl->sl_mslabs);
}

uint64_t
slablist_getelems(slablist_t *sl)
{
	return (sl->sl_elems);
}

uint64_t
slablist_gettype(slablist_t *sl)
{
	if (SLIST_SORTED(sl->sl_flags)) {
		return (SL_SORTED);
	}
	return (SL_ORDERED);
}

void
link_sml_node(slablist_t *sl, small_list_t *prev, small_list_t *to_link)
{
	SLABLIST_LINK_SML_NODE(sl);
	small_list_t *next = NULL;

	if (prev != NULL) {
		next = prev->sml_next;
		prev->sml_next = to_link;
		to_link->sml_next = next;
	} else {
		next = sl->sl_head;
		sl->sl_head = to_link;
		SLABLIST_ADD_HEAD(sl);
		to_link->sml_next = next;
	}

	sl->sl_elems++;
	SLABLIST_SL_INC_ELEMS(sl);
}

/*
 * We unlink the node that comes after `prev` from the linked list.
 */
void
unlink_sml_node(slablist_t *sl, small_list_t *prev)
{
	SLABLIST_UNLINK_SML_NODE(sl);
	small_list_t *to_rem = NULL;

	/*
	 * If prev is NULL, we want to remove the head, otherwise we just
	 * remove the next node.
	 */
	if (prev == NULL) {
		to_rem = sl->sl_head;
		sl->sl_head = to_rem->sml_next;
		SLABLIST_REM_HEAD(sl);
	} else {
		to_rem = prev->sml_next;
		prev->sml_next = to_rem->sml_next;
	}

	sl->sl_elems--;
	SLABLIST_SL_DEC_ELEMS(sl);
}

/*
 * We link slab `s1` to slab `s2`. `flag` indicates if we link to the left or
 * to the right of `s2`.
 */
void
link_slab(slab_t *s1, slab_t *s2, int flag)
{
	slablist_t *sl = s2->s_list;
	if (flag == SLAB_LINK_BEFORE) {
		SLABLIST_LINK_SLAB_BEFORE(sl, s1, s2);
		s1->s_next = s2;
		s1->s_prev = s2->s_prev;
		s2->s_prev = s1;
		if (s1->s_prev != NULL) {
			s1->s_prev->s_next = s1;
		}
		if (s2 == sl->sl_head) {
			sl->sl_head = s1;
			SLABLIST_ADD_HEAD(sl);
		}
	}

	if (flag == SLAB_LINK_AFTER) {
		SLABLIST_LINK_SLAB_AFTER(sl, s1, s2);
		s1->s_prev = s2;
		s1->s_next = s2->s_next;
		s2->s_next = s1;
		if (s1->s_next != NULL) {
			s1->s_next->s_prev = s1;
		}
		if (s2 == sl->sl_end) {
			sl->sl_end = s1;
		}
	}

	s2->s_list->sl_slabs++;
	SLABLIST_SL_INC_SLABS(s2->s_list);
	s1->s_list = s2->s_list;

}

/*
 * We link subslab `s1` to subslab `s2`. `flag` indicates if we link to the left or
 * to the right of `s2`.
 */
void
link_subslab(subslab_t *s1, subslab_t *s2, int flag)
{
	slablist_t *sl = s2->ss_list;
	if (flag == SLAB_LINK_BEFORE) {
		SLABLIST_LINK_SUBSLAB_BEFORE(sl, s1, s2);
		s1->ss_next = s2;
		s1->ss_prev = s2->ss_prev;
		s2->ss_prev = s1;
		if (s1->ss_prev != NULL) {
			s1->ss_prev->ss_next = s1;
		}
		if (s2 == sl->sl_head) {
			sl->sl_head = s1;
			SLABLIST_ADD_HEAD(sl);
		}
	}

	if (flag == SLAB_LINK_AFTER) {
		SLABLIST_LINK_SUBSLAB_AFTER(sl, s1, s2);
		s1->ss_prev = s2;
		s1->ss_next = s2->ss_next;
		s2->ss_next = s1;
		if (s1->ss_next != NULL) {
			s1->ss_next->ss_prev = s1;
		}
		if (s2 == sl->sl_end) {
			sl->sl_end = s1;
		}
	}

	s2->ss_list->sl_slabs++;
	SLABLIST_SL_INC_SUBSLABS(s2->ss_list);
	s1->ss_list = s2->ss_list;

}

/*
 * Removes slab from slab list.
 */
void
unlink_slab(slab_t *s)
{
	slablist_t *sl = s->s_list;
	SLABLIST_UNLINK_SLAB(sl, s);
	if (s->s_prev != NULL) {
		s->s_prev->s_next = s->s_next;
	}

	if (s->s_next != NULL) {
		s->s_next->s_prev = s->s_prev;
		if (sl->sl_head == s) {
			sl->sl_head = s->s_next;
			SLABLIST_REM_HEAD(sl);
		}
	}

	sl->sl_slabs--;
	SLABLIST_SL_DEC_SLABS(sl);
}

/*
 * Removes subslab from subslab list.
 */
void
unlink_subslab(subslab_t *s)
{
	slablist_t *sl = s->ss_list;
	SLABLIST_UNLINK_SUBSLAB(sl, s);
	if (s->ss_prev != NULL) {
		s->ss_prev->ss_next = s->ss_next;
	}

	if (s->ss_next != NULL) {
		s->ss_next->ss_prev = s->ss_prev;
		if (sl->sl_head == s) {
			sl->sl_head = s->ss_next;
			SLABLIST_REM_HEAD(sl);
		}
	}

	sl->sl_slabs--;
	SLABLIST_SL_DEC_SUBSLABS(sl);
}

/*
 * Removes all slabs from `sl`. Used as a catch-all.
 */
static void
remove_slabs(slablist_t *sl)
{
	slab_t *s;
	s = sl->sl_head;
	uint64_t i = 0;
	uint64_t nslabs = sl->sl_slabs;
	while (i < nslabs) {
		/*
		 * We are not responsible for user-allocated objects.
		 */
		unlink_slab(s);
		rm_slab(s);
		SLABLIST_SLAB_RM(sl);
		s = s->s_next;
		i++;
	}
}

/*
 * Removes all slabs from `sl`. Used as a catch-all.
 */
static void
remove_subslabs(slablist_t *sl)
{
	subslab_t *s;
	s = sl->sl_head;
	uint64_t i = 0;
	uint64_t nslabs = sl->sl_slabs;
	while (i < nslabs) {
		/*
		 * We are not responsible for user-allocated objects.
		 */
		unlink_subslab(s);
		rm_subslab(s);
		SLABLIST_SUBSLAB_RM(sl);
		s = s->ss_next;
		i++;
	}
}

/*
 * Destroys a slab list, and frees all slabs as well as removing `sl` from
 * memory.
 */
void
slablist_destroy(slablist_t *sl)
{
	lock_list(sl);
	SLABLIST_DESTROY(sl);
	size_t namesz = strlen(sl->sl_name);
	rm_buf(sl->sl_name, namesz);
	small_list_t *sml;
	small_list_t *smln;

	slablist_t *p;
	slablist_t *q;
	p = sl->sl_sublayer;
	if (!(sl->sl_is_small_list) && sl->sl_head != NULL) {
		remove_slabs(sl);
	}

	if (!(sl->sl_is_small_list) && sl->sl_sublayer != NULL) {
		while (p != NULL) {
			q = p;
			remove_slabs(p);
			p = q->sl_sublayer;
			rm_slablist(q);
		}
	}

	if (sl->sl_is_small_list && sl->sl_head != NULL) {
		sml = sl->sl_head;
		uint64_t i = 0;
		while (i < sl->sl_elems) {
			smln = sml->sml_next;
			rm_sml_node(sml);
			sml = smln;
			i++;
		}
	}

	nlists--;
	rm_slablist(sl);
	unlock_list(sl);
}


/*
 * This function detaches the sublayer that is immediately attached `sl`, and
 * frees all data associated with it. Be careful not to detach a sublayer which
 * has sublayers! This is the caller's responsibility.
 */
void
detach_sublayer(slablist_t *sl)
{
	slablist_t *sub = sl->sl_sublayer;
	SLABLIST_DETACH_SUBLAYER(sl, sub);

	remove_subslabs(sub);

	sl->sl_sublayer = NULL;

	SLABLIST_SL_DEC_ELEMS(sub);

	rm_slablist(sub);
	slablist_t *sup = sl;
	/* Update the sublayer counter in all the superlayers. */
	while (sup != NULL) {
		sup->sl_baselayer = sl;
		sup->sl_sublayers--;
		sup = sup->sl_superlayer;
	}
}

/*
 * This function attaches a new sublayer to `sl`. Be careful not to attach a
 * new sublayer to a list which already has a sublayer. This is the caller's
 * responsibility.
 */
void
attach_sublayer(slablist_t *sl)
{
	slablist_t *sub = mk_slablist();
	SLABLIST_ATTACH_SUBLAYER(sl, sub);
	sub->sl_req_sublayer = sl->sl_req_sublayer;
	bcopy(sl, sub, sizeof (slablist_t));
	sl->sl_sublayer = sub;
	sl->sl_baselayer = sub;

	sub->sl_head = mk_subslab();

	SLABLIST_SLAB_MK(sub);
	SLIST_SET_SUBLAYER(sub->sl_flags);

	subslab_t *sh = sub->sl_head;
	sh->ss_elems = sl->sl_slabs;
	sh->ss_list = sub;
	sh->ss_arr = mk_subarr();

	slab_t *h = NULL;
	subslab_t *hh = NULL;
	if (sl->sl_superlayer == NULL) {
		h = sl->sl_head;
	} else {
		hh = sl->sl_head;
	}


	sub->sl_slabs = 1;
	sub->sl_elems = sl->sl_slabs;
	sub->sl_superlayer = sl;
	sub->sl_layer++;
	SLABLIST_SL_INC_LAYER(sub);

	slablist_t *sup = sub->sl_superlayer;

	/* Update the sublayer counter in all the superlayers */
	while (sup != NULL) {
		sup->sl_sublayers++;
		sup->sl_baselayer = sub;
		SLABLIST_SL_INC_SUBLAYERS(sup);
		sup = sup->sl_superlayer;
	}
	sup = sub->sl_superlayer;

	subslab_t *sc = hh;
	slab_t *c = h;
	subslab_t *sfirst = NULL;
	subslab_t *slast = NULL;
	slab_t *f = NULL;
	slab_t *l = NULL;
	uint64_t i = 0;
	if (sup->sl_layer) {
		/* Copy pointers of all superslabs into the head subslab */
		while (i < sl->sl_slabs) {
			SLABLIST_SUBSLAB_ADD_INTO(sub, sh, NULL, sc);
			SET_SUBSLAB_ELEM(sh, (void *)sc, i);
			sc = sc->ss_next;
			i++;
		}
		/* Set the max and the min of head subslab */
		sfirst = (subslab_t *)(GET_SUBSLAB_ELEM(sh, 0));
		int last = i - 1;
		slast = (subslab_t *)(GET_SUBSLAB_ELEM(sh, last));
		sh->ss_min = (uintptr_t)(sfirst->ss_min);
		SLABLIST_SUBSLAB_SET_MIN(sh);
		sh->ss_max = (uintptr_t)(slast->ss_max);
		SLABLIST_SUBSLAB_SET_MAX(sh);
	} else {
		/* Copy pointers of all superslabs into the head subslab */
		while (i < sl->sl_slabs) {
			SLABLIST_SUBSLAB_ADD_INTO(sub, sh, c, NULL);
			SET_SUBSLAB_ELEM(sh, (void *)c, i);
			c = c->s_next;
			i++;
		}
		/* Set the max and the min of head subslab */
		f = (slab_t *)(GET_SUBSLAB_ELEM(sh, 0));
		int last = i - 1;
		l = (slab_t *)(GET_SUBSLAB_ELEM(sh, last));
		sh->ss_min = (uintptr_t)(f->s_min);
		SLABLIST_SUBSLAB_SET_MIN(sh);
		sh->ss_max = (uintptr_t)(l->s_max);
		SLABLIST_SUBSLAB_SET_MAX(sh);
	}
}

/*
 * We use this function later, so it needs to be declared.
 */
int small_list_add(slablist_t *, uintptr_t, int, uintptr_t *);


/*
 * When we decrease to less than 50% capacity in a slab list with one remaining
 * slab, we convert the slab into a singly linked list. This way, if we keep
 * removing elements from this slab list, we will never dip below 50% memory
 * efficiency.
 */
void
slab_to_small_list(slablist_t *sl)
{
	sl->sl_is_small_list = 1;
	slab_t *h = sl->sl_head;
	sl->sl_head = NULL;
	uint64_t nelems = sl->sl_elems;
	sl->sl_elems = 0;
	uint64_t i = 0;
	while (i < nelems) {
		/*
		 * We copy the data from the head slab into sml_nodes, which we
		 * link up into a singly linked list.
		 */
		small_list_add(sl, h->s_arr[i], 0, NULL);
		i++;
	}

	sl->sl_slabs = 0;
	if (SLABLIST_TEST_SLAB_TO_SML_ENABLED()) {
		int f = test_slab_to_sml(sl, h);
		SLABLIST_TEST_SLAB_TO_SML(f);
	}
	rm_slab(h);
	SLABLIST_SLAB_RM(sl);
	SLABLIST_TO_SMALL_LIST(sl);
}

void
small_list_to_slab(slablist_t *sl)
{
	SLABLIST_TO_SLAB(sl);
	slab_t *s = NULL;
	s = mk_slab();
	SLABLIST_SLAB_MK(sl);
	s->s_list = sl;
	small_list_t *sml = sl->sl_head;
	small_list_t *smlp = NULL;
	uint64_t i = 0;
	while (i < sl->sl_elems) {
		s->s_arr[i] = sml->sml_data;
		smlp = sml;
		sml = sml->sml_next;
		rm_sml_node(smlp);
		s->s_elems++;
		i++;
	}

	SLABLIST_SLAB_INC_ELEMS(s);
	sl->sl_is_small_list = 0;
	sl->sl_head = s;
	sl->sl_end = s;
	sl->sl_slabs = 1;
	SLABLIST_SL_INC_SLABS(sl);

	if (SLIST_SORTED(sl->sl_flags)) {
		s->s_min = s->s_arr[0];
		s->s_max = s->s_arr[(s->s_elems - 1)];
	}
}

/*
 * This function tries to reap all the slabs in a slab list (not counting any
 * subslabs). It will only reap if the slab list can have a minumum number of
 * slabs AND a minimum percentage of slabs reaped. This way the user won't
 * waste cycles on trivial memory savings.
 */
void
try_reap(slablist_t *sl)
{
	uint64_t slabs_saveable = sl->sl_slabs - (sl->sl_elems / SELEM_MAX);
	float percntg_slabs_saveable = ((float)slabs_saveable) / ((float)sl->sl_slabs);
	float req_percntg = ((float)(sl->sl_mpslabs))/100.0;
	if (slabs_saveable >= sl->sl_mslabs &&
	    percntg_slabs_saveable >= req_percntg) {
		slablist_reap(sl);
	}
}

/*
 * This function tries to reap all the slabs in a slab list, including all the
 * subslabs.
 */
void
try_reap_all(slablist_t *sl)
{
	slablist_t *csl = sl;
	/* TODO make reaps work for sublayers too */
	try_reap(csl);
	/*
	int i = 0;
	do {
		try_reap(csl);
		csl = csl->sl_sublayer;
		i++;
	} while (i < sl->sl_sublayers);
	*/
}

/*
 * Records the subslabs `s` into the bread crumb path `crumbs`.
 */
void
record_subslab(bc_t *crumbs, subslab_t *s, int edge)
{
	uint8_t i = crumbs->bc_sscount;
	if (i >= (MAX_LYRS - 1)) {
		abort();
	}
	crumbs->bc_ssarr[i].ssbc_subslab = s;
	crumbs->bc_ssarr[i].ssbc_on_edge = edge;
	crumbs->bc_sscount++;
}

/*
 * Records the slab `s` in the bread crumb path `crumbs`.
 */
void
record_slab(bc_t *crumbs, slab_t *s, int edge)
{
	uint8_t i = crumbs->bc_sscount;
	if (i >= MAX_LYRS) {
		abort();
	}
	crumbs->bc_top.sbc_slab = s;
	crumbs->bc_top.sbc_on_edge = edge;
}

subslab_t *
retrieve_subslab(bc_t *crumbs, uint8_t i)
{
	if (i >= crumbs->bc_sscount) {
		abort();
	}
	subslab_t *s = crumbs->bc_ssarr[i].ssbc_subslab;
	return (s);
}

subslab_t *
retrieve_top_subslab(bc_t *crumbs)
{
	subslab_t *s = crumbs->bc_ssarr[(crumbs->bc_sscount - 1)].ssbc_subslab;
	return (s);
}

slab_t *
retrieve_slab(bc_t *crumbs)
{
	slab_t *s = crumbs->bc_top.sbc_slab;
	return (s);
}
