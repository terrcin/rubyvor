#include <ruby.h>
#include <vdefs.h>
#include <ruby_vor_c.h>
#include <stdio.h>
#include <stdlib.h>

static Site * nextone(void);
static int scomp(const void *, const void *);

/*
 * See ruby_vor.c for RDOC
 */


/*
 * Class methods for RubyVor::VDDT::Computation
 */

VALUE
RubyVor_from_points(VALUE self, VALUE pointsArray)
{
    VALUE * inPtr, newComp;
    ID x, y;

    long i, inSize;

    /* Require T_ARRAY */
    Check_Type(pointsArray, T_ARRAY);

    /* Intern our point access methods */
    x = rb_intern("x");
    y = rb_intern("y");


    /* Require nonzero size and x & y methods on each array object */
    if (RARRAY(pointsArray)->len < 1)
        rb_raise(rb_eRuntimeError, "points array have a nonzero length");
    for (i = 0; i < RARRAY(pointsArray)->len; i++) {
        if(!rb_respond_to(RARRAY(pointsArray)->ptr[i], x) || !rb_respond_to(RARRAY(pointsArray)->ptr[i], y))
            rb_raise(rb_eRuntimeError, "members of points array must respond to 'x' and 'y'");
    }

    /* Load up point count & points pointer. */
    pointsArray = rb_funcall(pointsArray, rb_intern("uniq"), 0);
    inSize = RARRAY(pointsArray)->len;
    inPtr  = RARRAY(pointsArray)->ptr;

    
    /* Initialize rubyvorState */
    initialize_state(0 /* debug? */);
    debug_memory();
    
    /* Create our return object. */
    newComp = rb_funcall(self, rb_intern("new"), 0);
    rb_iv_set(newComp, "@points", pointsArray);
    
    /* Store it in rubyvorState so we can populate its values. */
    rubyvorState.comp = (void *) &newComp;
    
    /*
     * Read in the sites, sort, and compute xmin, xmax, ymin, ymax
     *
     * TODO: refactor this block into a separate method for clarity?
     */
    {
        /* Allocate memory for N sites... */
        rubyvorState.sites = (Site *) myalloc(inSize * sizeof(Site));
    
    
        /* Iterate over the arrays, doubling the incoming values. */
        for (i=0; i<inSize; i++)
        {
            rubyvorState.sites[rubyvorState.nsites].coord.x = NUM2DBL(rb_funcall(inPtr[i], x, 0));
            rubyvorState.sites[rubyvorState.nsites].coord.y = NUM2DBL(rb_funcall(inPtr[i], y, 0));
             
            rubyvorState.sites[rubyvorState.nsites].sitenbr = rubyvorState.nsites;
            rubyvorState.sites[rubyvorState.nsites++].refcnt = 0;
            
            /* Allocate for N more if we just hit a multiple of N... */
            if (rubyvorState.nsites % inSize == 0)
            {
                rubyvorState.sites = (Site *)myrealloc(rubyvorState.sites,(rubyvorState.nsites+inSize)*sizeof(Site),rubyvorState.nsites*sizeof(Site));
            }
        }
        
        /* Sort the Sites */
        qsort((void *)rubyvorState.sites, rubyvorState.nsites, sizeof(Site), scomp) ;
        
        /* Pull the minimum values. */
        rubyvorState.xmin = rubyvorState.sites[0].coord.x;
        rubyvorState.xmax = rubyvorState.sites[0].coord.x;
        for (i=1; i < rubyvorState.nsites; ++i)
        {
            if (rubyvorState.sites[i].coord.x < rubyvorState.xmin)
            {
                rubyvorState.xmin = rubyvorState.sites[i].coord.x;
            }
            if (rubyvorState.sites[i].coord.x > rubyvorState.xmax)
            {
                rubyvorState.xmax = rubyvorState.sites[i].coord.x;
            }
        }
        rubyvorState.ymin = rubyvorState.sites[0].coord.y;
        rubyvorState.ymax = rubyvorState.sites[rubyvorState.nsites-1].coord.y;
        
    }

    
    /* Perform the computation */
    voronoi(nextone);
    debug_memory();
    
    /* Get rid of our comp reference */
    rubyvorState.comp = (void *)NULL;
    
    /* Free our allocated objects */
    free_all();    
    debug_memory();

    return newComp;
}


/*
 * Instance methods
 */

