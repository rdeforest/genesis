/*
// Full copyright information is available in the file ../doc/CREDITS
*/

#define _object_

#include "defs.h"

#include <string.h>
#include "cdc_pcode.h"
#include "cdc_db.h"
#include "util.h"
#include "log.h"
#include "quickhash.h"

static void method_cache_invalidate(cObjnum objnum);

Int ancestor_cache_hits = 0;
Int ancestor_cache_sets = 0;
Int ancestor_cache_misses = 0;
Int ancestor_cache_collisions = 0;
Int ancestor_cache_invalidates = 0;
cList * ancestor_cache_history = NULL;

Int method_cache_hits = 0;
Int method_cache_sets = 0;
Int method_cache_misses = 0;
Int method_cache_partials = 0;
Int method_cache_collisions = 0;
Int method_cache_invalidates = 0;
cList * method_cache_history = NULL;

cList * ancestor_cache_info()
{
    cList * entry;
    cData * d;
    Int used_buckets, i;

    used_buckets = 0;
    for (i = 0; i < ANCESTOR_CACHE_SIZE; i++) {
        if (ancestor_cache[i].stamp == cur_anc_stamp)
            used_buckets++;
    }

    entry = list_new(7);
    d = list_empty_spaces(entry, 7);

    d[0].type = INTEGER;
    d[0].u.val = ancestor_cache_invalidates;
    d[1].type = INTEGER;
    d[1].u.val = ancestor_cache_hits;
    d[2].type = INTEGER;
    d[2].u.val = ancestor_cache_misses;
    d[3].type = INTEGER;
    d[3].u.val = ancestor_cache_sets;
    d[4].type = INTEGER;
    d[4].u.val = ancestor_cache_collisions;
    d[5].type = INTEGER;
    d[5].u.val = used_buckets;
    d[6].type = INTEGER;
    d[6].u.val = ANCESTOR_CACHE_SIZE;

    return entry;
}

static inline void ancestor_cache_invalidate()
{
    cList * entry;
    cData   list_entry;

    entry = ancestor_cache_info();

    list_entry.type = LIST;
    list_entry.u.list = entry;

    ancestor_cache_history = list_add(ancestor_cache_history, &list_entry);
    list_discard(entry);

    if (list_length(ancestor_cache_history) >= cache_history_size) {
        cList * sublist, * oldlist;
        Int start;

        start = list_length(ancestor_cache_history) - cache_history_size;
        sublist = list_sublist(list_dup(ancestor_cache_history), start,
                               cache_history_size);
        oldlist = ancestor_cache_history;
        ancestor_cache_history = sublist;
        list_discard(oldlist);
    }

    ancestor_cache_invalidates++;
    ancestor_cache_hits = 0;
    ancestor_cache_misses = 0;
    ancestor_cache_sets = 0;
    ancestor_cache_collisions = 0;
    cur_anc_stamp++;
}

/*
// -----------------------------------------------------------------
//
// Error-checking on parents is the job of the calling function.  Also,
// dire things may happen if an object numbered objnum already exists.
//
*/

Obj * object_new(Long objnum, cList * parents) {
    Obj   * cnew;
    Int     i;
#ifdef USE_PARENT_OBJS
    cData * d, cthis;
#endif

    if (objnum == -1)
	objnum = db_top++;
    else if (objnum >= db_top)
	db_top = objnum + 1;

    cnew = cache_get_holder(objnum);
    cnew->parents = list_dup(parents);
#ifdef USE_PARENT_OBJS
    cnew->parent_objs = list_new(list_length(parents));
    cthis.type = OBJECT;
    for (d=list_first(parents); d; d=list_next(parents, d))
    {
	cthis.u.object = cache_retrieve(d->u.objnum);
        cnew->parent_objs = list_add(cnew->parent_objs, &cthis);
    }
#endif

    cnew->children = list_new(0);

    /* Initialize variables table and hash table. */
    cnew->vars.tab = EMALLOC(Var, VAR_STARTING_SIZE);
    cnew->vars.hashtab = EMALLOC(Int, VAR_STARTING_SIZE);
    cnew->vars.blanks = 0;
    cnew->vars.size = VAR_STARTING_SIZE;
    for (i = 0; i < VAR_STARTING_SIZE; i++) {
	cnew->vars.hashtab[i] = -1;
	cnew->vars.tab[i].name = -1;
	cnew->vars.tab[i].next = i + 1;
    }
    cnew->vars.tab[VAR_STARTING_SIZE - 1].next = -1;

    /* Initialize methods table and hash table. */
    cnew->methods.tab = EMALLOC(struct mptr, METHOD_STARTING_SIZE);
    cnew->methods.hashtab = EMALLOC(Int, METHOD_STARTING_SIZE);
    cnew->methods.blanks = 0;
    cnew->methods.size = METHOD_STARTING_SIZE;
    for (i = 0; i < METHOD_STARTING_SIZE; i++) {
	cnew->methods.hashtab[i] = -1;
	cnew->methods.tab[i].m = NULL;
	cnew->methods.tab[i].next = i + 1;
    }
    cnew->methods.tab[METHOD_STARTING_SIZE - 1].next = -1;

    /* Initialize string table. */
    cnew->strings = EMALLOC(String_entry, STRING_STARTING_SIZE);
    cnew->strings_size = STRING_STARTING_SIZE;
    cnew->num_strings = 0;

    /* Initialize identifier table. */
    cnew->idents = EMALLOC(Ident_entry, IDENTS_STARTING_SIZE);
    cnew->idents_size = IDENTS_STARTING_SIZE;
    cnew->num_idents = 0;

    cnew->search = START_SEARCH_AT;

    /* Add this object to the children list of parents. */
    object_update_parents(cnew, list_add);

    cnew->objname = -1;
    cnew->conn = NULL;
    cnew->file = NULL;

    /* last still, which is ok, since coming out the gate its active and the
     * cleaner thread should ignore it anyway
     */
    cache_dirty_object(cnew);

    return cnew;
}

