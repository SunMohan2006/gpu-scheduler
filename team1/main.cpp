#include <iostream>
#include <vector>
#include <algorithm>
#include <climits>
using namespace std;

typedef long long ll;

// ============================================================================
// GPU Server Scheduler — C++17
// Core: Priority-greedy list scheduling + event-based resource tracking
// Refinement: Cross-server backfill + per-server compaction
//
// Key design (learned from reference code):
//   - GPU tracking is AGGREGATE (sum u_i ≤ G_s), not per-card intervals.
//     Because GPUs within a server are homogeneous, aggregate check is
//     both necessary and sufficient — no need to track individual cards.
//   - Feasible machines are pre-computed per job (O(N·M) once).
//   - GPU count uses ceiling division: u = max(g, ⌈v / VG⌉).
// ============================================================================

// ---- Data types ----------------------------------------------------------

struct Job {
    int id, r, p, g, v, c, m, w;
};

struct Assignment {
    int server_id, start_time, gpu_count, finish_time;
};

// ---- Server state: three event-based resource trackers --------------------
// All three resources (GPU count, CPU, RAM) use the same delta-event pattern:
//   event = (time, ±amount), cumulative sum = current usage at that time.

struct Server {
    int id, G, VG, C, R;
    vector<pair<int, ll>> gpu_ev, cpu_ev, ram_ev;

    void init(int g, int vg, int c, int r) {
        G = g; VG = vg; C = c; R = r;
        gpu_ev.clear(); cpu_ev.clear(); ram_ev.clear();
    }

    // Insert a delta event, merging if same-time event exists.  O(log E).
    static void add_delta(vector<pair<int, ll>>& ev, int t, ll d) {
        int lo = 0, hi = (int)ev.size();
        while (lo < hi) { int m = (lo + hi) / 2; ev[m].first < t ? lo = m + 1 : hi = m; }
        if (lo < (int)ev.size() && ev[lo].first == t) ev[lo].second += d;
        else ev.insert(ev.begin() + lo, {t, d});
    }

    // Check: can we add `need` units throughout [t, t+dur) without exceeding cap?
    // Sweeps over the event list, tracking cumulative usage.  O(E_in_range).
    bool resource_ok(const vector<pair<int, ll>>& ev, int t, int dur, int need, int cap) const {
        int t_end = t + dur;
        ll cur = 0;
        size_t i = 0;
        while (i < ev.size() && ev[i].first <= t) { cur += ev[i].second; i++; }
        if (cur + need > cap) return false;
        int prev = t;
        while (i < ev.size() && ev[i].first < t_end) {
            if (ev[i].first > prev && cur + need > cap) return false;
            cur += ev[i].second; prev = ev[i].first; i++;
        }
        if (t_end > prev && cur + need > cap) return false;
        return true;
    }

    // ---- Public API ----

    // Can the job start at time t?
    bool can_start(int t, int p, int u, int c, int m) const {
        return resource_ok(gpu_ev, t, p, u, G) &&
               resource_ok(cpu_ev, t, p, c, C) &&
               resource_ok(ram_ev, t, p, m, R);
    }

    // Find earliest feasible start time ≥ r.  Returns -1 if not found within max_iter.
    int earliest_start(int r, int p, int u, int c_req, int m_req, int max_iter) const {
        int t = r;
        for (int it = 0; it < max_iter; it++) {
            if (resource_ok(gpu_ev, t, p, u, G) &&
                resource_ok(cpu_ev, t, p, c_req, C) &&
                resource_ok(ram_ev, t, p, m_req, R))
                return t;

            // Jump to next interesting time: binary-search for first event after t
            int nx = INT_MAX;
            auto next_time = [&](const vector<pair<int, ll>>& ev) {
                int lo = 0, hi = (int)ev.size();
                while (lo < hi) { int m = (lo + hi) / 2; ev[m].first <= t ? lo = m + 1 : hi = m; }
                if (lo < (int)ev.size() && ev[lo].first < nx) nx = ev[lo].first;
            };
            next_time(gpu_ev); next_time(cpu_ev); next_time(ram_ev);
            t = (nx == INT_MAX) ? t + 1 : max(t + 1, nx);
        }
        return -1;
    }

    // Commit a job's resources
    void schedule(int t, int p, int u, int c, int m) {
        add_delta(gpu_ev, t, u);     add_delta(gpu_ev, t + p, -u);
        add_delta(cpu_ev, t, c);     add_delta(cpu_ev, t + p, -c);
        add_delta(ram_ev, t, m);     add_delta(ram_ev, t + p, -m);
    }

