#include "scheduler.h"
#include <algorithm>
#include <cmath>
using namespace std;

Scheduler::Scheduler(vector<Server> s, vector<Job> j)
    : servers(move(s)), jobs(move(j))
{
    M = (int)servers.size() - 1;  // servers is 1-indexed
    N = (int)jobs.size();
    asgn.resize(N + 1);
}

// ---- Orchestration ----

void Scheduler::solve() {
    build_feasible();
    phase1_greedy();
    phase2_backfill();
    phase3_compact();
}

// ---- Pre-computation: feasible servers per job ----

void Scheduler::build_feasible() {
    feasible.resize(N);
    for (int i = 0; i < N; i++) {
        Job& j = jobs[i];
        for (int s = 1; s <= M; s++) {
            Server& sv = servers[s];
            if (j.c > sv.C || j.m > sv.R) continue;
            int u_mem = (j.v + sv.VG - 1) / sv.VG;
            int u = (j.g > u_mem) ? j.g : u_mem;
            if (u <= sv.G)
                feasible[i].push_back({s, u});
        }
    }
}

// ---- Phase 1: Priority-greedy list scheduling ----

void Scheduler::phase1_greedy() {
    for (int s = 1; s <= M; s++)
        servers[s].init(servers[s].G, servers[s].VG, servers[s].C, servers[s].R);

    vector<int> order(N);
    for (int i = 0; i < N; i++) order[i] = i;
    sort(order.begin(), order.end(), [this](int a, int b) {
        auto& ja = jobs[a], & jb = jobs[b];
        double sa = (double)ja.w / (ja.r + ja.p + 1);
        double sb = (double)jb.w / (jb.r + jb.p + 1);
        if (abs(sa - sb) > 1e-12) return sa > sb;
        if (ja.r != jb.r) return ja.r < jb.r;
        return ja.p < jb.p;
    });

    for (int idx : order) {
        Job& j = jobs[idx];
        int iters = (N <= 1000) ? 500 : 200;
        if (!try_schedule(j, iters))
            if (!try_schedule(j, iters * 10))
                fallback_schedule(j);
    }
}

bool Scheduler::try_schedule(Job& j, int max_iter) {
    double best_cost = 1e30;
    int best_s = -1, best_t = 0, best_u = 0;

    for (auto& [s, u_min] : feasible[j.id - 1]) {
        Server& sv = servers[s];
        for (int u = u_min; u <= min(u_min + 1, sv.G); u++) {
            if ((ll)u * sv.VG < j.v) continue;
            int t = sv.earliest_start(j.r, j.p, u, j.c, j.m, max_iter);
            if (t < 0) continue;
            double cost = (double)j.w * (t - j.r)
                        + (double)((ll)u * sv.VG - j.v) * 5.0
                        + (double)(t + j.p) * 2.0;
            if (cost < best_cost - 1e-12)
                best_cost = cost, best_s = s, best_t = t, best_u = u;
        }
    }
    if (best_s < 0) return false;
    servers[best_s].schedule(best_t, j.p, best_u, j.c, j.m);
    asgn[j.id] = {best_s, best_t, best_u, best_t + j.p};
    return true;
}

void Scheduler::fallback_schedule(Job& j) {
    int limit = (N <= 1000) ? 500000 : 200000;
    for (auto& [s, u] : feasible[j.id - 1]) {
        for (int t = j.r; t <= j.r + limit; t++) {
            if (servers[s].can_start(t, j.p, u, j.c, j.m)) {
                servers[s].schedule(t, j.p, u, j.c, j.m);
                asgn[j.id] = {s, t, u, t + j.p};
                return;
            }
        }
    }
    auto& [s0, u0] = feasible[j.id - 1][0];
    servers[s0].schedule(j.r, j.p, u0, j.c, j.m);
    asgn[j.id] = {s0, j.r, u0, j.r + j.p};
}

// ---- Phase 2: Cross-server backfill ----

void Scheduler::phase2_backfill() {
    int rounds = (N <= 1000) ? 2 : 1;
    for (int rnd = 0; rnd < rounds; rnd++) {
        vector<int> order(N);
        for (int i = 0; i < N; i++) order[i] = i;
        sort(order.begin(), order.end(), [this](int a, int b) {
            return asgn[jobs[a].id].start_time > asgn[jobs[b].id].start_time;
        });

        for (int idx : order) {
            Job& j = jobs[idx];
            Assignment& cur = asgn[j.id];
            servers[cur.server_id].unschedule(cur.start_time, j.p, cur.gpu_count, j.c, j.m);

            double best_cost = (double)j.w * (cur.start_time - j.r)
                + (double)((ll)cur.gpu_count * servers[cur.server_id].VG - j.v) * 5.0
                + (double)cur.finish_time * 2.0;
            int best_s = cur.server_id, best_t = cur.start_time, best_u = cur.gpu_count;

            for (auto& [s, u_min] : feasible[j.id - 1]) {
                Server& sv = servers[s];
                for (int u = u_min; u <= min(u_min + 1, sv.G); u++) {
                    if ((ll)u * sv.VG < j.v) continue;
                    int t = sv.earliest_start(j.r, j.p, u, j.c, j.m, 200);
                    if (t < 0) continue;
                    double c = (double)j.w * (t - j.r)
                             + (double)((ll)u * sv.VG - j.v) * 5.0
                             + (double)(t + j.p) * 2.0;
                    if (c < best_cost - 1e-12)
                        best_cost = c, best_s = s, best_t = t, best_u = u;
                }
            }

            servers[best_s].schedule(best_t, j.p, best_u, j.c, j.m);
            cur = {best_s, best_t, best_u, best_t + j.p};
        }
    }
}

// ---- Phase 3: Per-server compaction ----

void Scheduler::phase3_compact() {
    for (int s = 1; s <= M; s++) {
        vector<int> sj;
        for (int i = 0; i < N; i++)
            if (asgn[jobs[i].id].server_id == s) sj.push_back(i);
        if (sj.empty()) continue;

        sort(sj.begin(), sj.end(), [this](int a, int b) {
            return asgn[jobs[a].id].start_time < asgn[jobs[b].id].start_time;
        });

        servers[s].init(servers[s].G, servers[s].VG, servers[s].C, servers[s].R);
        for (int idx : sj) {
            Job& j = jobs[idx];
            int u = asgn[j.id].gpu_count;
            if (u < 1) {
                int u_mem = (j.v + servers[s].VG - 1) / servers[s].VG;
                u = (j.g > u_mem) ? j.g : u_mem;
                if (u < 1) u = 1;
            }
            int t = servers[s].earliest_start(j.r, j.p, u, j.c, j.m, 200);
            if (t < 0)
                for (t = j.r; t <= j.r + 200000; t++)
                    if (servers[s].can_start(t, j.p, u, j.c, j.m)) break;
            servers[s].schedule(t, j.p, u, j.c, j.m);
            asgn[j.id] = {s, t, u, t + j.p};
        }
    }
}
