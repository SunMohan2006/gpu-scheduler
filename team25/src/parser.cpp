#include "parser.h"
using namespace std;

pair<vector<Server>, vector<Job>> read_instance(istream& in) {
    int M, N;
    in >> M >> N;

    vector<Server> servers(M + 1);
    for (int i = 1; i <= M; i++) {
        int g, vg, c, r;
        in >> g >> vg >> c >> r;
        servers[i].id = i;
        servers[i].init(g, vg, c, r);
    }

    vector<Job> jobs(N);
    for (int i = 0; i < N; i++) {
        Job& j = jobs[i];
        j.id = i + 1;
        in >> j.r >> j.p >> j.g >> j.v >> j.c >> j.m >> j.w;
    }

    return {servers, jobs};
}
