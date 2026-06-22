#include "output.h"
#include <iostream>
using namespace std;

void write_schedule(const vector<Assignment>& asgn) {
    for (size_t i = 1; i < asgn.size(); i++)
        cout << i << ' ' << asgn[i].server_id << ' ' << asgn[i].start_time
             << ' ' << asgn[i].gpu_count << ' ' << asgn[i].finish_time << '\n';
}
