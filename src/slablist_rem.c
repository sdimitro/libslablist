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

/*
 * Removal Implementation
 *
 * This file implements the procedures that remove elements from Slab Lists.
 * The code in this file uses functions defined in slablist_cons.c,
 * slablist_find.c, and slablist_test.c.
 *
 * The internal architecture of libslablist is described in slablist_impl.h
 */

#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <strings.h>
#include <stdio.h>
#include "slablist_impl.h"
#include "slablist_provider.h"
#include "slablist_cons.h"
#include "slablist_find.h"
#include "slablist_test.h"

/*
 * This function removes an element from a slablist's linked list, either by
 * key or position, if the slablist is sorted or ordered, respectively.
 */
static int
small_list_rem(slablist_t *sl, slablist_elem_t elem, uint64_t pos, slablist_elem_t *rdl)
{
	lock_list(sl);

	int ret;
	if (sl->sl_head == NULL || sl->sl_elems == 0) {
		unlock_list(sl);
		return (SL_EMPTY);
	}


	small_list_t *prev = NULL;

	if (SLIST_SORTED(sl->sl_flags)) {

		/*
		 * We initialize sml to the head of the list, and prev to NULL.
		 * So if unlink_sml_node gets called on the first element, it
		 * will know to unlink the head. `unlink_sml_node` can't unlink
		 * a node, if it does not know what the previous element is (as
		 * these are nodes in a single linked list)..
		 */
		small_list_t *sml = sl->sl_head;
		uint64_t i = 0;
		while (i < sl->sl_elems) {
			if (sl->sl_cmp_elem(elem, sml->sml_data) == 0) {
				/*
				 * If we find the sml node that contains `elem`
				 * we unlink it, free it, and set rdl to that
				 * elem.
				 */
				*rdl = sml->sml_data;
				unlink_sml_node(sl, prev);
				rm_sml_node(sml);
				ret = SL_SUCCESS;
				goto end;
			} else {
				/*
				 * Otherwise, we go to the next element.
				 */
				prev = sml;
				sml = sml->sml_next;
			}
			i++;
		}

		/*
		 * We didn't find a node that matched, so we idicate that
		 * nothing was removed, and we return an indication that the
		 * node was not found.
		 */
		rdl->sle_u = 0;
		ret = SL_ENFOUND;

	} else {

		uint64_t mod = pos % sl->sl_elems;
		small_list_t *sml = sl->sl_head;
		sml = sl->sl_head;


		if (sl->sl_elems < pos && !SLIST_IS_CIRCULAR(sl->sl_flags)) {
			ret = SL_ENCIRC;
		}

		uint64_t i = 0;
		while (i < mod) {
			prev = sml;
			sml = sml->sml_next;
		}
		*rdl = sml->sml_data;
		unlink_sml_node(sl, prev);
		rm_sml_node(sml);
		ret = SL_SUCCESS;
	}


end:;
	/*
	 * We run tests, if DTrace test-probes are enabled.
 	 */
	if (SLABLIST_TEST_IS_SML_LIST_ENABLED()) {
		SLABLIST_TEST_IS_SML_LIST(!(sl->sl_is_small_list));
	}

	if (SLABLIST_TEST_SMLIST_NELEMS_ENABLED()) {
		int f = test_smlist_nelems(sl);
		SLABLIST_TEST_SMLIST_NELEMS(f);
	}

	if (SLIST_SORTED(sl->sl_flags) &&
	    SLABLIST_TEST_SMLIST_ELEMS_SORTED_ENABLED()) {
			/*
			 * If test probe is enabled, we verify that the elems
			 * are sorted.
			 */
			int f = test_smlist_elems_sorted(sl);
			SLABLIST_TEST_SMLIST_ELEMS_SORTED(f);
	}
	unlock_list(sl);
	return (ret);
}

extern void ripple_update_extrema(slablist_t *, subslab_t *);