/*
// -----------------------------------------------------------------
//
// Free the data on an object, as when we're swapping it out.  Since the object
// probably still exists on disk, we don't free parent and child references;
// also, we don't free code references on methods because that would make
// swapping too difficult.
//
*/
void object_free(Obj *object) {
    Int i;

    /* Free parents and children list. */
    list_discard(object->parents);
#ifdef USE_PARENT_OBJS
    list_discard(object->parent_objs);
#endif
    list_discard(object->children);

    /* Free variable names and contents. */
    for (i = 0; i < object->vars.size; i++) {
	if (object->vars.tab[i].name != -1) {
	    ident_discard(object->vars.tab[i].name);
	    data_discard(&object->vars.tab[i].val);
	}
    }
    efree(object->vars.tab);
    efree(object->vars.hashtab);

    /* Free methods. */
    for (i = 0; i < object->methods.size; i++) {
	if (object->methods.tab[i].m)
	    method_free(object->methods.tab[i].m);
    }
    efree(object->methods.tab);
    efree(object->methods.hashtab);

    /* Discard strings. */
    for (i = 0; i < object->num_strings; i++) {
	if (object->strings[i].str)
	    string_discard(object->strings[i].str);
    }
    efree(object->strings);

    /* Discard identifiers. */
    for (i = 0; i < object->num_idents; i++) {
	if (object->idents[i].id != NOT_AN_IDENT) {
	  ident_discard(object->idents[i].id);
	}
    }
    efree(object->idents);
}

/*
// -----------------------------------------------------------------
//
// Free everything on the object, update parents and descendents, etc.  The
// object is really going to be gone.  We don't want anything left, except for
// the structure it came in, which belongs to the cache.
//
*/

void object_destroy(Obj *object) {
    cList *children;
    cData *d, cthis;
    Obj *kid;
#ifdef USE_PARENT_OBJS
    cData *d2, cthat, cother;
#endif

    /*
     * Invalidate the method cache if object is not a leaf object,
     */
    if (list_length(object->children) != 0) {
        method_cache_invalidate_all();
    }

    /* Invalidate the ancestor cache if the object has any children */
    if (list_length(object->children) != 0) {
        ancestor_cache_invalidate();
    }

    /* remove the object name, if it has one */
    object_del_objname(object);

    /* Tell parents we don't exist any more. */
    object_update_parents(object, list_delete_element);

    /* Tell children the same thing (no function for this, just do it).
     * Also, check if any kid hits zero parents, and reparent it to our
     * parents if it does. */
    children = object->children;

    cthis.type = OBJNUM;
    cthis.u.objnum = object->objnum;
#ifdef USE_PARENT_OBJS
    cthat.type = OBJECT;
    cthat.u.object = object;
    cother.type = OBJECT;
#endif
    for (d = list_first(children); d; d = list_next(children, d)) {
	kid = cache_retrieve(d->u.objnum);
	cache_dirty_object(kid);

	kid->parents = list_delete_element(kid->parents, &cthis);
#ifdef USE_PARENT_OBJS
	kid->parent_objs = list_delete_element(kid->parent_objs, &cthat);
#endif
	if (!kid->parents->len) {
	    list_discard(kid->parents);
	    kid->parents = list_dup(object->parents);
#ifdef USE_PARENT_OBJS
	    list_discard(kid->parent_objs);
	    kid->parent_objs = list_new(list_length(object->parents));
            for (d2=list_first(kid->parents); d2; d2=list_next(kid->parents, d2))
            {
		cother.u.object = cache_retrieve(d2->u.objnum);
                kid->parent_objs = list_add(kid->parent_objs, &cother);
            }
#endif
	    object_update_parents(kid, list_add);
	}
	cache_discard(kid);
    }

    /* boot the connection on this object (if it exists) */
    boot(object);

    /* close the file on this object (if there is one) */
    abort_file(object->file);

    /* Having freed all the stuff we don't normally free, free the stuff that
     * we do normally free. */
    object_free(object);
}

/*
// -----------------------------------------------------------------
*/
INTERNAL void object_update_parents(Obj * object,
				    cList *(*list_op)(cList *, cData *))
{
    Obj * p;
    cList     * parents;
    cData     * d,
               cthis;

    /* Make a data structure for the children list. */
    cthis.type = OBJNUM;
    cthis.u.objnum = object->objnum;

    parents = object->parents;

    for (d = list_first(parents); d; d = list_next(parents, d)) {
	p = cache_retrieve(d->u.objnum);
	cache_dirty_object(p);

	p->children = (*list_op)(p->children, &cthis);

	cache_discard(p);
    }
}

/*
// -----------------------------------------------------------------
*/

INTERNAL Hash * object_ancestors_depth_aux(Long objnum, Hash * h) {
    Obj    * obj;
    cData  * d;
    cData    this;
    cList  * parents;

    obj = cache_retrieve(objnum);
    if (SEARCHED(obj)) {
        cache_discard(obj);
        return h;
    }
    HAVE_SEARCHED(obj);

    parents = list_dup(obj->parents);
    cache_discard(obj);

    for (d = list_last(parents); d; d = list_prev(parents, d))
	h = object_ancestors_depth_aux(d->u.objnum, h);

    list_discard(parents);

    this.type = OBJNUM;
    this.u.objnum = objnum;
    return hash_add(h, &this);
}

cList * object_ancestors_depth(Long objnum) {
    Hash   * h;
    cList  * list;
    cData    d;

    /* short circut root */
    if (objnum == ROOT_OBJNUM) {
        list = list_new(1);
        d.type = OBJNUM;
        d.u.objnum = ROOT_OBJNUM;
        return list_add(list, &d);
    } else {
        h = hash_new(0);
        START_SEARCH();
        h = object_ancestors_depth_aux(objnum, h);
        END_SEARCH();
        list = list_dup(h->keys);
        hash_discard(h);
        return list_reverse(list);
    }
}

