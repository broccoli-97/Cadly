#include "TestChecks.h"

#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <Precision.hxx>
#include <gp_Ax2.hxx>

int main() {
  // GCC-built MSYS2 OCCT 7.9.3 dispatched a recursive point query to the
  // line selector, then crashed reading a nonexistent curve object. A rotated
  // box reaches that path without needing the private STEP files that exposed it.
  const gp_Ax2 axes(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0),
                    gp_Dir(1.0, 1.0, 0.0));
  const TopoDS_Shape box = BRepPrimAPI_MakeBox(axes, 2.0, 2.0, 3.0);
  BRepClass3d_SolidClassifier classifier(box);

  classifier.Perform(gp_Pnt(0.0, 0.7, 0.0), Precision::Confusion());
  CHECK(classifier.State() == TopAbs_ON);
  classifier.Perform(gp_Pnt(0.0, 0.7, 3.0), Precision::Confusion());
  CHECK(classifier.State() == TopAbs_ON);
  classifier.Perform(gp_Pnt(0.0, 0.7, 1.5), Precision::Confusion());
  CHECK(classifier.State() == TopAbs_IN);
  classifier.Perform(gp_Pnt(0.0, -0.7, 1.5), Precision::Confusion());
  CHECK(classifier.State() == TopAbs_OUT);
  classifier.PerformInfinitePoint(Precision::Confusion());
  CHECK(classifier.State() == TopAbs_OUT);

  return cadly::tests::report();
}