static void
sub_move_to_next(subslab_t *s, subslab_t *sn)
{
	uint64_t nelems = sn->ss_elems;		/* num elems in next slab */
	uint64_t melems = s->ss_elems;		/* elems in mid slab */
	uint64_t cpelems;			/* elems to copy */
	uint64_t from = 0;			/* ix to start cping from */
	size_t sz = sizeof (void *);
	slablist_t *sl = s->ss_list;

	if (!melems) {
		return;
	}

	if (melems >= SUBSLAB_FREE_SPACE(sn)) {
		cpelems = SUBSLAB_FREE_SPACE(sn);
		from = s->ss_elems - cpelems;
	}

	if (SUBSLAB_FREE_SPACE(sn) > melems) {
		cpelems = melems;
	}



	subslab_t *scp = NULL;
	subslab_t *sncp = NULL;
	subarr_t *sa = NULL;
	subarr_t *sna = NULL;
	int test_data_allocated = 0;
	if (SLABLIST_TEST_SUBSLAB_MOVE_NEXT_ENABLED()) {
		/*
		 * To test that the next slab has of the elements that are to
		 * be copied, we make copies of the slabs before they get
		 * modified.
		 */
		scp = mk_subslab();
		sncp = mk_subslab();
		sa = mk_subarr();
		sna = mk_subarr();
		bcopy(s->ss_arr, sa, sizeof (subarr_t));
		bcopy(sn->ss_arr, sna, sizeof (subarr_t));
		bcopy(s, scp, sizeof (subslab_t));
		bcopy(sn, sncp, sizeof (subslab_t));
		scp->ss_arr = sa;
		sncp->ss_arr = sna;
		test_data_allocated++;
	}

	/*
	 * We make space in the next slab for all of the preceding elements,
	 * which necessarily come before the elements in the slab. We also want
	 * to give preference to the slab with the most free space.
	 */
	bcopy(&(GET_SUBSLAB_ELEM(sn, 0)), &(GET_SUBSLAB_ELEM(sn, cpelems)),
	    nelems*sz);


	/*
	 * We update the s_below bptr, and get the sum of the
	 * ss_usr_elems/s_elems.
	 */
	uint64_t i = from;
	uint64_t sum_usr_elems = 0;
	if (sl->sl_layer == 1) {
		while (i < (from + cpelems)) {
			slab_t *slab = GET_SUBSLAB_ELEM(s, i);
			sum_usr_elems += slab->s_elems;
			slab->s_below = sn;
			SLABLIST_SLAB_SET_BELOW(slab);
			i++;
		}
	} else {
		while (i < (from + cpelems)) {
			subslab_t *ss = GET_SUBSLAB_ELEM(s, i);
			sum_usr_elems += ss->ss_usr_elems;
			ss->ss_below = sn;
			SLABLIST_SUBSLAB_SET_BELOW(sn);
			i++;
		}
	}


	/*
	 * We actually move the elems from s to sn
	 */
	bcopy(&(GET_SUBSLAB_ELEM(s, from)), &(GET_SUBSLAB_ELEM(sn, 0)),
	    cpelems*sz);

	/*
	 * We update the ss_usr_elems count for all of the subslabs below s and
	 * sn.
	 */
	subslab_t *p = s;
	subslab_t *q = sn;
	while (p != NULL) {
		p->ss_usr_elems -= sum_usr_elems;
		SLABLIST_SET_USR_ELEMS(p);
		p = p->ss_below;
	}
	while (q != NULL) {
		q->ss_usr_elems += sum_usr_elems;
		SLABLIST_SET_USR_ELEMS(q);
		q = q->ss_below;
	}

	sn->ss_elems = sn->ss_elems + cpelems;
	s->ss_elems = s->ss_elems - cpelems;

	if (SLABLIST_TEST_SUBSLAB_MOVE_NEXT_ENABLED()) {
		/*
		 * Here we compare the modified subslabs with their pre-mod
		 * copies. And we remove the copies when done.
		 */
		int i;
		int f = test_subslab_move_next(scp, sn, sncp, &i);
		SLABLIST_TEST_SUBSLAB_MOVE_NEXT(f, scp, sn, sncp, from, i);
	}

	if (test_data_allocated) {
		test_data_allocated--;
		rm_subslab(scp);
		rm_subslab(sncp);
		rm_subarr(sa);
		rm_subarr(sna);
	}

	slab_t *ss0;
	subslab_t *ss1;
	if (sl->sl_layer == 1) {
		int last = s->ss_elems - 1;
		ss0 = (slab_t *)GET_SUBSLAB_ELEM(sn, 0);
		sn->ss_min = ss0->s_min;
		if (s->ss_elems) {
			/*
			 * We only update the max of the middle slab if it is
			 * not empty.
			 */
			ss0 = (slab_t *)GET_SUBSLAB_ELEM(s, last);
			s->ss_max = ss0->s_max;
		}
	} else {
		int last = s->ss_elems - 1;
		ss1 = (subslab_t *)GET_SUBSLAB_ELEM(sn, 0);
		sn->ss_min = ss1->ss_min;
		if (s->ss_elems) {
			/*
			 * We only update the max of the middle slab if it is
			 * not empty.
			 */
			ss1 = (subslab_t *)GET_SUBSLAB_ELEM(s, last);
			s->ss_max = ss1->ss_max;
		}
	}
	SLABLIST_SUBSLAB_INC_ELEMS(sn);
	SLABLIST_SUBSLAB_DEC_ELEMS(s);
	SLABLIST_SUBSLAB_SET_MAX(s);
	SLABLIST_SUBSLAB_SET_MIN(sn);
	if (sn->ss_below != NULL) {
		if (s->ss_elems > 0) {
			ripple_update_extrema(s->ss_list, s->ss_below);
		}
		ripple_update_extrema(sn->ss_list, sn->ss_below);
	}
}