cList * object_ancestors_breadth(Long objnum) {
    Hash   * h;
    Obj    * parent;
    int      pos;
    cList  * list;
    cData  * key,
           * c,
             this;

    h = hash_new(1);
    this.type = OBJNUM;
    this.u.objnum = objnum;
    h = hash_add(h, &this);

    /* short circut root */
    if (objnum == ROOT_OBJNUM)
        goto END_LABEL;

    START_SEARCH();

    for (pos=0; list_length(h->keys) > pos; pos++) {
        c = list_elem(h->keys, pos);
        parent = cache_retrieve(c->u.objnum);

        if (SEARCHED(parent)) {
            cache_discard(parent);
            continue;
        }
        HAVE_SEARCHED(parent);

        list = parent->parents;
        for (key = list_first(list); key; key = list_next(list, key)) {
            if (hash_find(h, key) == F_FAILURE)
                h = hash_add(h, key);
        }
        cache_discard(parent);
    }

    END_SEARCH();

    END_LABEL:

    list = list_dup(h->keys);
    hash_discard(h);
    return list;
}

static Bool ancestor_cache_check(cObjnum objnum, cObjnum ancestor,
                                 Bool *is_ancestor)
{
    cObjnum i;

    i = (objnum + ancestor) % ANCESTOR_CACHE_SIZE;

    if ((ancestor_cache[i].stamp == cur_anc_stamp) &&
        (ancestor_cache[i].objnum == objnum) &&
        (ancestor_cache[i].ancestor == ancestor))
    {
        *is_ancestor = ancestor_cache[i].is_ancestor;
        ancestor_cache_hits++;
        return TRUE;
    }

    ancestor_cache_misses++;
    return FALSE;
}

static void ancestor_cache_set(cObjnum objnum, cObjnum ancestor,
                               Bool is_ancestor)
{
    cObjnum i;

    i = (objnum + ancestor) % ANCESTOR_CACHE_SIZE;

    if (ancestor_cache[i].stamp == cur_anc_stamp)
        ancestor_cache_collisions++;
    
    ancestor_cache[i].stamp = cur_anc_stamp;
    ancestor_cache[i].objnum = objnum;
    ancestor_cache[i].ancestor = ancestor;
    ancestor_cache[i].is_ancestor = is_ancestor;

    ancestor_cache_sets++;
}

Int object_has_ancestor(Long objnum, Long ancestor)
{
    Int retv;
    Bool anc_cache_check;

    if (objnum == ancestor)
	return 1;

    if (ancestor_cache_check(objnum, ancestor, &anc_cache_check))
        return anc_cache_check;

    START_SEARCH();
    retv = object_has_ancestor_aux(objnum, ancestor);
    ancestor_cache_set(objnum, ancestor, retv);
    END_SEARCH();
    return retv;
}

static Int object_has_ancestor_aux(Long objnum, Long ancestor)
{
    Obj *object;
    cList *parents;
    cData *d;
    Bool anc_cache_check;

    /* Don't search an object twice. */
    object = cache_retrieve(objnum);
    if (SEARCHED(object)) {
	cache_discard(object);
	return 0;
    }
    HAVE_SEARCHED(object);

    parents = list_dup(object->parents);
    cache_discard(object);

    for (d = list_first(parents); d; d = list_next(parents, d)) {
        /* Do this check first since it is trivial
	 * The result of this will be cached in the
	 * caller. */
	if (d->u.objnum == ancestor) {
            list_discard(parents);
            return 1;
        }
        /* Only fall out if the test is true, since other parents may
           have a true result. */
        if (ancestor_cache_check(d->u.objnum, ancestor, &anc_cache_check) &&
            anc_cache_check) {
	    list_discard(parents);
	    return anc_cache_check;
	}
    }

    for (d = list_first(parents); d; d = list_next(parents, d)) {
	if (object_has_ancestor_aux(d->u.objnum, ancestor)) {
            ancestor_cache_set(d->u.objnum, ancestor, TRUE);
	    list_discard(parents);
	    return 1;
	}
    }

    ancestor_cache_set(objnum, ancestor, FALSE);
    list_discard(parents);
    return 0;
}

Int object_change_parents(Obj *object, cList *parents)
{
    cObjnum parent;
    cData *d;
#ifdef USE_PARENT_OBJS
    cData cthis;
#endif

    /* If the new and old parent lists are equal, then do no work */
    if (list_cmp(object->parents, parents) == 0)
        return -1;

    /* Make sure that all parents are valid objects, and that they don't create
     * any cycles.  If something is wrong, return the index of the parent that
     * caused the problem. */
    for (d = list_first(parents); d; d = list_next(parents, d)) {
	if (d->type != OBJNUM)
	    return d - list_first(parents);
	parent = d->u.objnum;
	if (!cache_check(parent) || object_has_ancestor(parent, object->objnum))
	    return d - list_first(parents);
    }

    /* Invalidate the method cache. */
    /* NOTE:  is there a better way to invalidate this? */
    if (list_length(object->children) != 0) {
        method_cache_invalidate_all();
    } else {
        method_cache_invalidate(object->objnum);
    }

    /* Invalidate the ancestor cache */
    ancestor_cache_invalidate();

    cache_dirty_object(object);

    /* Tell our old parents that we're no longer a kid, and discard the old
     * parents list. */
    object_update_parents(object, list_delete_element);
    list_discard(object->parents);
#ifdef USE_PARENT_OBJS
    list_discard(object->parent_objs);
#endif

    /* Set the object's parents list to a copy of the new list, and tell all
     * our new parents that we're a kid. */
    object->parents = list_dup(parents);
#ifdef USE_PARENT_OBJS
    object->parent_objs = list_new(list_length(parents));
    cthis.type = OBJECT;
    for (d=list_first(parents); d; d=list_next(parents, d))
    {
	cthis.u.object = cache_retrieve(d->u.objnum);
        object->parent_objs = list_add(object->parent_objs, &cthis);
    }
#endif
    object_update_parents(object, list_add);

    /* Return -1, meaning that all the parents were okay. */
    return -1;
}

Int object_add_string(Obj *object, cStr *str)
{
    Int i, blank = -1;

    /* Get the object dirty now, so we can return with a clean conscience. */
    cache_dirty_object(object);

    /* Look for blanks while checking for an equivalent string. */
    for (i = 0; i < object->num_strings; i++) {
	if (!object->strings[i].str) {
	    blank = i;
	} else if (string_cmp(str, object->strings[i].str) == 0) {
	    object->strings[i].refs++;
	    return i;
	}
    }

    /* Fill in a blank if we found one. */
    if (blank != -1) {
	object->strings[blank].str = string_dup(str);
	object->strings[blank].refs = 1;
	return blank;
    }

    /* Check to see if we have to enlarge the table. */
    if (i >= object->strings_size) {
	object->strings_size = object->strings_size * 2 + MALLOC_DELTA;
	object->strings = EREALLOC(object->strings, String_entry,
				   object->strings_size);
    }

    /* Add the string to the end of the table. */
    object->strings[i].str = string_dup(str);
    object->strings[i].refs = 1;
    object->num_strings++;

    return i;
}

