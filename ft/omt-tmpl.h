/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:
#if !defined(OMT_TMPL_H)
#define OMT_TMPL_H

#ident "$Id$"
#ident "Copyright (c) 2007-2012 Tokutek Inc.  All rights reserved."
#ident "The technology is licensed by the Massachusetts Institute of Technology, Rutgers State University of New Jersey, and the Research Foundation of State University of New York at Stony Brook under United States of America Serial No. 11/760379 and to the patents and/or patent applications resulting from it."

#include <toku_portability.h>
#include <stdint.h>
#include <stdbool.h>

namespace toku {

/**
 * Order Maintenance Tree (OMT)
 *
 * Maintains a collection of totally ordered values, where each value has an integer weight.
 * The OMT is a mutable datatype.
 *
 * The Abstraction:
 *
 * An OMT is a vector of values, $V$, where $|V|$ is the length of the vector.
 * The vector is numbered from $0$ to $|V|-1$.
 * Each value has a weight.  The weight of the $i$th element is denoted $w(V_i)$.
 *
 * We can create a new OMT, which is the empty vector.
 *
 * We can insert a new element $x$ into slot $i$, changing $V$ into $V'$ where
 *  $|V'|=1+|V|$       and
 *
 *   V'_j = V_j       if $j<i$
 *          x         if $j=i$
 *          V_{j-1}   if $j>i$.
 *
 * We can specify $i$ using a kind of function instead of as an integer.
 * Let $b$ be a function mapping from values to nonzero integers, such that
 * the signum of $b$ is monotically increasing.
 * We can specify $i$ as the minimum integer such that $b(V_i)>0$.
 *
 * We look up a value using its index, or using a Heaviside function.
 * For lookups, we allow $b$ to be zero for some values, and again the signum of $b$ must be monotonically increasing.
 * When lookup up values, we can look up
 *  $V_i$ where $i$ is the minimum integer such that $b(V_i)=0$.   (With a special return code if no such value exists.)
 *      (Rationale:  Ordinarily we want $i$ to be unique.  But for various reasons we want to allow multiple zeros, and we want the smallest $i$ in that case.)
 *  $V_i$ where $i$ is the minimum integer such that $b(V_i)>0$.   (Or an indication that no such value exists.)
 *  $V_i$ where $i$ is the maximum integer such that $b(V_i)<0$.   (Or an indication that no such value exists.)
 *
 * When looking up a value using a Heaviside function, we get the value and its index.
 *
 * We can also split an OMT into two OMTs, splitting the weight of the values evenly.
 * Find a value $j$ such that the values to the left of $j$ have about the same total weight as the values to the right of $j$.
 * The resulting two OMTs contain the values to the left of $j$ and the values to the right of $j$ respectively.
 * All of the values from the original OMT go into one of the new OMTs.
 * If the weights of the values don't split exactly evenly, then the implementation has the freedom to choose whether
 *  the new left OMT or the new right OMT is larger.
 *
 * Performance:
 *  Insertion and deletion should run with $O(\log |V|)$ time and $O(\log |V|)$ calls to the Heaviside function.
 *  The memory required is O(|V|).
 *
 * Usage:
 *  The omt is templated by two parameters:
 *   - omtdata_t is what will be stored within the omt.  These could be pointers or real data types (ints, structs).
 *   - omtdataout_t is what will be returned by find and related functions.  By default, it is the same as omtdata_t, but you can set it to (omtdata_t *).
 *  To create an omt which will store "TXNID"s, for example, it is a good idea to typedef the template:
 *   typedef omt<TXNID> txnid_omt_t;
 *  If you are storing structs, you may want to be able to get a pointer to the data actually stored in the omt (see find_zero).  To do this, use the second template parameter:
 *   typedef omt<struct foo, struct foo *> foo_omt_t;
 */
template<typename omtdata_t,
         typename omtdataout_t=omtdata_t>
class omt {
public:
    /**
     * Effect: Create an empty OMT.
     * Performance: constant time.
     */
    void create(void);