VALUE
RubyVor_minimum_spanning_tree(int argc, VALUE *argv, VALUE self)
{
    VALUE mst, dist_proc, nodes, nnGraph, points, queue, tmp, adjacent, adjacentData, adjacentDistance, current, currentData, floatMax;
    ID i_call, i_push, i_pop, i_data, i_priority, i_priority_eq, i_has_key, i_index;
    long i;

    /* 0 mandatory, 1 optional */
    rb_scan_args(argc, argv, "01", &dist_proc);

    mst = rb_iv_get(self, "@mst");
    
    if (RTEST(mst))
        return mst;


    if (NIL_P(dist_proc)) {
        /* Use our default Proc */
        dist_proc = rb_eval_string("lambda{|a,b| a.distance_from(b)}");
    } else if (rb_class_of(dist_proc) != rb_cProc) {
        /* Blow up if we have a non-nil, non-Proc */
        rb_raise(rb_eTypeError, "wrong argument type %s (expected %s)", rb_obj_classname(dist_proc), rb_class2name(rb_cProc));
    }

    // Set up interned values
    i_call     = rb_intern("call");
    i_push     = rb_intern("push");
    i_pop      = rb_intern("pop");
    i_data     = rb_intern("data");
    i_priority = rb_intern("priority");
    i_priority_eq = rb_intern("priority=");
    i_has_key  = rb_intern("has_key?");
    i_index    = rb_intern("index");
    
    points  = rb_iv_get(self, "@points");
    queue   = rb_eval_string("RubyVor::PriorityQueue.new");
    nnGraph = RubyVor_nn_graph(self);
    floatMax= rb_iv_get(rb_cFloat, "MAX");

    for (i = 0; i < RARRAY(points)->len; i++) {
        tmp = rb_ary_new2(5);
        /* 0: node index */
        rb_ary_push(tmp, LONG2FIX(i));
        /* 1: parent */
        rb_ary_push(tmp, Qnil);
        /* 2: adjacency_list */
        rb_ary_push(tmp, rb_obj_clone(RARRAY(nnGraph)->ptr[i]));
        /* 3: min_adjacency_list */
        rb_ary_push(tmp, rb_ary_new());
        /* 4: in_q */
        rb_ary_push(tmp, Qtrue);

        rb_funcall(queue, i_push, 2, tmp, (i == 0) ? rb_float_new(0.0) : floatMax);
    }
    nodes = rb_obj_clone(rb_funcall(queue, i_data, 0));
    
    while(RTEST(current = rb_funcall(queue, i_pop, 0))) {
        currentData = rb_funcall(current, i_data, 0);
        
        /* mark in_q */
        rb_ary_store(currentData, 4, Qfalse);

        /* check for presence of parent */
        if (RTEST(RARRAY(currentData)->ptr[1])) {
            /* push this node into adjacency_list of parent */
            rb_ary_push(RARRAY(rb_funcall(RARRAY(currentData)->ptr[1], i_data, 0))->ptr[3], current);
            /* push parent into adjacency_list of this node */
            rb_ary_push(RARRAY(currentData)->ptr[3], RARRAY(currentData)->ptr[1]);
        }

        for (i = 0; i < RARRAY(RARRAY(currentData)->ptr[2])->len; i++) {
            /* grab indexed node */
            adjacent = RARRAY(nodes)->ptr[FIX2LONG(RARRAY(RARRAY(currentData)->ptr[2])->ptr[i])];
            adjacentData = rb_funcall(adjacent, i_data, 0);
        
            /* check in_q -- only look at new nodes */
            if (Qtrue == RARRAY(adjacentData)->ptr[4]) {

                /* compare points by node -- adjacent against current */
                adjacentDistance = rb_funcall(dist_proc,
                                              i_call, 2,
                                              RARRAY(points)->ptr[FIX2LONG(RARRAY(currentData)->ptr[0])],
                                              RARRAY(points)->ptr[FIX2LONG(RARRAY(adjacentData)->ptr[0])]);

                /* If the new distance is better than our current priority, exchange them. */
                if (RFLOAT(adjacentDistance)->value < RFLOAT(rb_funcall(adjacent, i_priority, 0))->value) {
                    /* set new :parent */
                    rb_ary_store(adjacentData, 1, current);
                    /* update priority */
                    rb_funcall(adjacent, i_priority_eq, 1, adjacentDistance);
                    /* percolate up into correctn position */
                    RubyVor_percolate_up(queue, rb_funcall(adjacent, i_index, 0));
                }
            }
        }
    }

    mst = rb_hash_new();
    for (i = 0; i < RARRAY(nodes)->len; i++) {
        current = RARRAY(nodes)->ptr[i];
        currentData = rb_funcall(current, i_data, 0);
        if (!NIL_P(RARRAY(currentData)->ptr[1])) {
            adjacentData = rb_funcall(RARRAY(currentData)->ptr[1], i_data, 0);
            tmp = rb_ary_new2(2);
            if (FIX2LONG(RARRAY(currentData)->ptr[0]) < FIX2LONG(RARRAY(adjacentData)->ptr[0])) {
                rb_ary_push(tmp, RARRAY(currentData)->ptr[0]);
                rb_ary_push(tmp, RARRAY(adjacentData)->ptr[0]);
            } else {
                rb_ary_push(tmp, RARRAY(adjacentData)->ptr[0]);                
                rb_ary_push(tmp, RARRAY(currentData)->ptr[0]);
            }
            if (!RTEST(rb_funcall(mst, i_has_key, 1, tmp))) {
                rb_hash_aset(mst, tmp, rb_funcall(current, i_priority, 0));

            }
        }
    }

    rb_iv_set(self, "@mst", mst);
    
    return mst;
}