void object_discard_string(Obj *object, Int ind)
{
    cache_dirty_object(object);

    object->strings[ind].refs--;
    if (!object->strings[ind].refs) {
	string_discard(object->strings[ind].str);
	object->strings[ind].str = NULL;
    }
}

cStr *object_get_string(Obj *object, Int ind)
{
    return object->strings[ind].str;
}

Int object_add_ident(Obj *object, char *ident)
{
    Int i, blank = -1;
    Long id;

    /* Mark the object dirty, since we will modify it in all cases. */
    cache_dirty_object(object);

    /* Get an identifier for the identifier string. */
    id = ident_get(ident);

    /* Look for blanks while checking for an equivalent identifier. */
    for (i = 0; i < object->num_idents; i++) {
	if (object->idents[i].id == -1) {
	    blank = i;
	} else if (object->idents[i].id == id) {
	    /* We already have this id.  Up the reference count on the object's
	     * copy if it, discard this function's copy of it, and return the
	     * index into the object's identifier table. */
	  object->idents[i].refs++;
	  ident_discard(id);
	  return i;
	}
    }

    /* Fill in a blank if we found one. */
    if (blank != -1) {
	object->idents[blank].id = id;
	object->idents[blank].refs = 1;
	return blank;
    }

    /* Check to see if we have to enlarge the table. */
    if (i >= object->idents_size) {
	object->idents_size = object->idents_size * 2 + MALLOC_DELTA;
	object->idents = EREALLOC(object->idents, Ident_entry,
				  object->idents_size);
    }

    /* Add the string to the end of the table. */
    object->idents[i].id = id;
    object->idents[i].refs = 1;
    object->num_idents++;

    return i;
}

void object_discard_ident(Obj *object, Int ind)
{
    cache_dirty_object(object);

    object->idents[ind].refs--;
    if (!object->idents[ind].refs) {
      /*write_err("##object_discard_ident %d %s",
	object->idents[ind].id, ident_name(object->idents[ind].id));*/

      ident_discard(object->idents[ind].id);
      object->idents[ind].id = NOT_AN_IDENT;
    }
}

Long object_get_ident(Obj *object, Int ind) {
    return object->idents[ind].id;
}

Long object_add_var(Obj *object, Long name) {
    if (object_find_var(object, object->objnum, name))
	return varexists_id;
    object_create_var(object, object->objnum, name);
    return NOT_AN_IDENT;
}

Long object_del_var(Obj *object, Long name)
{
    Int *indp;
    Var *var;

    /* This is the index-thread equivalent of double pointers in a standard
     * linked list.  We traverse the list using pointers to the ->next element
     * of the variables. */
    indp = &object->vars.hashtab[ident_hash(name) % object->vars.size];
    for (; *indp != -1; indp = &object->vars.tab[*indp].next) {
	var = &object->vars.tab[*indp];
	if (var->name == name && var->cclass == object->objnum) {
	    cache_dirty_object(object);

	    /*  write_err("##object_del_var %d %s", var->name, ident_name(var->name));*/
	    ident_discard(var->name);
	    data_discard(&var->val);
	    var->name = -1;

	    /* Remove ind from hash table thread, and add it to blanks
	     * thread. */
	    *indp = var->next;
	    var->next = object->vars.blanks;
	    object->vars.blanks = var - object->vars.tab;

	    return NOT_AN_IDENT;
	}
    }

    return varnf_id;
}

Long object_assign_var(Obj *object, Obj *cclass, Long name, cData *val) {
    Var *var;

    /* Make sure variable exists in cclass (method object). */
    if (!object_find_var(cclass, cclass->objnum, name))
	return varnf_id;

    /* Get variable slot on object, creating it if necessary. */
    var = object_find_var(object, cclass->objnum, name);
    if (!var)
	var = object_create_var(object, cclass->objnum, name);

    cache_dirty_object(object);

    data_discard(&var->val);
    data_dup(&var->val, val);

    return NOT_AN_IDENT;
}

Long object_delete_var(Obj *object, Obj *cclass, Long name) {
    Var *var;
    Int *indp;

    /* find the parameter definition in cclass (method object). */
    if (!object_find_var(cclass, cclass->objnum, name))
        return varnf_id;

    /* Get variable slot on object */
    var = object_find_var(object, cclass->objnum, name);
    if (var) {
        indp=&object->vars.hashtab[ident_hash(name)%object->vars.size];
        for (; *indp != -1; indp = &object->vars.tab[*indp].next) {
            var = &object->vars.tab[*indp];
            if (var->name == name && var->cclass == cclass->objnum) {
                cache_dirty_object(object);

                ident_discard(var->name);
                data_discard(&var->val);
                var->name = -1;

                /* Remove ind from hash table thread, and add it to blanks
                 * thread. */
                *indp = var->next;
                var->next = object->vars.blanks;
                object->vars.blanks = var - object->vars.tab;

                return NOT_AN_IDENT;
            }
        }
    }

    return varnf_id;
}

Long object_retrieve_var(Obj *object, Obj *cclass, Long name, cData *ret)
{
    Var *var;

    /* Make sure variable exists on cclass. */
    if (!object_find_var(cclass, cclass->objnum, name))
	return varnf_id;

    var = object_find_var(object, cclass->objnum, name);
    if (var) {
	data_dup(ret, &var->val);
    } else {
	ret->type = INTEGER;
	ret->u.val = 0;
    }

    return NOT_AN_IDENT;
}

Long object_default_var(Obj *object, Obj *cclass, Long name, cData *ret)
{
    Var * var,
        * defvar;

    /* Make sure variable exists on cclass. */
    if (!(defvar = object_find_var(cclass, cclass->objnum, name)))
        return varnf_id;

    var = object_find_var(object, cclass->objnum, name);
    if (var) {
        data_dup(ret, &var->val);
    } else {
        data_dup(ret, &defvar->val);
    }

    return NOT_AN_IDENT;
}