static void
sub_move_to_prev(subslab_t *s, subslab_t *sp)
{
	uint64_t pelems = sp->ss_elems;		/* elems in prev slab */
	uint64_t melems = s->ss_elems;		/* elems in middle slab */
	uint64_t cpelems = 0;			/* elems to cp */
	uint64_t from = s->ss_elems - 1;	/* we copy from the end to front */
	size_t sz = sizeof (slablist_elem_t);
	slablist_t *sl = s->ss_list;

	if (!melems) {
		return;
	}

	if (melems >= SUBSLAB_FREE_SPACE(sp)) {
		cpelems = SUBSLAB_FREE_SPACE(sp);
		from = SUBSLAB_FREE_SPACE(sp) - 1;
	}

	if (melems < SUBSLAB_FREE_SPACE(sp)) {
		cpelems = melems;
	}

	subslab_t *scp = NULL;
	subslab_t *spcp = NULL;
	subarr_t *sa = NULL;
	subarr_t *spa = NULL;
	int test_data_allocated = 0;
	if (SLABLIST_TEST_SUBSLAB_MOVE_PREV_ENABLED()) {
		/*
		 * To test that the next slab has of the elements that are to
		 * be copied, we make copies of the slabs before they get
		 * modified.
		 */
		scp = mk_subslab();
		spcp = mk_subslab();
		sa = mk_subarr();
		spa = mk_subarr();
		bcopy(s->ss_arr, sa, sizeof (subarr_t));
		bcopy(sp->ss_arr, spa, sizeof (subarr_t));
		bcopy(s, scp, sizeof (subslab_t));
		bcopy(sp, spcp, sizeof (subslab_t));
		scp->ss_arr = sa;
		spcp->ss_arr = spa;
		test_data_allocated++;
	}

	/*
	 * We update the sbptrs of the to-be-moved slabs.
	 */
	uint64_t i = 0;
	uint64_t sum_usr_elems = 0;
	if (sl->sl_layer == 1) {
		while (i < cpelems) {
			slab_t *slab = GET_SUBSLAB_ELEM(s, i);
			sum_usr_elems += slab->s_elems;
			slab->s_below = sp;
			SLABLIST_SLAB_SET_BELOW(slab);
			i++;
		}
	} else {
		while (i < cpelems) {
			subslab_t *ss = GET_SUBSLAB_ELEM(s, i);
			sum_usr_elems += ss->ss_usr_elems;
			ss->ss_below = sp;
			SLABLIST_SUBSLAB_SET_BELOW(ss);
			i++;
		}
	}
	/*
	 * We move the elems from s to sp.
	 */
	bcopy(&(GET_SUBSLAB_ELEM(s, 0)), &(GET_SUBSLAB_ELEM(sp, pelems)),
	    cpelems*sz);

	subslab_t *p = s;
	subslab_t *q = sp;
	/*
	 * We update the ss_usr_elems count for all of the subslabs below s and
	 * sp.
	 */
	while (p != NULL) {
		p->ss_usr_elems -= sum_usr_elems;
		SLABLIST_SET_USR_ELEMS(p);
		p = p->ss_below;
	}
	while (q != NULL) {
		q->ss_usr_elems += sum_usr_elems;
		SLABLIST_SET_USR_ELEMS(q);
		q = q->ss_below;
	}
	sp->ss_elems = sp->ss_elems + cpelems;
	s->ss_elems = s->ss_elems - cpelems;
	/* bwd shift */
	bcopy(&(GET_SUBSLAB_ELEM(s, cpelems)), &(GET_SUBSLAB_ELEM(s, 0)),
	    (melems-cpelems)*sz);

	if (SLABLIST_TEST_SUBSLAB_MOVE_PREV_ENABLED()) {
		/*
		 * Here we ccompare the modified slabs with their pre-mod
		 * copies. And we remove the copies when done.
		 */
		int i;
		int f = test_subslab_move_prev(scp, sp, spcp, &i);
		SLABLIST_TEST_SUBSLAB_MOVE_PREV(f, scp, sp, spcp, from, i);
	}

	if (test_data_allocated) {
		test_data_allocated--;
		rm_subslab(scp);
		rm_subslab(spcp);
		rm_subarr(sa);
		rm_subarr(spa);
	}

	slab_t *ss0;
	subslab_t *ss1;
	if (sl->sl_layer == 1) {
		int last = sp->ss_elems - 1;
		ss0 = (slab_t *)GET_SUBSLAB_ELEM(s, 0);
		s->ss_min = ss0->s_min;
		if (s->ss_elems) {
			/*
			 * We only update the max of the middle slab if it is
			 * not empty.
			 */
			ss0 = (slab_t *)GET_SUBSLAB_ELEM(sp, last);
			sp->ss_max = ss0->s_max;
		}
	} else {
		int last = sp->ss_elems - 1;
		ss1 = (subslab_t *)GET_SUBSLAB_ELEM(s, 0);
		s->ss_min = ss1->ss_min;
		if (s->ss_elems) {
			/*
			 * We only update the max of the middle slab if it is
			 * not empty.
			 */
			ss1 = (subslab_t *)GET_SUBSLAB_ELEM(sp, last);
			sp->ss_max = ss1->ss_max;
		}
	}
	if (sp->ss_below != NULL) {
		if (s->ss_elems > 0) {
			ripple_update_extrema(s->ss_list, s->ss_below);
		}
		ripple_update_extrema(sp->ss_list, sp->ss_below);
	}
	SLABLIST_SUBSLAB_INC_ELEMS(sp);
	SLABLIST_SUBSLAB_DEC_ELEMS(s);
	SLABLIST_SUBSLAB_SET_MAX(sp);
	SLABLIST_SUBSLAB_SET_MIN(s);
}