    /**
     * Effect: Create an empty OMT with no internal allocated space.
     * Performance: constant time.
     * Rationale: In some cases we need a valid omt but don't want to malloc.
     */
    void create_no_array(void);

    /**
     * Effect: Create a OMT containing values.  The number of values is in numvalues.
     *  Stores the new OMT in *omtp.
     * Requires: this has not been created yet
     * Requires: values != NULL
     * Requires: values is sorted
     * Performance:  time=O(numvalues)
     * Rationale:    Normally to insert N values takes O(N lg N) amortized time.
     *               If the N values are known in advance, are sorted, and
     *               the structure is empty, we can batch insert them much faster.
     */
    __attribute__((nonnull))
    void create_from_sorted_array(const omtdata_t *const values, const uint32_t numvalues);

    /**
     * Effect: Create an OMT containing values.  The number of values is in numvalues.
     *         On success the OMT takes ownership of *values array, and sets values=NULL.
     * Requires: this has not been created yet
     * Requires: values != NULL
     * Requires: *values is sorted
     * Requires: *values was allocated with toku_malloc
     * Requires: Capacity of the *values array is <= new_capacity
     * Requires: On success, *values may not be accessed again by the caller.
     * Performance:  time=O(1)
     * Rational:     create_from_sorted_array takes O(numvalues) time.
     *               By taking ownership of the array, we save a malloc and memcpy,
     *               and possibly a free (if the caller is done with the array).
     */
    __attribute__((nonnull))
    void create_steal_sorted_array(omtdata_t **const values, const uint32_t numvalues, const uint32_t new_capacity);

    /**
     * Effect: Create a new OMT, storing it in *newomt.
     *  The values to the right of index (starting at index) are moved to *newomt.
     * Requires: newomt != NULL
     * Returns
     *    0             success,
     *    EINVAL        if index > toku_omt_size(omt)
     * On nonzero return, omt and *newomt are unmodified.
     * Performance: time=O(n)
     * Rationale:  We don't need a split-evenly operation.  We need to split items so that their total sizes
     *  are even, and other similar splitting criteria.  It's easy to split evenly by calling size(), and dividing by two.
     */
    __attribute__((nonnull))
    int split_at(omt *const newomt, const uint32_t idx);

    /**
     * Effect: Appends leftomt and rightomt to produce a new omt.
     *  Creates this as the new omt.
     *  leftomt and rightomt are destroyed.
     * Performance: time=O(n) is acceptable, but one can imagine implementations that are O(\log n) worst-case.
     */
    __attribute__((nonnull))
    void merge(omt *const leftomt, omt *const rightomt);

    /**
     * Effect: Creates a copy of an omt.
     *  Creates this as the clone.
     *  Each element is copied directly.  If they are pointers, the underlying data is not duplicated.
     * Performance: O(n) or the running time of fill_array_with_subtree_values()
     */
    void clone(const omt &src);

    /**
     * Effect: Set the tree to be empty.
     *  Note: Will not reallocate or resize any memory.
     * Performance: time=O(1)
     */
    void clear(void);

    /**
     * Effect:  Destroy an OMT, freeing all its memory.
     *   If the values being stored are pointers, their underlying data is not freed.  See free_items()
     *   Those values may be freed before or after calling toku_omt_destroy.
     * Rationale: Returns no values since free() cannot fail.
     * Rationale: Does not free the underlying pointers to reduce complexity.
     * Performance:  time=O(1)
     */
    void destroy(void);

    /**
     * Effect: return |this|.
     * Performance:  time=O(1)
     */
    uint32_t size(void) const;
    