Long object_inherited_var(Obj *object, Obj *cclass, Long name, cData *ret)
{
    Var   * var, * dvar;
    cList * ancestors;
    cData * d;
    Obj   * a;

    /* Make sure variable exists on cclass. */
    if (!(dvar = object_find_var(cclass, cclass->objnum, name)))
        return varnf_id;

    var = object_find_var(object, cclass->objnum, name);
    if (var) {
        data_dup(ret, &var->val);
    } else {
        /* Unless the database is corrupt, we *will* find an ancestor with
           the var--this is a horrible and inefficient way of doing this */
        ancestors = object_ancestors_breadth(object->objnum);
        for (d = list_first(ancestors);
             d->u.objnum != cclass->objnum;
             d = list_next(ancestors, d))
        {
            a = cache_retrieve(d->u.objnum);
            if ((var = object_find_var(a, cclass->objnum, name))) {
                data_dup(ret, &var->val);
                cache_discard(a);
                goto DONE;
            }
            cache_discard(a);
        }

        /* If we didn't find it above, default to the definer's value */
        data_dup(ret, &dvar->val);

        DONE:
        list_discard(ancestors);
    }

    return NOT_AN_IDENT;
}

/* Only the text dump reader calls this function; it assigns or creates a
 * variable as needed, and always succeeds. */
void object_put_var(Obj *object, Long cclass, Long name, cData *val)
{
    Var *var;

    var = object_find_var(object, cclass, name);
    if (!var)
	var = object_create_var(object, cclass, name);
    data_discard(&var->val);
    data_dup(&var->val, val);
}

/* Add a variable to an object. */
static Var *object_create_var(Obj *object, Long cclass, Long name)
{
    Var *cnew;
    Int ind;

    cache_dirty_object(object);

    /* If the variable table is full, expand it and its corresponding hash
     * table. */
    if (object->vars.blanks == -1) {
	Int new_size, i;

	/* Compute new size and resize tables. */
	new_size = object->vars.size * 2 + MALLOC_DELTA + 1;
	object->vars.tab = EREALLOC(object->vars.tab, Var, new_size);
	object->vars.hashtab = EREALLOC(object->vars.hashtab, Int, new_size);

	/* Refill hash table. */
	for (i = 0; i < new_size; i++)
	    object->vars.hashtab[i] = -1;
	for (i = 0; i < object->vars.size; i++) {
	    ind = ident_hash(object->vars.tab[i].name) % new_size;
	    object->vars.tab[i].next = object->vars.hashtab[ind];
	    object->vars.hashtab[ind] = i;
	}

	/* Create new thread of blanks, setting names to -1. */
	for (i = object->vars.size; i < new_size; i++) {
	    object->vars.tab[i].name = -1;
	    object->vars.tab[i].next = i + 1;
	}
	object->vars.tab[new_size - 1].next = -1;
	object->vars.blanks = object->vars.size;

	object->vars.size = new_size;
    }

    /* Add variable at first blank. */
    cnew = &object->vars.tab[object->vars.blanks];
    object->vars.blanks = cnew->next;

    /* Fill in new variable. */
    cnew->name = ident_dup(name);
    cnew->cclass = cclass;
    cnew->val.type = INTEGER;
    cnew->val.u.val = 0;

    /* Add variable to hash table thread. */
    ind = ident_hash(name) % object->vars.size;
    cnew->next = object->vars.hashtab[ind];
    object->vars.hashtab[ind] = cnew - object->vars.tab;

    return cnew;
}

/* Look for a variable on an object. */
static Var *object_find_var(Obj *object, Long cclass, Long name)
{
    Int ind;
    Var *var;

    /* Traverse hash table thread, stopping if we get a match on the name. */
    ind = object->vars.hashtab[ident_hash(name) % object->vars.size];
    for (; ind != -1; ind = object->vars.tab[ind].next) {
	var = &object->vars.tab[ind];
	if (var->name == name && var->cclass == cclass)
	    return var;
    }

    return NULL;
}

/* Reference-counting kludge: on return, the method's object field has an
   extra reference count, in order to keep it in cache.  objnum must be
   valid. */
/* object_find_method is now a front end that increments the
   cache_search variable and calls object_find_message_recurse,
   the recursive routine.  This is necessary to make single-
   inheritance lineage obejct re-entrant.  It fixes a bug in
   the circular-definition catching logic that caused it to
   fail when a parent's method sent a message to the child as
   a result of a message to teh child hanbdled by the parent
   (whew.) added 5/7/1995 Jeffrey P. kesselman */
Method *object_find_method(Long objnum, Long name, Bool is_frob) {
    Search_params   params;
    Obj           * object;
    Method        * method, *local_method;
    cList         * parents;
    cData         * d;
    Bool            method_cache_hit;

    /* Look for cached value. */
    method_cache_hit = method_cache_check(objnum, name, -1, is_frob, &method);
    if (method_cache_hit)
	return method;

    object = cache_retrieve(objnum);
    parents = list_dup(object->parents);
    cache_discard(object);

    if (list_length(parents) != 0) {
        /* If the object has parents */
        if (list_length(parents) == 1) {
            /* If it has only one parent, call this function recursively. */
            method = object_find_method(list_elem(parents, 0)->u.objnum, name, is_frob);
        } else {
            /* We've hit a bulge; resort to the reverse depth-first search. */
            START_SEARCH();
            params.name = name;
            params.stop_at = -1;
            params.done = 0;
            params.last_method_found = NULL;
	    params.is_frob = is_frob;
            for (d = list_last(parents); d; d = list_prev(parents, d))
                search_object(d->u.objnum, &params);
            method = params.last_method_found;
            END_SEARCH();
        }
    }

    list_discard(parents);

    /* If we have not found a method defined above, or the top method we
       have found is overridable */
    if (!method || !(method->m_flags & MF_NOOVER)) {
	object = cache_retrieve(objnum);
	local_method = object_find_method_local(object, name, is_frob);
	if (local_method) {
	    if (method)
		cache_discard(method->object);
	    method = local_method;
	} else {
	    cache_discard(object);
	}
    }

    method_cache_set(objnum, name, -1, (method ? method->object->objnum : -2), is_frob, (method ? FALSE : TRUE));
    return method;
}

