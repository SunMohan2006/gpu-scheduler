#ifndef GPU_SCHEDULING_SCHEDULER_H
#define GPU_SCHEDULING_SCHEDULER_H

#include <vector>
#include "models.h"

class Scheduler {
    int M = 0, N = 0;
    std::vector<Server>     servers;
    std::vector<Job>        jobs;
    std::vector<Assignment> asgn;
    std::vector<std::vector<std::pair<int,int>>> feasible;

public:
    Scheduler(std::vector<Server> s, std::vector<Job> j);

    void solve();
    std::vector<Assignment> result() const { return asgn; }

private:
    void build_feasible();
    void phase1_greedy();
    void phase2_backfill();
    void phase3_compact();

    bool try_schedule(Job& j, int max_iter);
    void fallback_schedule(Job& j);
};

#endif
