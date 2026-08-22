#include "okx/common/response_utils.hpp"

#include "okx/common/okx_constants.hpp"
#include "util/json.hpp"

std::optional<std::string_view> FindData(std::string_view json) {
    return json::FindArrayElement(json, kData, 0);
}
