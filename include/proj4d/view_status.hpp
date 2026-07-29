#pragma once

#include <string>

#include "proj4d/camera.hpp"

namespace proj4d {

[[nodiscard]] std::string formatViewStatus(const Camera4D &camera);

} // namespace proj4d