/*
 * This function takes last elems in slab `s` and moves to the beginning of
 * slab `sn`. The number of elements taken depends on the number of available
 * spaces in `sn`.
 */
static void
move_to_next(slab_t *s, slab_t *sn)
{
	uint64_t nelems = sn->s_elems;		/* num elems in next slab */
	uint64_t melems = s->s_elems;		/* elems in mid slab */
	uint64_t cpelems;			/* elems to copy */
	uint64_t from = 0;			/* ix to start cping from */
	size_t sz = sizeof (slablist_elem_t);

	if (!melems) {
		return;
	}

	if (melems >= SLAB_FREE_SPACE(sn)) {
		cpelems = SLAB_FREE_SPACE(sn);
		from = s->s_elems - cpelems;
	}

	if (SLAB_FREE_SPACE(sn) > melems) {
		cpelems = melems;
	}



	slab_t *scp = NULL;
	slab_t *sncp = NULL;
	int test_data_allocated = 0;
	if (SLABLIST_TEST_SLAB_MOVE_NEXT_ENABLED()) {
		/*
		 * To test that the next slab has of the elements that are to
		 * be copied, we make copies of the slabs before they get
		 * modified.
		 */
		scp = mk_slab();
		sncp = mk_slab();
		bcopy(s, scp, sizeof (slab_t));
		bcopy(sn, sncp, sizeof (slab_t));
		test_data_allocated++;
	}

	/*
	 * We make space in the next slab for all of the preceding elements,
	 * which necessarily come before the elements in the slab. We also want
	 * to give preference to the slab with the most free space.
	 */
	bcopy(&(sn->s_arr[0]), &(sn->s_arr[cpelems]), nelems*sz);


	/*
	 * We actually move the elems from s to sn.
	 */
	bcopy(&(s->s_arr[from]), &(sn->s_arr[0]), cpelems*sz);
	sn->s_elems = sn->s_elems + cpelems;
	s->s_elems = s->s_elems - cpelems;

	/*
	 * We update the ss_usr_elems count for all of the subslabs below s and
	 * sn.
	 */
	uint64_t sum_usr_elems = cpelems;
	subslab_t *p = s->s_below;
	subslab_t *q = sn->s_below;
	while (p != NULL) {
		p->ss_usr_elems -= sum_usr_elems;
		SLABLIST_SET_USR_ELEMS(p);
		p = p->ss_below;
	}
	while (q != NULL) {
		q->ss_usr_elems += sum_usr_elems;
		SLABLIST_SET_USR_ELEMS(q);
		q = q->ss_below;
	}

	if (SLABLIST_TEST_SLAB_MOVE_NEXT_ENABLED()) {
		/*
		 * Here we ccompare the modified slabs with their pre-mod
		 * copies. And we remove the copies when done.
		 */
		int i;
		int f = test_slab_move_next(scp, sn, sncp, &i);
		SLABLIST_TEST_SLAB_MOVE_NEXT(f, scp, sn, sncp, from, i);
	}

	if (test_data_allocated) {
		test_data_allocated--;
		rm_slab(scp);
		rm_slab(sncp);
	}

	sn->s_min = sn->s_arr[0];
	s->s_max = s->s_arr[(s->s_elems - 1)];
	SLABLIST_SLAB_INC_ELEMS(sn);
	SLABLIST_SLAB_DEC_ELEMS(s);
	SLABLIST_SLAB_SET_MAX(s);
	SLABLIST_SLAB_SET_MIN(sn);
	ripple_update_extrema(s->s_list, s->s_below);
	ripple_update_extrema(sn->s_list, sn->s_below);
}

/*
 * This function takes first elems in slab `s` and moves to the end of slab
 * `sp`. The number of elements taken depends on the number of available spaces
 * in `sp`.
 */
