#include "proj4d/view_status.hpp"

#include <iomanip>
#include <sstream>

namespace proj4d {

namespace {

double degrees(double radians) {
  return radians * 90.0 / straightVerticalPitch;
}

} // namespace

std::string formatViewStatus(const Camera4D &camera) {
  std::ostringstream status;
  status << std::fixed << std::setprecision(2) << "X:" << camera.position.x
         << " Y:" << camera.position.y << " Z:" << camera.position.z
         << " W:" << camera.position.w << std::setprecision(1)
         << " | H:" << degrees(camera.horizontalAngle())
         << " V:" << degrees(camera.verticalPitch())
         << " 4D:" << degrees(camera.fourthAngle());
  return status.str();
}

} // namespace proj4d
