#ifndef GPU_SCHEDULING_MODELS_H
#define GPU_SCHEDULING_MODELS_H

#include <vector>
#include <utility>

typedef long long ll;

struct Job {
    int id, r, p, g, v, c, m, w;
};

struct Assignment {
    int server_id, start_time, gpu_count, finish_time;
};

struct Server {
    int id, G, VG, C, R;
    std::vector<std::pair<int, ll>> gpu_ev, cpu_ev, ram_ev;

    void init(int g, int vg, int c, int r);

    static void add_delta(std::vector<std::pair<int, ll>>& ev, int t, ll d);

    bool resource_ok(const std::vector<std::pair<int, ll>>& ev,
                     int t, int dur, int need, int cap) const;

    bool can_start(int t, int p, int u, int c, int m) const;
    int  earliest_start(int r, int p, int u, int c_req, int m_req, int max_iter) const;
    void schedule(int t, int p, int u, int c, int m);
    void unschedule(int t, int p, int u, int c, int m);
};

#endif
