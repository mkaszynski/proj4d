#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace proj4d {

struct Vec2 {
  double x{};
  double y{};

  [[nodiscard]] double &operator[](std::size_t index) {
    return *std::array<double *, 2>{&x, &y}.at(index);
  }
  [[nodiscard]] double operator[](std::size_t index) const {
    return std::array<double, 2>{x, y}.at(index);
  }
};

struct Vec3 {
  double x{};
  double y{};
  double z{};

  [[nodiscard]] double &operator[](std::size_t index) {
    return *std::array<double *, 3>{&x, &y, &z}.at(index);
  }
  [[nodiscard]] double operator[](std::size_t index) const {
    return std::array<double, 3>{x, y, z}.at(index);
  }
};

struct Vec4 {
  double x{};
  double y{};
  double z{};
  double w{};

  [[nodiscard]] double &operator[](std::size_t index) {
    return *std::array<double *, 4>{&x, &y, &z, &w}.at(index);
  }
  [[nodiscard]] double operator[](std::size_t index) const {
    return std::array<double, 4>{x, y, z, w}.at(index);
  }
};

[[nodiscard]] inline Vec2 operator+(const Vec2 &left, const Vec2 &right) {
  return {left.x + right.x, left.y + right.y};
}
[[nodiscard]] inline Vec2 operator-(const Vec2 &left, const Vec2 &right) {
  return {left.x - right.x, left.y - right.y};
}
[[nodiscard]] inline Vec2 operator*(const Vec2 &value, double scalar) {
  return {value.x * scalar, value.y * scalar};
}
inline Vec2 &operator+=(Vec2 &left, const Vec2 &right) {
  left = left + right;
  return left;
}
[[nodiscard]] inline Vec3 operator+(const Vec3 &left, const Vec3 &right) {
  return {left.x + right.x, left.y + right.y, left.z + right.z};
}
[[nodiscard]] inline Vec3 operator-(const Vec3 &left, const Vec3 &right) {
  return {left.x - right.x, left.y - right.y, left.z - right.z};
}
[[nodiscard]] inline Vec3 operator*(const Vec3 &value, double scalar) {
  return {value.x * scalar, value.y * scalar, value.z * scalar};
}
[[nodiscard]] inline Vec4 operator+(const Vec4 &left, const Vec4 &right) {
  return {left.x + right.x, left.y + right.y, left.z + right.z,
          left.w + right.w};
}
[[nodiscard]] inline Vec4 operator-(const Vec4 &left, const Vec4 &right) {
  return {left.x - right.x, left.y - right.y, left.z - right.z,
          left.w - right.w};
}
[[nodiscard]] inline Vec4 operator*(const Vec4 &value, double scalar) {
  return {value.x * scalar, value.y * scalar, value.z * scalar,
          value.w * scalar};
}
inline Vec4 &operator+=(Vec4 &left, const Vec4 &right) {
  left = left + right;
  return left;
}
[[nodiscard]] inline double dot(const Vec2 &left, const Vec2 &right) {
  return left.x * right.x + left.y * right.y;
}
[[nodiscard]] inline double dot(const Vec3 &left, const Vec3 &right) {
  return left.x * right.x + left.y * right.y + left.z * right.z;
}
[[nodiscard]] inline double dot(const Vec4 &left, const Vec4 &right) {
  return left.x * right.x + left.y * right.y + left.z * right.z +
         left.w * right.w;
}
[[nodiscard]] inline double length(const Vec4 &value) {
  return std::sqrt(dot(value, value));
}
[[nodiscard]] inline double length(const Vec2 &value) {
  return std::sqrt(dot(value, value));
}
[[nodiscard]] inline Vec2 normalized(const Vec2 &value) {
  const double magnitude = length(value);
  if (magnitude <= 1.0e-12) {
    throw std::invalid_argument("cannot normalize a zero-length 2D vector");
  }
  return value * (1.0 / magnitude);
}
[[nodiscard]] inline Vec4 normalized(const Vec4 &value) {
  const double magnitude = length(value);
  if (magnitude <= 1.0e-12) {
    throw std::invalid_argument("cannot normalize a zero-length 4D vector");
  }
  return value * (1.0 / magnitude);
}
[[nodiscard]] inline Vec4 lerp(const Vec4 &from, const Vec4 &to,
                               double amount) {
  return from + (to - from) * amount;
}
[[nodiscard]] inline bool nearlyEqual(double left, double right,
                                      double tolerance = 1.0e-9) {
  return std::abs(left - right) <= tolerance;
}

} // namespace proj4d
