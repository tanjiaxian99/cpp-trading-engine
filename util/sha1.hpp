#pragma once

#include <string_view>
#include <vector>

std::vector<unsigned char> Sha1(std::string_view message);