    /**
     * Effect:  Insert value into the OMT.
     *   If there is some i such that $h(V_i, v)=0$ then returns DB_KEYEXIST.
     *   Otherwise, let i be the minimum value such that $h(V_i, v)>0$.
     *      If no such i exists, then let i be |V|
     *   Then this has the same effect as
     *    insert_at(tree, value, i);
     *   If idx!=NULL then i is stored in *idx
     * Requires:  The signum of h must be monotonically increasing.
     * Returns:
     *    0            success
     *    DB_KEYEXIST  the key is present (h was equal to zero for some value)
     * On nonzero return, omt is unchanged.
     * Performance: time=O(\log N) amortized.
     * Rationale: Some future implementation may be O(\log N) worst-case time, but O(\log N) amortized is good enough for now.
     */
    template<typename omtcmp_t, int (*h)(const omtdata_t &, const omtcmp_t &)>
    int insert(const omtdata_t &value, const omtcmp_t &v, uint32_t *const idx);

    /**
     * Effect: Increases indexes of all items at slot >= idx by 1.
     *         Insert value into the position at idx.
     * Returns:
     *   0         success
     *   EINVAL    if idx > this->size()
     * On error, omt is unchanged.
     * Performance: time=O(\log N) amortized time.
     * Rationale: Some future implementation may be O(\log N) worst-case time, but O(\log N) amortized is good enough for now.
     */
    int insert_at(const omtdata_t &value, const uint32_t idx);

    /**
     * Effect:  Replaces the item at idx with value.
     * Returns:
     *   0       success
     *   EINVAL  if idx>=this->size()
     * On error, omt is unchanged.
     * Performance: time=O(\log N)
     * Rationale: The FT needs to be able to replace a value with another copy of the same value (allocated in a different location)
     * 
     */
    int set_at(const omtdata_t &value, const uint32_t idx);

    /**
     * Effect: Delete the item in slot idx.
     *         Decreases indexes of all items at slot > idx by 1.
     * Returns
     *     0            success
     *     EINVAL       if idx>=this->size()
     * On error, omt is unchanged.
     * Rationale: To delete an item, first find its index using find or find_zero, then delete it.
     * Performance: time=O(\log N) amortized.
     */
    int delete_at(const uint32_t idx);

    /**
     * Effect:  Iterate over the values of the omt, from left to right, calling f on each value.
     *  The first argument passed to f is a ref-to-const of the value stored in the omt.
     *  The second argument passed to f is the index of the value.
     *  The third argument passed to f is iterate_extra.
     *  The indices run from 0 (inclusive) to this->size() (exclusive).
     * Requires: f != NULL
     * Returns:
     *  If f ever returns nonzero, then the iteration stops, and the value returned by f is returned by iterate.
     *  If f always returns zero, then iterate returns 0.
     * Requires:  Don't modify the omt while running.  (E.g., f may not insert or delete values from the omt.)
     * Performance: time=O(i+\log N) where i is the number of times f is called, and N is the number of elements in the omt.
     * Rationale: Although the functional iterator requires defining another function (as opposed to C++ style iterator), it is much easier to read.
     * Rationale: We may at some point use functors, but for now this is a smaller change from the old OMT.
     */
    template<typename iterate_extra_t,
             int (*f)(const omtdata_t &, const uint32_t, iterate_extra_t *const)>
    int iterate(iterate_extra_t *const iterate_extra) const;

    /**
     * Effect:  Iterate over the values of the omt, from left to right, calling f on each value.
     *  The first argument passed to f is a ref-to-const of the value stored in the omt.
     *  The second argument passed to f is the index of the value.
     *  The third argument passed to f is iterate_extra.
     *  The indices run from 0 (inclusive) to this->size() (exclusive).
     *  We will iterate only over [left,right)
     *
     * Requires: left <= right
     * Requires: f != NULL
     * Returns:
     *  EINVAL  if right > this->size()
     *  If f ever returns nonzero, then the iteration stops, and the value returned by f is returned by iterate_on_range.
     *  If f always returns zero, then iterate_on_range returns 0.
     * Requires:  Don't modify the omt while running.  (E.g., f may not insert or delete values from the omt.)
     * Performance: time=O(i+\log N) where i is the number of times f is called, and N is the number of elements in the omt.
     * Rational: Although the functional iterator requires defining another function (as opposed to C++ style iterator), it is much easier to read.
     */
    template<typename iterate_extra_t,
             int (*f)(const omtdata_t &, const uint32_t, iterate_extra_t *const)>
    int iterate_on_range(const uint32_t left, const uint32_t right, iterate_extra_t *const iterate_extra) const;