    // Revert a job's resources
    void unschedule(int t, int p, int u, int c, int m) {
        add_delta(gpu_ev, t, -u);    add_delta(gpu_ev, t + p, u);
        add_delta(cpu_ev, t, -c);    add_delta(cpu_ev, t + p, c);
        add_delta(ram_ev, t, -m);    add_delta(ram_ev, t + p, m);
    }
};

// ---- Scheduler -----------------------------------------------------------

class Scheduler {
    int M = 0, N = 0;
    vector<Server> servers;
    vector<Job> jobs;
    vector<Assignment> asgn;
    // Pre-computed: for each job, list of (server_index, min_gpu_count) pairs
    vector<vector<pair<int,int>>> feasible;

public:
    // ---- I/O ----

    void read_input() {
        ios::sync_with_stdio(false); cin.tie(nullptr);
        cin >> M >> N;
        servers.resize(M + 1);
        for (int i = 1; i <= M; i++) {
            int g, vg, c, r; cin >> g >> vg >> c >> r;
            servers[i].id = i; servers[i].init(g, vg, c, r);
        }
        jobs.resize(N);
        for (int i = 0; i < N; i++) {
            Job& j = jobs[i]; j.id = i + 1;
            cin >> j.r >> j.p >> j.g >> j.v >> j.c >> j.m >> j.w;
        }
        asgn.resize(N + 1);
    }

    void print_output() const {
        for (int i = 1; i <= N; i++)
            cout << i << ' ' << asgn[i].server_id << ' ' << asgn[i].start_time
                 << ' ' << asgn[i].gpu_count << ' ' << asgn[i].finish_time << '\n';
    }

    // ---- Orchestration ----

    void solve() {
        build_feasible();
        phase1_greedy();
        phase2_backfill();
        phase3_compact();
    }

    // Pre-compute which servers can run each job (and min GPU count).
    // Uses O(1) ceiling division: u = max(g, ⌈v / VG⌉).
    void build_feasible() {
        feasible.resize(N);
        for (int i = 0; i < N; i++) {
            Job& j = jobs[i];
            for (int s = 1; s <= M; s++) {
                Server& sv = servers[s];
                if (j.c > sv.C || j.m > sv.R) continue;
                // Ceiling division: (v + VG - 1) / VG
                int u_mem = (j.v + sv.VG - 1) / sv.VG;
                int u = (j.g > u_mem) ? j.g : u_mem;
                if (u <= sv.G)
                    feasible[i].push_back({s, u});
            }
        }
    }

    // ---- Phase 1: Priority-greedy list scheduling ----

    void phase1_greedy() {
        for (int s = 1; s <= M; s++)
            servers[s].init(servers[s].G, servers[s].VG, servers[s].C, servers[s].R);

        // Sort jobs: w / (r + p + 1) — higher priority, earlier submit, shorter first
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

    // Try to schedule one job using Best-Fit across feasible servers.
    // Tries both u = min_gpus and u = min_gpus + 1 (when beneficial).
    bool try_schedule(Job& j, int max_iter) {
        double best_cost = 1e30;
        int best_s = -1, best_t = 0, best_u = 0;

        for (auto& [s, u_min] : feasible[j.id - 1]) {
            Server& sv = servers[s];
            for (int u = u_min; u <= min(u_min + 1, sv.G); u++) {
                if ((ll)u * sv.VG < j.v) continue;
                int t = sv.earliest_start(j.r, j.p, u, j.c, j.m, max_iter);
                if (t < 0) continue;
                double cost = (double)j.w * (t - j.r)           // wait
                            + (double)((ll)u * sv.VG - j.v) * 5.0  // GPU waste
                            + (double)(t + j.p) * 2.0;             // finish
                if (cost < best_cost - 1e-12)
                    best_cost = cost, best_s = s, best_t = t, best_u = u;
            }
        }
        if (best_s < 0) return false;
        servers[best_s].schedule(best_t, j.p, best_u, j.c, j.m);
        asgn[j.id] = {best_s, best_t, best_u, best_t + j.p};
        return true;
    }

    // Fallback: linear scan on compatible servers
    void fallback_schedule(Job& j) {
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
        // Absolute last resort
        auto& [s0, u0] = feasible[j.id - 1][0];
        servers[s0].schedule(j.r, j.p, u0, j.c, j.m);
        asgn[j.id] = {s0, j.r, u0, j.r + j.p};
    }

    // ---- Phase 2: Cross-server backfill ----

    void phase2_backfill() {
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

    void phase3_compact() {
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
};

// ---- Entry point ---------------------------------------------------------

int main() {
    Scheduler sch;
    sch.read_input();
    sch.solve();
    sch.print_output();
    return 0;
}