VALUE
RubyVor_nn_graph(VALUE self)
{
    long i;
    VALUE dtRaw, graph, points, * dtPtr, * tripletPtr, * graphPtr;

    graph = rb_iv_get(self, "@nn_graph");
    
    if (RTEST(graph))
        return graph;

    /* Create an array of same size as points for the graph */
    points = rb_iv_get(self, "@points");
    
    graph = rb_ary_new2(RARRAY(points)->len);
    for (i = 0; i < RARRAY(points)->len; i++)
        rb_ary_push(graph, rb_ary_new());
    
    /* Get our pointer into this array. */
    graphPtr = RARRAY(graph)->ptr;

    /* Iterate over the triangulation */
    dtRaw = rb_iv_get(self, "@delaunay_triangulation_raw");
    dtPtr = RARRAY(dtRaw)->ptr;
    for (i = 0; i < RARRAY(dtRaw)->len; i++) {
        tripletPtr = RARRAY(dtPtr[i])->ptr;

        rb_ary_push(graphPtr[FIX2INT(tripletPtr[0])], tripletPtr[1]);
        rb_ary_push(graphPtr[FIX2INT(tripletPtr[1])], tripletPtr[0]);

        rb_ary_push(graphPtr[FIX2INT(tripletPtr[0])], tripletPtr[2]);
        rb_ary_push(graphPtr[FIX2INT(tripletPtr[2])], tripletPtr[0]);

        rb_ary_push(graphPtr[FIX2INT(tripletPtr[1])], tripletPtr[2]);
        rb_ary_push(graphPtr[FIX2INT(tripletPtr[2])], tripletPtr[1]);
        
    }
    for (i = 0; i < RARRAY(graph)->len; i++) {
        if (RARRAY(graphPtr[i])->len < 1) {
            rb_raise(rb_eIndexError, "index of 0 (no neighbors) at %i", i);
            /* No valid triangles touched this node -- include *all* possible neighbors
            for(j = 0; j < RARRAY(points)->len; j++) {
                if (j != i) {
                    rb_ary_push(graphPtr[i], INT2FIX(j));
                    if (RARRAY(graphPtr[j])->len > 0 && !rb_ary_includes(graphPtr[j], INT2FIX(i)))
                        rb_ary_push(graphPtr[j], INT2FIX(i));
                }
            }
            */
        } else {
            rb_funcall(graphPtr[i], rb_intern("uniq!"), 0);
        }
    }

    rb_iv_set(self, "@nn_graph", graph);
    
    return graph;
}



/*
 * Static C helper methods
 */


/*** sort sites on y, then x, coord ***/
static int
scomp(const void * vs1, const void * vs2)
{
    Point * s1 = (Point *)vs1 ;
    Point * s2 = (Point *)vs2 ;

    if (s1->y < s2->y)
    {
        return (-1) ;
    }
    if (s1->y > s2->y)
    {
        return (1) ;
    }
    if (s1->x < s2->x)
    {
        return (-1) ;
    }
    if (s1->x > s2->x)
    {
        return (1) ;
    }
    return (0) ;
}

/*** return a single in-storage site ***/
static Site *
nextone(void)
{
    Site * s ;

    if (rubyvorState.siteidx < rubyvorState.nsites)
    {
        s = &rubyvorState.sites[rubyvorState.siteidx++];
        return (s) ;
    }
    else
    {
        return ((Site *)NULL) ;
    }
}