    /**
     * Effect:  Iterate over the values of the omt, from left to right, calling f on each value.
     *  The first argument passed to f is a pointer to the value stored in the omt.
     *  The second argument passed to f is the index of the value.
     *  The third argument passed to f is iterate_extra.
     *  The indices run from 0 (inclusive) to this->size() (exclusive).
     * Requires: same as for iterate()
     * Returns: same as for iterate()
     * Performance: same as for iterate()
     * Rationale: In general, most iterators should use iterate() since they should not modify the data stored in the omt.  This function is for iterators which need to modify values (for example, free_items).
     * Rationale: We assume if you are transforming the data in place, you want to do it to everything at once, so there is not yet an iterate_on_range_ptr (but there could be).
     */
    template<typename iterate_extra_t,
             int (*f)(omtdata_t *, const uint32_t, iterate_extra_t *const)>
    void iterate_ptr(iterate_extra_t *const iterate_extra);

    /**
     * Effect: Set *value=V_idx
     * Returns
     *    0             success
     *    EINVAL        if index>=toku_omt_size(omt)
     * On nonzero return, *value is unchanged
     * Performance: time=O(\log N)
     */
    int fetch(const uint32_t idx, omtdataout_t *const value) const;
    

    /**
     * Effect:  Find the smallest i such that h(V_i, extra)>=0
     *  If there is such an i and h(V_i,extra)==0 then set *idxp=i, set *value = V_i, and return 0.
     *  If there is such an i and h(V_i,extra)>0  then set *idxp=i and return DB_NOTFOUND.
     *  If there is no such i then set *idx=this->size() and return DB_NOTFOUND.
     * Note: value is of type omtdataout_t, which may be of type (omtdata_t) or (omtdata_t *) but is fixed by the instantiation.
     *  If it is the value type, then the value is copied out (even if the value type is a pointer to something else)
     *  If it is the pointer type, then *value is set to a pointer to the data within the omt.
     *  This is determined by the type of the omt as initially declared.
     *   If the omt is declared as omt<foo_t>, then foo_t's will be stored and foo_t's will be returned by find and related functions.
     *   If the omt is declared as omt<foo_t, foo_t *>, then foo_t's will be stored, and pointers to the stored items will be returned by find and related functions.
     * Rationale:
     *  Structs too small for malloc should be stored directly in the omt.
     *  These structs may need to be edited as they exist inside the omt, so we need a way to get a pointer within the omt.
     *  Using separate functions for returning pointers and values increases code duplication and reduces type-checking.
     *  That also reduces the ability of the creator of a data structure to give advice to its future users.
     *  Slight overloading in this case seemed to provide a better API and better type checking.
     */
    template<typename omtcmp_t,
             int (*h)(const omtdata_t &, const omtcmp_t &)>
    int find_zero(const omtcmp_t &extra, omtdataout_t *const value, uint32_t *const idxp) const;

