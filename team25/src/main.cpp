#include <iostream>
#include "models.h"
#include "parser.h"
#include "scheduler.h"
#include "output.h"
using namespace std;

int main() {
    ios::sync_with_stdio(false); cin.tie(nullptr);
    auto [servers, jobs] = read_instance(cin);
    if (jobs.empty()) return 0;

    Scheduler sch(move(servers), move(jobs));
    sch.solve();
    write_schedule(sch.result());
    return 0;
}