void
move_to_prev(slab_t *s, slab_t *sp)
{
	uint64_t pelems = sp->s_elems;		/* elems in prev slab */
	uint64_t melems = s->s_elems;		/* elems in middle slab */
	uint64_t cpelems = 0;			/* elems to cp */
	uint64_t from = s->s_elems - 1;		/* we copy from the end to front */
	size_t sz = sizeof (slablist_elem_t);

	if (!melems) {
		return;
	}

	if (melems >= SLAB_FREE_SPACE(sp)) {
		cpelems = SLAB_FREE_SPACE(sp);
		from = SLAB_FREE_SPACE(sp) - 1;
	}

	if (melems < SLAB_FREE_SPACE(sp)) {
		cpelems = melems;
	}

	slab_t *scp = NULL;
	slab_t *spcp = NULL;
	int test_data_allocated = 0;
	if (SLABLIST_TEST_SLAB_MOVE_PREV_ENABLED()) {
		/*
		 * To test that the next slab has of the elements that are to
		 * be copied, we make copies of the slabs before they get
		 * modified.
		 */
		scp = mk_slab();
		spcp = mk_slab();
		bcopy(s, scp, sizeof (slab_t));
		bcopy(sp, spcp, sizeof (slab_t));
		test_data_allocated++;
	}

	/*
	 * We move the elems from s to sp.
	 */
	bcopy((s->s_arr), (sp->s_arr + pelems), cpelems*sz);
	sp->s_elems = sp->s_elems + cpelems;
	s->s_elems = s->s_elems - cpelems;
	/* bwd shift */
	bcopy((s->s_arr + cpelems), (s->s_arr), (melems-cpelems)*sz);
	/*
	 * We update the ss_usr_elems count for all of the subslabs below s and
	 * sp.
	 */
	uint64_t sum_usr_elems = cpelems;
	subslab_t *p = s->s_below;
	subslab_t *q = sp->s_below;
	while (p != NULL) {
		p->ss_usr_elems -= sum_usr_elems;
		SLABLIST_SET_USR_ELEMS(p);
		p = p->ss_below;
	}
	while (q != NULL) {
		q->ss_usr_elems += sum_usr_elems;
		SLABLIST_SET_USR_ELEMS(q);
		q = q->ss_below;
	}

	if (SLABLIST_TEST_SLAB_MOVE_PREV_ENABLED()) {
		/*
		 * Here we ccompare the modified slabs with their pre-mod
		 * copies. And we remove the copies when done.
		 */
		int i;
		int f = test_slab_move_prev(scp, sp, spcp, &i);
		SLABLIST_TEST_SLAB_MOVE_PREV(f, scp, sp, spcp, from, i);
	}

	if (test_data_allocated) {
		test_data_allocated--;
		rm_slab(scp);
		rm_slab(spcp);
	}

	s->s_min = s->s_arr[0];
	sp->s_min = sp->s_arr[0];
	sp->s_max = sp->s_arr[(sp->s_elems - 1)];
	s->s_max = s->s_arr[(s->s_elems - 1)];
	SLABLIST_SLAB_INC_ELEMS(sp);
	SLABLIST_SLAB_DEC_ELEMS(s);
	SLABLIST_SLAB_SET_MAX(sp);
	SLABLIST_SLAB_SET_MIN(s);
	ripple_update_extrema(s->s_list, s->s_below);
	ripple_update_extrema(sp->s_list, sp->s_below);
}

/*
 * This function tries to remove a slab if it is empty. If it not empty, it
 * tries to shuffle its elements between adjacent slabs, to make it empty (and
 * thus removable). If it can't remove `s` in such a way, it will try to remove
 * the slabs adjacent to `s`. Note that, unlike slab_generic_insert, this
 * function does not remove elements within the slab. It merely places them
 * elsewhere. the slablist_rem function (below) does remove discrete elements
 * from the slab. There are 5 different kinds of cases in which can shuffle
 * between the slabs, p, m, and n (p = s->prev, m = s, n = s->next):
 *
 *	m -> n
 * 	m -> p
 * 	m -> p & m -> n
 * 	n -> m & m -> p
 * 	p -> m & m -> n
 *
 * For any of the above cases, the slabs have to exist and the the number
 * element from the source slab (the first slab from the left), has to be <=
 * the number of elements in the slab(s) on the right.
 */
slab_t *
slab_generic_rem(slab_t *sm, subslab_t **below)
{
	slab_t *sn = sm->s_next;
	slab_t *sp = sm->s_prev;
	slab_t *uls = NULL; /* this is ptr to slab we have to unlink + free */
	slablist_t *sl = sm->s_list;
	*below = sm->s_below;

	/*
	 * If the slab becomes empty we can free it right away.
	 */
	if (sm->s_elems == 0) {
		uls = sm;
		goto end;
	}

	/*
	 * If we have only one slab, there is nothing for us to do and
	 * we return from the function.
	 */
	if (sl->sl_slabs == 1) {
		return (NULL);
	}


	sn = sm->s_next;
	sp = sm->s_prev;
	if (sn != NULL && sm->s_elems <= SLAB_FREE_SPACE(sn)) {
		SLABLIST_SLAB_MOVE_MID_TO_NEXT(sl, sm, sn);
		move_to_next(sm, sn);
		uls = sm;
		goto end;
	}
	if (sp != NULL && sm->s_elems <= SLAB_FREE_SPACE(sp)) {
		SLABLIST_SLAB_MOVE_MID_TO_PREV(sl, sm, sp);
		move_to_prev(sm, sp);
		uls = sm;
		goto end;
	}
	if (sp != NULL && sn != NULL
	    && sm->s_elems <= (SLAB_FREE_SPACE(sp) + SLAB_FREE_SPACE(sn))) {
		SLABLIST_SLAB_MOVE_MID_TO_NEXT(sl, sm, sn);
		move_to_next(sm, sn);
		SLABLIST_SLAB_MOVE_MID_TO_PREV(sl, sm, sp);
		move_to_prev(sm, sp);
		uls = sm;
		goto end;
	}
	if (sp != NULL && sn != NULL
	    && sn->s_elems <= (SLAB_FREE_SPACE(sm) + SLAB_FREE_SPACE(sp))) {
		SLABLIST_SLAB_MOVE_MID_TO_PREV(sl, sm, sp);
		move_to_prev(sm, sp);
		SLABLIST_SLAB_MOVE_NEXT_TO_MID(sl, sn, sm);
		move_to_prev(sn, sm);
		uls = sn;
		goto end;
	}
	if (sp != NULL && sn != NULL
	    && sp->s_elems <= (SLAB_FREE_SPACE(sm) + SLAB_FREE_SPACE(sn))) {
		SLABLIST_SLAB_MOVE_MID_TO_NEXT(sl, sm, sn);
		move_to_next(sm, sn);
		SLABLIST_SLAB_MOVE_PREV_TO_MID(sl, sm, sp);
		move_to_next(sp, sm);
		uls = sp;
		goto end;
	}



end:;

	if (sl->sl_head == uls) {
		sl->sl_head = uls->s_next;
	}

	if (uls != NULL) {
		unlink_slab(uls);
		rm_slab(uls);
		SLABLIST_SLAB_RM(sl);
	}

	return (uls);
}

