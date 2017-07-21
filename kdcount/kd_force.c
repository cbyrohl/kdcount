#include "kdtree.h"

typedef struct TraverseData {
    KDAttr * xmass;
    KDAttr * mass;

    double * pos;
    double r_cut2;
    double eta2;
    int node_ndims;
    double * force;
    kd_force_func func;
    void * userdata;
    int64_t node_probed;
    int64_t node_computed;
    int64_t pair_computed;
} TraverseData;

/* compute x - y*/
static double
distance(KDTree * tree, double * x, double * y, double * dx)
{
    double r2 = 0;
    int Nd = tree->input.dims[1];
    int d;
    double half;
    for(d = 0; d < Nd; d++) {
        dx[d] = y[d] - x[d];
        if (tree->boxsize) {
            half = 0.5 * tree->boxsize[d];
            if (dx[d] > half) dx[d] = dx[d] - tree->boxsize[d];
            else if (dx[d] < -half) dx[d] = dx[d] + tree->boxsize[d];
        }
        r2 += dx[d] * dx[d];
    }
    return r2;
}

static void
kd_force_check(TraverseData * trav, KDNode * node)
{
    int d;
    int node_ndims = trav->node_ndims;
    double f[node_ndims];
    double dx[node_ndims];

    if(node->dim >= 0 && node->size > 1) {
        trav->node_probed ++;
        double *min = kd_node_min(node);
        double *max = kd_node_max(node);

        double r2min=0, r2max=0;

        for(d = 0; d < node_ndims; d++) {
            double realmin;
            double realmax;
            realmin = trav->pos[d] - max[d];
            realmax = trav->pos[d] - min[d];
            kd_realminmax(node->tree, realmin, realmax, &realmin, &realmax, d);
            r2min += realmin * realmin;
            r2max += realmax * realmax;

            if (r2min > trav->r_cut2) return;
        }

        double l = 0;

        for(d = 0; d < node_ndims; d++) {
            l += max[d] - min[d];
        }

        // printf("ll %g r2min %g r2max %g\n", l * l, r2min, r2max);
        /* fully inside, check opening angle too */
        if (r2max <= trav->r_cut2 && l * l < trav->eta2 * r2min) {
            trav->node_computed ++;
            double cm[node_ndims];

            double *mx = kd_attr_get_node(trav->xmass, node);
            double * m = kd_attr_get_node(trav->mass, node);

            for(d = 0; d < node_ndims; d ++) {
                cm[d] = mx[d] / m[0];
            }

            double r2 = distance(node->tree, trav->pos, cm, dx);

            double r = sqrt(r2);

            /* direct add the force no need to open */
            trav->func(r, dx, f, node_ndims, trav->userdata);
            for(d = 0; d < node_ndims; d ++) {
                trav->force[d] += m[0] * f[d];
            }
            return;
        }

        /* open the node */
        kd_force_check(trav, node->link[0]);
        kd_force_check(trav, node->link[1]);
        return;
    }

    /* leaf node */
    trav->pair_computed +=  node->size;
    double * pbase = alloca(node->size * sizeof(double) * trav->node_ndims);
    double * mbase = alloca(node->size * sizeof(double));

    ptrdiff_t i;

    double * p, * m;

    kd_collect(node, &node->tree->input, pbase);
    kd_collect(node, &trav->mass->input, mbase);

    for(p = pbase, m = mbase, i = 0; i < node->size; i++) {
        double r2 = distance(node->tree, trav->pos, p, dx);
        if (r2 <= trav->r_cut2) {
            double r = sqrt(r2);
            trav->func(r, dx, f, node_ndims,  trav->userdata);
            for(d = 0; d < node_ndims; d ++) {
                trav->force[d] += m[0] * f[d];
            }
        }
        p += trav->node_ndims;
        m += 1;
    }
}

void
kd_force(double * pos, KDNode * node, KDAttr * mass, KDAttr * xmass,
        double r_cut, double eta, double * force,
        kd_force_func func, void * userdata)
{
    int node_ndims = node->tree->input.dims[1];

    TraverseData trav = {
        .pos = pos,
        .mass = mass,
        .xmass = xmass,
        .r_cut2 = r_cut * r_cut,
        .node_ndims = node_ndims,
        .eta2 = eta * eta,
        .force = force,
        .func = func,
        .userdata = userdata,
        .node_probed = 0,
        .node_computed = 0,
        .pair_computed = 0,
    };

    int d;
    for(d = 0; d < node_ndims; d ++) {
        force[d] = 0;
    }

    kd_force_check(&trav, node);
    printf("Node probed = %ld, Node computed = %ld pair computed = %ld\n", trav.node_probed, trav.node_computed, trav.pair_computed);
}
