#pragma once
#include <string>
#include <vector>
// Called by the capture worker before SDK init and after all SDK threads stop.
void deepmirrorSetUvcPaths(const std::string &rgb,const std::string &depth,const std::vector<std::string> &controls);
