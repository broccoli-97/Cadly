#include "TestChecks.h"

#include <Intf_InterferencePolygon2d.hxx>
#include <Intf_Polygon2d.hxx>
#include <Intf_SectionLine.hxx>
#include <Intf_SectionPoint.hxx>
#include <Intf_TangentZone.hxx>
#include <gp_Pnt2d.hxx>

// Compile the unmodified, pinned upstream implementation under a separate
// class name. Both algorithms then see the same polygons in one process.
#undef _Intf_InterferencePolygon2d_HeaderFile
#define Intf_InterferencePolygon2d CadlyReferenceInterferencePolygon2d
#include <Intf_InterferencePolygon2d.hxx>
#undef Intf_InterferencePolygon2d

#include <cmath>
#include <random>
#include <utility>
#include <vector>

namespace {

class Polygon final : public Intf_Polygon2d {
public:
  Polygon(std::vector<gp_Pnt2d> points, double deflection, bool closed = false)
      : points_(std::move(points)), deflection_(deflection), closed_(closed) {
    for (const auto& point : points_) myBox.Add(point);
    myBox.Enlarge(deflection_);
  }
  Standard_Boolean Closed() const override { return closed_; }
  Standard_Real DeflectionOverEstimation() const override { return deflection_; }
  Standard_Integer NbSegments() const override {
    return static_cast<Standard_Integer>(points_.size()) - (closed_ ? 0 : 1);
  }
  void Segment(Standard_Integer index, gp_Pnt2d& first, gp_Pnt2d& last) const override {
    first = points_[static_cast<std::size_t>(index - 1)];
    last = points_[static_cast<std::size_t>(index) % points_.size()];
  }
private:
  std::vector<gp_Pnt2d> points_;
  double deflection_;
  bool closed_;
};

void compare_point(const Intf_SectionPoint& a, const Intf_SectionPoint& b) {
  CHECK(a.Pnt().X() == b.Pnt().X());
  CHECK(a.Pnt().Y() == b.Pnt().Y());
  CHECK(a.Pnt().Z() == b.Pnt().Z());
  CHECK(a.Incidence() == b.Incidence());
  Intf_PIType a_type, b_type;
  int a_index = 0, b_index = 0;
  double a_parameter = 0, b_parameter = 0;
  a.InfoFirst(a_type, a_index, a_parameter);
  b.InfoFirst(b_type, b_index, b_parameter);
  CHECK(a_type == b_type && a_index == b_index && a_parameter == b_parameter);
  a.InfoSecond(a_type, a_index, a_parameter);
  b.InfoSecond(b_type, b_index, b_parameter);
  CHECK(a_type == b_type && a_index == b_index && a_parameter == b_parameter);
}

void compare(const Intf_Interference& a, const Intf_Interference& b) {
  if (CHECK(a.NbSectionPoints() == b.NbSectionPoints()))
    for (int i = 1; i <= a.NbSectionPoints(); ++i) compare_point(a.PntValue(i), b.PntValue(i));
  if (CHECK(a.NbSectionLines() == b.NbSectionLines())) {
    for (int i = 1; i <= a.NbSectionLines(); ++i) {
      const auto& x = a.LineValue(i);
      const auto& y = b.LineValue(i);
      if (CHECK(x.NumberOfPoints() == y.NumberOfPoints()))
        for (int j = 1; j <= x.NumberOfPoints(); ++j) compare_point(x.GetPoint(j), y.GetPoint(j));
    }
  }
  if (CHECK(a.NbTangentZones() == b.NbTangentZones())) {
    for (int i = 1; i <= a.NbTangentZones(); ++i) {
      const auto& x = a.ZoneValue(i);
      const auto& y = b.ZoneValue(i);
      if (CHECK(x.NumberOfPoints() == y.NumberOfPoints()))
        for (int j = 1; j <= x.NumberOfPoints(); ++j) compare_point(x.GetPoint(j), y.GetPoint(j));
      double x_min, x_max, y_min, y_max;
      x.ParamOnFirst(x_min, x_max);
      y.ParamOnFirst(y_min, y_max);
      CHECK(x_min == y_min && x_max == y_max);
      x.ParamOnSecond(x_min, x_max);
      y.ParamOnSecond(y_min, y_max);
      CHECK(x_min == y_min && x_max == y_max);
    }
  }
}

void check_pair(const Polygon& a, const Polygon& b) {
  compare(Intf_InterferencePolygon2d(a, b), CadlyReferenceInterferencePolygon2d(a, b));
  compare(Intf_InterferencePolygon2d(b, a), CadlyReferenceInterferencePolygon2d(b, a));
  compare(Intf_InterferencePolygon2d(a), CadlyReferenceInterferencePolygon2d(a));
}

std::vector<gp_Pnt2d> circle(int count, double x, double radius) {
  std::vector<gp_Pnt2d> points;
  for (int i = 0; i < count; ++i) {
    const double angle = 2 * std::acos(-1.0) * i / count;
    points.emplace_back(x + radius * std::cos(angle), radius * std::sin(angle));
  }
  return points;
}

} // namespace

int main() {
  // Open/closed curves, tangencies, overlap, repeated points, unequal box
  // margins, and the threshold between linear scanning and the index.
  for (const int count : {8, 63, 64, 65, 128, 257}) {
    for (const double tolerance : {0.0, 1e-7, 0.025}) {
      const Polygon a(circle(count, 0, 1), tolerance, true);
      for (const double shift : {0.0, 1.0, 2.0, 2.00001, 3.0})
        check_pair(a, Polygon(circle(count, shift, 1), tolerance / 2, true));
      std::vector<gp_Pnt2d> line, crossing;
      for (int i = 0; i <= count; ++i) {
        line.emplace_back(static_cast<double>(i / 2), 0);
        crossing.emplace_back(count * 0.25, (i - count * 0.5) * 0.25);
      }
      check_pair(Polygon(line, tolerance), Polygon(crossing, tolerance / 2));
    }
  }
  std::mt19937 generator(218713);
  std::uniform_real_distribution<double> distribution(-1, 1);
  for (int iteration = 0; iteration < 80; ++iteration) {
    std::vector<gp_Pnt2d> a, b;
    double ax = 0, ay = 0, bx = 0, by = 0;
    for (int i = 0; i < 130; ++i) {
      ax += distribution(generator); ay += distribution(generator);
      bx += distribution(generator); by += distribution(generator);
      a.emplace_back(ax, ay); b.emplace_back(bx, by);
    }
    check_pair(Polygon(a, 0.0001, iteration % 2 == 0),
               Polygon(b, 0.01, iteration % 3 == 0));
  }
  return cadly::tests::report();
}