subslab_t *
subslab_generic_rem(subslab_t *sm, subslab_t **below)
{
	subslab_t *sn = sm->ss_next;
	subslab_t *sp = sm->ss_prev;
	subslab_t *uls = NULL; /* this is ptr to slab we have to unlink + free */
	slablist_t *sl = sm->ss_list;
	*below = sm->ss_below;

	if (sm->ss_elems == 0) {
		/*
		 * If the slab becomes empty we can free it right away.
		 */
		uls = sm;
		goto end;
	}

	if (sl->sl_slabs == 1) {
		/*
		 * If we have only one slab, there is nothing for us to do and
		 * we return from the function.
		 */
		return (NULL);
	}


	sn = sm->ss_next;
	sp = sm->ss_prev;
	if (sn != NULL && sm->ss_elems <= SUBSLAB_FREE_SPACE(sn)) {
		SLABLIST_SUBSLAB_MOVE_MID_TO_NEXT(sl, sm, sn);
		sub_move_to_next(sm, sn);
		uls = sm;
		goto end;
	}
	if (sp != NULL && sm->ss_elems <= SUBSLAB_FREE_SPACE(sp)) {
		SLABLIST_SUBSLAB_MOVE_MID_TO_PREV(sl, sm, sp);
		sub_move_to_prev(sm, sp);
		uls = sm;
		goto end;
	}
	if (sp != NULL && sn != NULL
	    && sm->ss_elems <= (SUBSLAB_FREE_SPACE(sp) + SUBSLAB_FREE_SPACE(sn))) {
		SLABLIST_SUBSLAB_MOVE_MID_TO_NEXT(sl, sm, sn);
		sub_move_to_next(sm, sn);
		SLABLIST_SUBSLAB_MOVE_MID_TO_PREV(sl, sm, sp);
		sub_move_to_prev(sm, sp);
		uls = sm;
		goto end;
	}
	if (sp != NULL && sn != NULL
	    && sn->ss_elems <= (SUBSLAB_FREE_SPACE(sm) + SUBSLAB_FREE_SPACE(sp))) {
		SLABLIST_SUBSLAB_MOVE_MID_TO_PREV(sl, sm, sp);
		sub_move_to_prev(sm, sp);
		SLABLIST_SUBSLAB_MOVE_NEXT_TO_MID(sl, sn, sm);
		sub_move_to_prev(sn, sm);
		uls = sn;
		goto end;
	}
	if (sp != NULL && sn != NULL
	    && sp->ss_elems <= (SUBSLAB_FREE_SPACE(sm) + SUBSLAB_FREE_SPACE(sn))) {
		SLABLIST_SUBSLAB_MOVE_MID_TO_NEXT(sl, sm, sn);
		sub_move_to_next(sm, sn);
		SLABLIST_SUBSLAB_MOVE_PREV_TO_MID(sl, sm, sp);
		sub_move_to_next(sp, sm);
		uls = sp;
		goto end;
	}



end:;

	if (sl->sl_head == uls) {
		sl->sl_head = uls->ss_next;
	}

	if (uls != NULL) {
		unlink_subslab(uls);
		rm_subarr(uls->ss_arr);
		rm_subslab(uls);
		SLABLIST_SUBSLAB_RM(sl);
	}

	return (uls);
}

/*
 * This function, given an index (0..SELEM_MAX), removes the elment at that
 * index and shifts all the elements _after_ that element to the left.
 */