/* Reference-counting kludge: on return, the method's object field has an extra
 * reference count, in order to keep it in cache.  objnum must be valid. */
Method *object_find_next_method(Long objnum, Long name, Long after, Bool is_frob)
{
    Search_params params;
    Obj *object;
    Method *method;
    cList *parents;
    cData *d;
    Long parent;
    Bool method_cache_hit;

    /* Check cache. */
    method_cache_hit = method_cache_check(objnum, name, after, is_frob, &method);
    if (method_cache_hit)
	return method;

    object = cache_retrieve(objnum);
    parents = object->parents;

    if (list_length(parents) == 1) {
	/* Object has only one parent; search recursively. */
	parent = list_elem(parents, 0)->u.objnum;
	cache_discard(object);
	if (objnum == after)
	    method = object_find_method(parent, name, is_frob);
	else
	    method = object_find_next_method(parent, name, after, is_frob);
    } else {
	/* Object has more than one parent; use complicated search. */
	START_SEARCH();
	params.name = name;
	params.stop_at = (objnum == after) ? -1 : after;
	params.done = 0;
	params.last_method_found = NULL;
	params.is_frob = is_frob;
	for (d = list_last(parents); d; d = list_prev(parents, d))
	    search_object(d->u.objnum, &params);
	cache_discard(object);
	method = params.last_method_found;
	END_SEARCH();
    }

    method_cache_set(objnum, name, after, (method ? method->object->objnum : -2), is_frob, (method ? FALSE : TRUE));
    return method;
}

/* Perform a reverse depth-first traversal of this object and its ancestors
 * with no repeat visits, thus searching ancestors before children and
 * searching parents right-to-left.  We will take the last method we find,
 * possibly stopping at a method if we were looking for the next method after
 * a given method. */
static void search_object(Long objnum, Search_params *params)
{
    Obj *object;
    Method *method;
    cList *parents;
    cData *d;

    RETRIEVE_ONCE_OR_RETURN(object, objnum);

    /* Grab the parents list and discard the object. */
    parents = list_dup(object->parents);
    cache_discard(object);

    /* Traverse the parents list backwards. */
    for (d = list_last(parents); d; d = list_prev(parents, d))
	search_object(d->u.objnum, params);
    list_discard(parents);

    /* If the search is done, don't visit this object. */
    if (params->done)
	return;

    /* If we were searching for a next method after a given object, then this
     * might be the given object, in which case we should stop. */
    if (objnum == params->stop_at) {
	params->done = 1;
	return;
    }

    /* Visit this object.  First, get it back from the cache. */
    object = cache_retrieve(objnum);
    method = object_find_method_local(object, params->name, params->is_frob);
    if (method) {
	/* We found a method on this object.  Discard the reference count on
	 * the last method found's object, if we have one, and set this method
	 * as the last one found.  Leave object's reference count there, since
	 * we don't want it to get swapped out. */
	if (params->last_method_found)
	    cache_discard(params->last_method_found->object);
	params->last_method_found = method;

	/* If this method is non-overridable, the search is done. */
        if (method->m_flags & MF_NOOVER)
	    params->done = 1;
    } else {
	cache_discard(object);
    }
}

/* Look for a method on an object. */
Method *object_find_method_local(Obj *object, Long name, Bool is_frob)
{
    Int ind, method;
    Method *meth;

    /* Traverse hash table thread, stopping if we get a match on the name. */
    ind = ident_hash(name) % object->methods.size;
    method = object->methods.hashtab[ind];
    if (is_frob == FROB_ANY) {
	for (; method != -1; method = object->methods.tab[method].next) {
	    meth=object->methods.tab[method].m;
	    if (meth->name == name)
		return object->methods.tab[method].m;
	}
    }
    else if (is_frob == FROB_YES) {
	for (; method != -1; method = object->methods.tab[method].next) {
	    meth=object->methods.tab[method].m;
	    if (meth->name == name && meth->m_access == MS_FROB )
		return object->methods.tab[method].m;
	}
    }
    else {
	for (; method != -1; method = object->methods.tab[method].next) {
	    meth=object->methods.tab[method].m;
	    if (meth->name == name && meth->m_access != MS_FROB )
		return object->methods.tab[method].m;
	}
    }

    return NULL;
}

static Bool method_cache_check(Long objnum, Long name, Long after, Bool is_frob, Method **method)
{
    Obj *object;
    Int i;

    i = (10 + objnum + (name << 4) + (is_frob << 8) + after) % METHOD_CACHE_SIZE;
    if (method_cache[i].stamp == cur_stamp && method_cache[i].objnum == objnum &&
	method_cache[i].name == name && method_cache[i].after == after &&
	method_cache[i].loc != -1 && method_cache[i].is_frob==is_frob) {
        method_cache_hits++;
        if (!method_cache[i].failed) {
            object = cache_retrieve(method_cache[i].loc);
            *method = object_find_method_local(object, name, is_frob);
            return TRUE;
        } else {
            *method = NULL;
            return TRUE;
        }
    } else {
        method_cache_misses++;
	*method = NULL;
	return FALSE;
    }
}

static void method_cache_set(Long objnum, Long name, Long after, Long loc, Bool is_frob, Bool failed)
{
    Int i;

    i = (10 + objnum + (name << 4) + (is_frob << 8) + after) % METHOD_CACHE_SIZE;
    if (method_cache[i].stamp != 0) {
      ident_discard(method_cache[i].name);
      if (method_cache[i].stamp == cur_stamp)
          method_cache_collisions++;
    }
    method_cache[i].stamp = cur_stamp;
    method_cache[i].objnum = objnum;
    method_cache[i].name = ident_dup(name);
    method_cache[i].after = after;
    method_cache[i].loc = loc;
    method_cache[i].is_frob=(is_frob == FROB_RETRY) ? FROB_NO : is_frob;
    method_cache[i].failed = failed;

    method_cache_sets++;
}

