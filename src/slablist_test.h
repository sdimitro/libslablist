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

int test_slab_get_elem_pos(slablist_t *sl, slab_t *s, slab_t **f,
    uint64_t pos, uint64_t op, uint64_t *opbptr);
int test_slab_to_sml(slablist_t *, slab_t *);
int test_smlist_nelems(slablist_t *);
int test_smlist_elems_sorted(slablist_t *);
int test_add_elem(slab_t *, slablist_elem_t, uint64_t);
int test_add_slab(subslab_t *, slab_t *, subslab_t *, uint64_t);
int test_remove_elem(uint64_t, slab_t *);
int test_remove_slab(uint64_t, subslab_t *);
int test_rem_range_sub(subslab_t *);
int test_rem_range_sub_slim(subslab_t *);
int test_rem_range(slab_t *);
int test_slab_extrema(slab_t *);
int test_ripple_add_slab(slab_t *, slab_t *, int);
int test_ripple_add_subslab(subslab_t *, int);
int test_find_bubble_up(subslab_t *, slab_t *, slablist_elem_t elem);
int test_slab_srch(slablist_elem_t, slab_t *, int);
int test_slab_move_next(slab_t *, slab_t *, slab_t *, int *);
int test_slab_move_prev(slab_t *, slab_t *, slab_t *, int *);
int test_subslab_extrema(subslab_t *);
int test_subslab_ref(subslab_t *s);
int test_subslab_move_next(subslab_t *, subslab_t *, subslab_t *, int *);
int test_subslab_move_prev(subslab_t *, subslab_t *, subslab_t *, int *);
int test_subslab_usr_elems(subslab_t *s);
