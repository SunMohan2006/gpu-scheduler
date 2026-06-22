#ifndef GPU_SCHEDULING_PARSER_H
#define GPU_SCHEDULING_PARSER_H

#include <istream>
#include <vector>
#include "models.h"

std::pair<std::vector<Server>, std::vector<Job>> read_instance(std::istream& in);

#endif