cList * method_cache_info() {
    cList * entry;
    cData * d;
    Int used_buckets, i;

    used_buckets = 0;
    for (i = 0; i < METHOD_CACHE_SIZE; i++) {
        if (method_cache[i].stamp == cur_stamp)
            used_buckets++;
    }

    entry = list_new(8);
    d = list_empty_spaces(entry, 8);

    d[0].type = INTEGER;
    d[0].u.val = method_cache_invalidates;
    d[1].type = INTEGER;
    d[1].u.val = method_cache_hits;
    d[2].type = INTEGER;
    d[2].u.val = method_cache_misses;
    d[3].type = INTEGER;
    d[3].u.val = method_cache_partials;
    d[4].type = INTEGER;
    d[4].u.val = method_cache_sets;
    d[5].type = INTEGER;
    d[5].u.val = method_cache_collisions;
    d[6].type = INTEGER;
    d[6].u.val = used_buckets;
    d[7].type = INTEGER;
    d[7].u.val = METHOD_CACHE_SIZE;

    return entry;
}

static void method_cache_invalidate(cObjnum objnum) {
    Int i;

    /*
     * Invalidate cache entries by decrementing the stamp.
     * Don't set it to 0, that way method_cache_set() can handle
     * the ident_discard() properly
     * Make sure that the stamp is already greater than 0
     * to avoid false invalidations where objnum == 0, so
     * it will catch all of the uninitialized entries in the
     * cache, who will have objnum == 0, but also stamp == 0.
     */
    for (i = 0; i < METHOD_CACHE_SIZE; i++) {
        if ((method_cache[i].objnum == objnum) &&
            (method_cache[i].stamp > 0)) {
            method_cache[i].stamp = 1;
        }
    }

    method_cache_partials++;
    
    if (log_method_cache == 2) {
        write_err("Method cache partially invalidated for obj #%l", objnum);
        log_current_task_stack(FALSE, write_err);
    }
}

static void method_cache_invalidate_all() {
    cList * entry;
    cData   list_entry;

    if (log_method_cache) {
        write_err("Method cache entirely invalidated:");
        log_current_task_stack(FALSE, write_err);
    }

    entry = method_cache_info();

    list_entry.type = LIST;
    list_entry.u.list = entry;

    method_cache_history = list_add(method_cache_history, &list_entry);
    list_discard(entry);

    if (list_length(method_cache_history) >= cache_history_size) {
        cList * sublist, * oldlist;
        Int start;

        start = list_length(method_cache_history) - cache_history_size;
        sublist = list_sublist(list_dup(method_cache_history), start,
                               cache_history_size);
        oldlist = method_cache_history;
        method_cache_history = sublist;
        list_discard(oldlist);
    }


    method_cache_invalidates++;
    method_cache_hits = 0;
    method_cache_misses = 0;
    method_cache_partials = 0;
    method_cache_sets = 0;
    method_cache_collisions = 0;
    cur_stamp++;
}


/* this makes me rather wary, hope it works ... */
/* we will know native methods have changed names because the name will
   be different from the one in the initialization table */
Int object_rename_method(Obj * object, Long oname, Long nname) {
    Method * method;

    method = object_find_method_local(object, oname, FROB_ANY);
    if (!method)
        return 0;

    method = method_dup(method);
    object_del_method(object, oname, FALSE);
    object_add_method(object, nname, method);
    method_discard(method);

    return 1;
}

void object_add_method(Obj *object, Long name, Method *method) {
    Int ind, hval;

    cache_dirty_object(object);

    /* Delete the method if it previous existed, calling this on a
       locked method WILL CAUSE PROBLEMS, make sure you check before
       calling this. */
    if (object_del_method(object, name, TRUE) != 1) {
        /* Invalidate the method cache. */
        if (list_length(object->children) != 0) {
            method_cache_invalidate_all();
        } else {
            method_cache_invalidate(object->objnum);
        }
    }

    /* If the method table is full, expand it and its corresponding hash
     * table. */
    if (object->methods.blanks == -1) {
	Int new_size, i, ind;

	/* Compute new size and resize tables. */
	new_size = object->methods.size * 2 + MALLOC_DELTA + 1;
	object->methods.tab = EREALLOC(object->methods.tab, struct mptr,
				       new_size);
	object->methods.hashtab = EREALLOC(object->methods.hashtab, Int,
					   new_size);

	/* Refill hash table. */
	for (i = 0; i < new_size; i++)
	    object->methods.hashtab[i] = -1;
	for (i = 0; i < object->methods.size; i++) {
	    ind = ident_hash(object->methods.tab[i].m->name) % new_size;
	    object->methods.tab[i].next = object->methods.hashtab[ind];
	    object->methods.hashtab[ind] = i;
	}

	/* Create new thread of blanks and set method pointers to null. */
	for (i = object->methods.size; i < new_size; i++) {
	    object->methods.tab[i].m = NULL;
	    object->methods.tab[i].next = i + 1;
	}
	object->methods.tab[new_size - 1].next = -1;
	object->methods.blanks = object->methods.size;

	object->methods.size = new_size;
    }

    method->object = object;
    method->name = ident_dup(name);

    /* Add method at first blank. */
    ind = object->methods.blanks;
    object->methods.blanks = object->methods.tab[ind].next;
    object->methods.tab[ind].m = method_dup(method);

    /* Add method to hash table thread. */
    hval = ident_hash(name) % object->methods.size;
    object->methods.tab[ind].next = object->methods.hashtab[hval];
    object->methods.hashtab[hval] = ind;

}

