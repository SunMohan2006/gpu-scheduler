#include "models.h"
#include <algorithm>
#include <climits>
using namespace std;

void Server::init(int g, int vg, int c, int r) {
    G = g; VG = vg; C = c; R = r;
    gpu_ev.clear(); cpu_ev.clear(); ram_ev.clear();
}

void Server::add_delta(vector<pair<int, ll>>& ev, int t, ll d) {
    int lo = 0, hi = (int)ev.size();
    while (lo < hi) { int m = (lo + hi) / 2; ev[m].first < t ? lo = m + 1 : hi = m; }
    if (lo < (int)ev.size() && ev[lo].first == t) ev[lo].second += d;
    else ev.insert(ev.begin() + lo, {t, d});
}

bool Server::resource_ok(const vector<pair<int, ll>>& ev,
                         int t, int dur, int need, int cap) const {
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

bool Server::can_start(int t, int p, int u, int c, int m) const {
    return resource_ok(gpu_ev, t, p, u, G) &&
           resource_ok(cpu_ev, t, p, c, C) &&
           resource_ok(ram_ev, t, p, m, R);
}

int Server::earliest_start(int r, int p, int u, int c_req, int m_req, int max_iter) const {
    int t = r;
    for (int it = 0; it < max_iter; it++) {
        if (resource_ok(gpu_ev, t, p, u, G) &&
            resource_ok(cpu_ev, t, p, c_req, C) &&
            resource_ok(ram_ev, t, p, m_req, R))
            return t;

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

void Server::schedule(int t, int p, int u, int c, int m) {
    add_delta(gpu_ev, t, u);     add_delta(gpu_ev, t + p, -u);
    add_delta(cpu_ev, t, c);     add_delta(cpu_ev, t + p, -c);
    add_delta(ram_ev, t, m);     add_delta(ram_ev, t + p, -m);
}

void Server::unschedule(int t, int p, int u, int c, int m) {
    add_delta(gpu_ev, t, -u);    add_delta(gpu_ev, t + p, u);
    add_delta(cpu_ev, t, -c);    add_delta(cpu_ev, t + p, c);
    add_delta(ram_ev, t, -m);    add_delta(ram_ev, t + p, m);
}