static void
remove_elem(uint64_t i, slab_t *s)
{
	if (SLABLIST_TEST_REMOVE_ELEM_ENABLED()) {
		int f = test_remove_elem(i, s);
		SLABLIST_TEST_REMOVE_ELEM(f, s, i);
	}
	slablist_t *sl = s->s_list;


	SLABLIST_BWDSHIFT_BEGIN(sl, s, i);
	size_t sz = 8 * (s->s_elems - (i + 1));
	if (i != (uint64_t)(s->s_elems - 1)) {
		/*
		 * If i is not the last element, we do a bwdshift. But if it
		 * is, we only have to decrement s_elems.
		 */
		bcopy(&(s->s_arr[(i + 1)]), &(s->s_arr[i]), sz);
	}
	SLABLIST_BWDSHIFT_END();

	s->s_elems--;
	SLABLIST_SLAB_DEC_ELEMS(s);

	/*
	 * Now we decrement the ss_usr_elems of the subslabs below s.
	 */
	subslab_t *ss = s->s_below;
	while (ss != NULL) {
		ss->ss_usr_elems--;
		SLABLIST_SET_USR_ELEMS(ss);
		ss = ss->ss_below;
	}

	if (s->s_elems && i == 0) {
		s->s_min = s->s_arr[0];
		SLABLIST_SLAB_SET_MIN(s);
	}

	if (s->s_elems && i == (s->s_elems)) {
		s->s_max = s->s_arr[(i - 1)];
		SLABLIST_SLAB_SET_MAX(s);
	}

	if (SLABLIST_TEST_REMOVE_ELEM_ENABLED() && s->s_elems) {
		int f = test_slab_extrema(s);
		SLABLIST_TEST_REMOVE_ELEM(f, s, i);
	}
}

/*
 * This function, given an index (0..SUBELEM_MAX), removes the [sub]slab-ptr at
 * that index and shifts all the [sub]slabs _after_ that [sub]slab to the left.
 */
static void
remove_slab(uint64_t i, subslab_t *s)
{
	slablist_t *sl = s->ss_list;

	SLABLIST_SUBBWDSHIFT_BEGIN(sl, s, i);
	size_t sz = 8 * (s->ss_elems - (i + 1));
	if (i != (uint64_t)(s->ss_elems - 1)) {
		/*
		 * If i is not the last element, we do a bwdshift. But if it
		 * is, we only have to decrement s_elems.
		 */
		int from = i + 1;
		int to = i;
		bcopy(&(GET_SUBSLAB_ELEM(s, from)), &(GET_SUBSLAB_ELEM(s, to)),
		    sz);
	}
	SLABLIST_SUBBWDSHIFT_END();

	s->ss_elems--;
	SLABLIST_SUBSLAB_DEC_ELEMS(s);

	slab_t *sm = NULL;
	subslab_t *ssm = NULL;
	if (s->ss_elems && i == 0) {
		if (sl->sl_layer == 1) {
			sm = (slab_t *)GET_SUBSLAB_ELEM(s, 0);
			s->ss_min = sm->s_min;
		} else {
			ssm = (subslab_t *)GET_SUBSLAB_ELEM(s, 0);
			s->ss_min = ssm->ss_min;
		}
		SLABLIST_SUBSLAB_SET_MIN(s);
	}

	if (s->ss_elems && i == (s->ss_elems)) {
		int last = s->ss_elems - 1;
		if (sl->sl_layer == 1) {
			sm = (slab_t *)GET_SUBSLAB_ELEM(s, last);
			s->ss_max = sm->s_max;
		} else {
			ssm = (subslab_t *)GET_SUBSLAB_ELEM(s, last);
			s->ss_max = ssm->ss_max;
		}
		SLABLIST_SUBSLAB_SET_MAX(s);
	}

	if (SLABLIST_TEST_REMOVE_ELEM_ENABLED()) {
		int f = test_remove_slab(i, s);
		SLABLIST_TEST_REMOVE_SLAB(f, s, i);
	}

	if (SLABLIST_TEST_REMOVE_ELEM_ENABLED() && s->ss_elems) {
		int f = test_subslab_extrema(s);
		SLABLIST_TEST_REMOVE_SLAB(f, s, i);
	}
}

/*
 * Removing an element from a slablist, may result in the freeing of a slab. If
 * the slablist has sublayers, we need to "ripple" through those sublayers,
 * removing any references to the slab that has been freed.
 */
void
ripple_rem_to_sublayers(slablist_t *sl, slab_t *remd, subslab_t *below)
{
	subslab_t *e[3];
	e[0] = below;
	e[1] = below->ss_next;
	e[2] = below->ss_prev;
	int epos = 0;
	subslab_t *ss = below;
	subslab_t *ss_below;
	void *r = remd;
	int i = 0;
	/*
	 * We ripple the removal to the subslabs.
	 */
	while (ss != NULL && r != NULL) {
		i = sublayer_slab_ptr_srch(r, ss);
		remove_slab(i, ss);
		r = subslab_generic_rem(ss, &ss_below);
		if (r == below) {
			epos = 1;
		}
		ss = ss_below;
	}
	/*
	 * We update the extrema as necessary.
	 */
	while (epos < 3) {
		ss = e[epos];
		if (ss == NULL) {
			epos++;
			continue;
		}
		/*
		 * Update the extrema of the topmost subslab.
		 */
		int last = ss->ss_elems - 1;
		slab_t *l = GET_SUBSLAB_ELEM(ss, last);
		slab_t *f = GET_SUBSLAB_ELEM(ss, 0);
		ss->ss_min = f->s_min;
		ss->ss_max = l->s_max;
		ss = ss->ss_below;
		/*
		 * And now we update the extrema of the subslabs that contain
		 * subslabs.
		 */
		while (ss != NULL) {
			last = ss->ss_elems - 1;
			subslab_t *ssf = GET_SUBSLAB_ELEM(ss, 0);
			subslab_t *ssl = GET_SUBSLAB_ELEM(ss, last);
			ss->ss_min = ssf->ss_min;
			ss->ss_max = ssl->ss_max;
			ss = ss->ss_below;
		}
		epos++;
	}
}