    /**
     *   Effect:
     *    If direction >0 then find the smallest i such that h(V_i,extra)>0.
     *    If direction <0 then find the largest  i such that h(V_i,extra)<0.
     *    (Direction may not be equal to zero.)
     *    If value!=NULL then store V_i in *value
     *    If idxp!=NULL then store i in *idxp.
     *   Requires: The signum of h is monotically increasing.
     *   Returns
     *      0             success
     *      DB_NOTFOUND   no such value is found.
     *   On nonzero return, *value and *idxp are unchanged
     *   Performance: time=O(\log N)
     *   Rationale:
     *     Here's how to use the find function to find various things
     *       Cases for find:
     *        find first value:         ( h(v)=+1, direction=+1 )
     *        find last value           ( h(v)=-1, direction=-1 )
     *        find first X              ( h(v)=(v< x) ? -1 : 1    direction=+1 )
     *        find last X               ( h(v)=(v<=x) ? -1 : 1    direction=-1 )
     *        find X or successor to X  ( same as find first X. )
     *
     *   Rationale: To help understand heaviside functions and behavor of find:
     *    There are 7 kinds of heaviside functions.
     *    The signus of the h must be monotonically increasing.
     *    Given a function of the following form, A is the element
     *    returned for direction>0, B is the element returned
     *    for direction<0, C is the element returned for
     *    direction==0 (see find_zero) (with a return of 0), and D is the element
     *    returned for direction==0 (see find_zero) with a return of DB_NOTFOUND.
     *    If any of A, B, or C are not found, then asking for the
     *    associated direction will return DB_NOTFOUND.
     *    See find_zero for more information.
     *
     *    Let the following represent the signus of the heaviside function.
     *
     *    -...-
     *        A
     *         D
     *
     *    +...+
     *    B
     *    D
     *
     *    0...0
     *    C
     *
     *    -...-0...0
     *        AC
     *
     *    0...0+...+
     *    C    B
     *
     *    -...-+...+
     *        AB
     *         D
     *
     *    -...-0...0+...+
     *        AC    B
     */
    template<typename omtcmp_t,
             int (*h)(const omtdata_t &, const omtcmp_t &)>
    int find(const omtcmp_t &extra, int direction, omtdataout_t *const value, uint32_t *const idxp) const;

    /**
     * Effect: Return the size (in bytes) of the omt, as it resides in main memory.  If the data stored are pointers, don't include the size of what they all point to.
     */
    size_t memory_size(void);

private:
    typedef uint32_t node_idx;
    enum {
        NODE_NULL = UINT32_MAX
    };

    struct omt_node {
        uint32_t weight;
        node_idx left;
        node_idx right;
        omtdata_t value;
    } __attribute__((__packed__));

    struct omt_array {
        uint32_t start_idx;
        uint32_t num_values;
        omtdata_t *values;
    };

    struct omt_tree {
        node_idx root;
        node_idx free_idx;
        struct omt_node *nodes;
    };

    bool is_array;
    uint32_t capacity;
    union {
        struct omt_array a;
        struct omt_tree t;
    } d;


    void create_internal_no_array(const uint32_t new_capacity);

    void create_internal(const uint32_t new_capacity);

    uint32_t nweight(const node_idx idx) const;

    node_idx node_malloc(void);

    void node_free(const node_idx idx);

    void maybe_resize_array(const uint32_t n);

    __attribute__((nonnull))
    void fill_array_with_subtree_values(omtdata_t *const array, const node_idx tree_idx) const;

    void convert_to_array(void);

    __attribute__((nonnull))
    void rebuild_from_sorted_array(node_idx *const n_idxp, const omtdata_t *const values, const uint32_t numvalues);

    void convert_to_tree(void);

    void maybe_resize_or_convert(const uint32_t n);

    bool will_need_rebalance(const node_idx n_idx, const int leftmod, const int rightmod) const;

    __attribute__((nonnull))
    void insert_internal(node_idx *const n_idxp, const omtdata_t &value, const uint32_t idx, node_idx **const rebalance_idx);

    void set_at_internal_array(const omtdata_t &value, const uint32_t idx);

    void set_at_internal(const node_idx n_idx, const omtdata_t &value, const uint32_t idx);

    void delete_internal(node_idx *const n_idxp, const uint32_t idx, omt_node *const copyn, node_idx **const rebalance_idx);

    template<typename iterate_extra_t,
             int (*f)(const omtdata_t &, const uint32_t, iterate_extra_t *const)>
    int iterate_internal_array(const uint32_t left, const uint32_t right,
                                      iterate_extra_t *const iterate_extra) const;

    template<typename iterate_extra_t,
             int (*f)(omtdata_t *, const uint32_t, iterate_extra_t *const)>
    void iterate_ptr_internal(const uint32_t left, const uint32_t right,
                                     const node_idx n_idx, const uint32_t idx,
                                     iterate_extra_t *const iterate_extra);