Int object_del_method(Obj *object, Long name, Bool replacing) {
    Int *indp, ind;

    /* This is the index-thread equivalent of double pointers in a standard
     * linked list.  We traverse the list using pointers to the ->next element
     * of the method pointers. */
    ind = ident_hash(name) % object->methods.size;
    indp = &object->methods.hashtab[ind];
    for (; *indp != -1; indp = &object->methods.tab[*indp].next) {
	ind = *indp;
	if (object->methods.tab[ind].m->name == name) {
            /* check the lock at a higher level, this is a better
               location to put it, but it causes logistic problems */
            /* ack, we found it, but its locked! */
            if (object->methods.tab[ind].m->m_flags & MF_LOCK)
                return -1;

            cache_dirty_object(object);

	    /* ok, we can discard it. */
	    method_discard(object->methods.tab[ind].m);
	    object->methods.tab[ind].m = NULL;

	    /* Remove ind from the hash table thread, and add it to the blanks
	     * thread. */
	    *indp = object->methods.tab[ind].next;
	    object->methods.tab[ind].next = object->methods.blanks;
	    object->methods.blanks = ind;

	    if (replacing == FALSE) {
                /* Invalidate the method cache. */
                if (list_length(object->children) != 0) {
                    method_cache_invalidate_all();
                } else {
                    method_cache_invalidate(object->objnum);
                }
            }

	    /* Return one, meaning the method was successfully deleted. */
	    return 1;
	}
    }

    /* Return zero, meaning no method was found to delete. */
    return 0;
}

cList *object_list_method(Obj *object, Long name, Int indent, int fflags)
{
    Method *method;

    method = object_find_method_local(object, name, FROB_ANY);
    return (method) ? decompile(method, object, indent, fflags) : NULL;
}

Int object_get_method_flags(Obj * object, Long name) {
    Method * method;

    method = object_find_method_local(object, name, FROB_ANY);
    return (method) ? method->m_flags : -1;
}

Int object_set_method_flags(Obj * object, Long name, Int flags) {
    Method * method;

    method = object_find_method_local(object, name, FROB_ANY);
    if (method == NULL)
        return -1;

    cache_dirty_object(object);

    if ((!(method->m_flags & MF_NOOVER) && (flags & MF_NOOVER)) ||
        ((method->m_flags & MF_NOOVER) && !(flags & MF_NOOVER))) {
        method_cache_invalidate_all();
    }

    method->m_flags = flags;
    return flags;
}

Int object_get_method_access(Obj * object, Long name) {
    Method * method;

    method = object_find_method_local(object, name, FROB_ANY);
    return (method) ? method->m_access : -1;
}

Int object_set_method_access(Obj * object, Long name, Int access) {
    Method * method;

    method = object_find_method_local(object, name, FROB_ANY);
    if (method == NULL)
        return -1;
    if (method->m_access == access) {
        /* yay, we don't have to do anything, let's go home. */
        return access;
    }
    if ((method->m_access == MS_FROB) || (access == MS_FROB)) {
        /*
	 * only invalidate when changing access to or from 'frob' access.
         */
        method_cache_invalidate_all();
    }
    cache_dirty_object(object);

    method->m_access = access;
    return access;
}

Method * method_new(void) {
    Method * method = EMALLOC(Method, 1);

    method->m_flags  = MF_NONE;
    method->m_access = MS_PUBLIC;
    method->native   = -1; 

    /* usually everything else is initialized elsewhere */
    return method;
}

/* Destroys a method.  Does not delete references from the method's code. */
void method_free(Method *method)
{
    Int i, j;
    Error_list *elist;

    if (method->name != -1)
        ident_discard(method->name);
    if (method->num_args)
	TFREE(method->argnames, method->num_args);
    if (method->num_vars)
	TFREE(method->varnames, method->num_vars);
    TFREE(method->opcodes, method->num_opcodes);
    if (method->num_error_lists) {
	/* Discard identifiers held in the method's error lists. */
	for (i = 0; i < method->num_error_lists; i++) {
	  elist = &method->error_lists[i];
	  for (j = 0; j < elist->num_errors; j++) {
	    ident_discard(elist->error_ids[j]);
	  }
          TFREE(elist->error_ids, elist->num_errors);
	}
	TFREE(method->error_lists, method->num_error_lists);
    }
    efree(method);
}

/* Delete references to object variables and strings in a method's code. */
void method_delete_code_refs(Method *method)
{
    Int i, j, arg_type, opcode;
    Op_info *info;

    for (i = 0; i < method->num_args; i++)
	object_discard_ident(method->object, method->argnames[i]);
    if (method->rest != -1)
	object_discard_ident(method->object, method->rest);

    for (i = 0; i < method->num_vars; i++)
	object_discard_ident(method->object, method->varnames[i]);

    i = 0;
    while (i < method->num_opcodes) {
	opcode = method->opcodes[i];

	/* Use opcode info table for anything else. */
	info = &op_table[opcode];
	for (j = 0; j < 2; j++) {
	    arg_type = (j == 0) ? info->arg1 : info->arg2;
	    if (arg_type) {
		i++;
		switch (arg_type) {

		  case STRING:
		    object_discard_string(method->object, method->opcodes[i]);
		    break;

		  case IDENT:
		    object_discard_ident(method->object, method->opcodes[i]);
		    break;

		}
	    }
	}
	i++;
    }

}

Method * method_dup(Method * method) {
    method->refs++;
    return method;
}

void method_discard(Method *method) {
    method->refs--;
    if (!method->refs) {
	method_delete_code_refs(method);
	method_free(method);
    }
}

Int object_del_objname(Obj * object) {
    Int result = 0;

    cache_dirty_object(object);

    /* if it has one, remove it */
    if (object->objname != -1) {
        result = lookup_remove_name(object->objname);
        ident_discard(object->objname);
    }

    object->objname = -1;

    return result;
}

Int object_set_objname(Obj * obj, Ident name) {
    Long num;

    /* does it already exist? */
    if (lookup_retrieve_name(name, &num))
        return 0;

    /* lucky for us, this call dirties the object appropriately and is first */
    /* do we have a name? Axe it... */
    object_del_objname(obj);

    /* ok, index the new name */
    lookup_store_name(name, obj->objnum);

    /* and for our own purposes, lets remember it */
    obj->objname = ident_dup(name);

    return 1;
}

#ifdef USE_PARENT_OBJS
void object_load_parent_objs(Obj * obj)
{
    cData *d, cthis;

    obj->parent_objs = list_new(list_length(obj->parents));
    cthis.type = OBJECT;
    for (d=list_first(obj->parents); d; d=list_next(obj->parents, d))
    {
	cthis.u.object = cache_retrieve(d->u.objnum);
        obj->parent_objs = list_add(obj->parent_objs, &cthis);
    }
}
#endif

#undef _object_