/*
 * This function tries to reap slabs once the elems/slabs ratio in a slablist
 * reaches a user-defined minimum. A reap tries to cram all of the elements so
 * that as many slabs from the beginning of the slablist as possible are full.
 * All of the empty slabs should be at the back of the slablist. Optionally,
 * one partial slab should be between the first empty slab and the last full
 * slab.
 */
void
slablist_reap(slablist_t *sl)
{
	if (sl->sl_is_small_list) {
		return;
	}

	/*
	 * For now, we iterate over all slabs. but this is not neccessary.  We
	 * just have to iterate over all the slabs between the first and last
	 * partial slabs, inclusive.
	 */
	SLABLIST_REAP_BEGIN(sl);
	slab_t *s = sl->sl_head;
	slab_t *sn = NULL;
	slab_t *rmd;
	subslab_t *below = NULL;
	uint64_t i = 0;
	while (i < (sl->sl_slabs - 1)) {
		rmd = NULL;
		sn = s->s_next;
		if (s->s_elems < SELEM_MAX) {
			move_to_prev(sn, s);
			if (sn->s_elems == 0) {
				/*
				 * We unlink and free the slab.
				 */
				below = sn->s_below;
				unlink_slab(sn);
				rm_slab(sn);
				rmd = sn;
				SLABLIST_SLAB_RM(sl);
				if (sl->sl_sublayers) {
					/*
					 * If we have sublayers, we ripple the changes
					 * down.
					 */
					ripple_rem_to_sublayers(sl, rmd, below);
				}
			}
		}
		s = s->s_next;
		i++;
	}
	SLABLIST_REAP_END(sl);
}

/*
 * Remove the pos'th element in this slablist. Or remove the element that is ==
 * to `elem`. Note that this function does _not_ deallocate the memory that
 * backs large objects.
 */
int
slablist_rem(slablist_t *sl, slablist_elem_t elem, uint64_t pos, slablist_elem_t *rdl)
{
	lock_list(sl);

	uint64_t off_pos;
	slab_t *s = NULL;
	int i;
	int ret;
	slab_t *found = NULL;

	if (!(sl->sl_is_small_list) && sl->sl_elems == SMELEM_MAX) {
		/*
		 * If we have lowered the number of elems to 1/2 a slab, we
		 * turn the slab into a small linked list.
		 */
		slab_to_small_list(sl);
	}


	if (sl->sl_is_small_list) {
		/*
		 * We are dealing with a small list and have to remove the
		 * element.
		 */
		SLABLIST_REM_BEGIN(sl, elem, pos);
		ret = small_list_rem(sl, elem, pos, rdl);
		goto end;
	}



	if (SLIST_SORTED(sl->sl_flags)) {

		if (SLABLIST_TEST_IS_SLAB_LIST_ENABLED()) {
			SLABLIST_TEST_IS_SLAB_LIST(sl->sl_is_small_list);
		}

		SLABLIST_REM_BEGIN(sl, elem, pos);

		if (sl->sl_sublayers) {

			find_bubble_up(sl, elem, &found);
			s = found;

		} else {

			find_linear_scan(sl, elem, &s);

		}

		i = slab_bin_srch(elem, s);

		if (sl->sl_cmp_elem(s->s_arr[i], elem) != 0) {
			/*
			 * If the element was not found, we have nothing to
			 * remove, and return.
			 */
			if (rdl != NULL) {
				rdl->sle_u = NULL;
			}

			ret = SL_ENFOUND;
			goto end;
		}

	} else {

		SLABLIST_REM_BEGIN(sl, elem, pos);

		s = slab_get_elem_pos(sl, pos, &off_pos);
		i = off_pos;
	}

	if (rdl != NULL) {
		*rdl = s->s_arr[i];
	}

	remove_elem(i, s);

	slab_t *remd = NULL;
	subslab_t *below = NULL;
	remd = slab_generic_rem(s, &below);
	if (sl->sl_sublayers) {
		ripple_rem_to_sublayers(sl, remd, below);
		slablist_t *subl = sl->sl_baselayer;
		slablist_t *supl = subl->sl_superlayer;
		if (subl != sl && supl->sl_slabs < sl->sl_req_sublayer) {
			/*
			 * If the baselayer's superlayer has < sl_req_sublayer,
			 * the baselayer is not needed. We remove it.
			 */
			detach_sublayer(supl);
		}
	}


	try_reap_all(sl);

	sl->sl_elems--;
	SLABLIST_SL_DEC_ELEMS(sl);
end:;
	unlock_list(sl);

	SLABLIST_REM_END(ret);

	return (ret);
}