    template<typename iterate_extra_t,
             int (*f)(omtdata_t *, const uint32_t, iterate_extra_t *const)>
    void iterate_ptr_internal_array(const uint32_t left, const uint32_t right,
                                           iterate_extra_t *const iterate_extra);

    template<typename iterate_extra_t,
             int (*f)(const omtdata_t &, const uint32_t, iterate_extra_t *const)>
    int iterate_internal(const uint32_t left, const uint32_t right,
                                const node_idx n_idx, const uint32_t idx,
                                iterate_extra_t *const iterate_extra) const;

    void fetch_internal_array(const uint32_t i, omtdataout_t *value) const;

    void fetch_internal(const node_idx idx, const uint32_t i, omtdataout_t *value) const;

    __attribute__((nonnull))
    void fill_array_with_subtree_idxs(node_idx *const array, const node_idx tree_idx) const;

    __attribute__((nonnull))
    void rebuild_subtree_from_idxs(node_idx *const n_idxp, const node_idx *const idxs, const uint32_t numvalues);

    __attribute__((nonnull))
    void rebalance(node_idx *const n_idxp);

    __attribute__((nonnull))
    static void copyout(omtdata_t *const out, const omt_node *const n);

    __attribute__((nonnull))
    static void copyout(omtdata_t **const out, omt_node *const n);

    __attribute__((nonnull))
    static void copyout(omtdata_t *const out, const omtdata_t *const stored_value_ptr);

    __attribute__((nonnull))
    static void copyout(omtdata_t **const out, omtdata_t *const stored_value_ptr);

    template<typename omtcmp_t,
             int (*h)(const omtdata_t &, const omtcmp_t &)>
    int find_internal_zero_array(const omtcmp_t &extra, omtdataout_t *value, uint32_t *const idxp) const;

    template<typename omtcmp_t,
             int (*h)(const omtdata_t &, const omtcmp_t &)>
    int find_internal_zero(const node_idx n_idx, const omtcmp_t &extra, omtdataout_t *value, uint32_t *const idxp) const;

    template<typename omtcmp_t,
             int (*h)(const omtdata_t &, const omtcmp_t &)>
    int find_internal_plus_array(const omtcmp_t &extra, omtdataout_t *value, uint32_t *const idxp) const;

    template<typename omtcmp_t,
             int (*h)(const omtdata_t &, const omtcmp_t &)>
    int find_internal_plus(const node_idx n_idx, const omtcmp_t &extra, omtdataout_t *value, uint32_t *const idxp) const;

    template<typename omtcmp_t,
             int (*h)(const omtdata_t &, const omtcmp_t &)>
    int find_internal_minus_array(const omtcmp_t &extra, omtdataout_t *value, uint32_t *const idxp) const;

    template<typename omtcmp_t,
             int (*h)(const omtdata_t &, const omtcmp_t &)>
    int find_internal_minus(const node_idx n_idx, const omtcmp_t &extra, omtdataout_t *value, uint32_t *const idxp) const;

    __attribute__((nonnull))
    static int deep_clone_iter(const omtdata_t &value, const uint32_t idx, omt *const dest);

    static int free_items_iter(omtdata_t *value, const uint32_t UU(idx), void *const UU(unused));
public:
    /**
     * Effect: Iterate over the values of the omt, from left to right, freeing each value with toku_free
     * Requires: all items in OMT to have been malloced with toku_malloc
     * Rational: This function was added due to a problem encountered in ft-ops.c. We needed to free the elements and then
     *   destroy the OMT. However, destroying the OMT requires invalidating cursors. This cannot be done if the values of the OMT
     *   have been already freed. So, this function is written to invalidate cursors and free items.
     */
    void free_items(void);

    /**
     * Effect: Creates a copy of an omt.
     *  Creates this as the clone.
     *  Each element is assumed to be a pointer, and the underlying data is duplicated for the clone using toku_malloc.
     * Performance: the running time of iterate()
     */
    void deep_clone(const omt &src);

};

} // namespace toku

// include the implementation here
#include "omt-tmpl.cc"

#endif  /* #ifndef OMT_TMPL_H */
