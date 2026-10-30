// F3.01: the Gizmos draw list (Unity names, fixed host list).
#include "rensga/core/gizmos.h"
#include "rensga_test.h"

using namespace rensga;

TEST(gizmos_primitives_fill_the_list) {
#if RENSGA_GIZMOS
  GizmoDraw::clear();
  CHECK_EQ(GizmoDraw::lineCount(), 0u);
  Gizmos::DrawLine(Vector3(0, 0, 0), Vector3(1, 2, 3));
  CHECK_EQ(GizmoDraw::lineCount(), 1u);
  Gizmos::DrawRay(Vector3(1, 1, 1), Vector3(0, 1, 0));
  CHECK_EQ(GizmoDraw::lineCount(), 2u);
  const GizmoLine& r = GizmoDraw::lines()[1];
  CHECK_NEAR(r.b[1], 2.0f, 1e-6);
  Gizmos::DrawWireCube(Vector3(0, 0, 0), Vector3(1, 1, 1));
  CHECK_EQ(GizmoDraw::lineCount(), 2u + 12u);
  Gizmos::DrawWireSphere(Vector3(0, 0, 0), 1.0f);
  CHECK_EQ(GizmoDraw::lineCount(), 2u + 12u + 96u);
  Gizmos::DrawCube(Vector3(0, 0, 0), Vector3(1, 1, 1));
  CHECK_EQ(GizmoDraw::triangleCount(), 12u);
  Gizmos::DrawSphere(Vector3(0, 0, 0), 1.0f);
  CHECK(GizmoDraw::triangleCount() > 12u);
  Gizmos::DrawFrustum(Vector3(0, 0, 0), 60, 10, 1, 1.5f);
  CHECK_EQ(GizmoDraw::dropped(), 0u);
  GizmoDraw::clear();
  CHECK_EQ(GizmoDraw::lineCount() + GizmoDraw::triangleCount(), 0u);
#endif
}

TEST(gizmos_color_and_matrix_apply_to_later_calls) {
#if RENSGA_GIZMOS
  GizmoDraw::clear();
  Gizmos::setColor(Color(1, 0, 0, 1));
  Gizmos::DrawLine(Vector3(0, 0, 0), Vector3(1, 0, 0));
  CHECK_EQ(GizmoDraw::lines()[0].rgba, 0xFF0000FFu);  // r low byte, a high byte
  Matrix4 m = Matrix4::translation(Vector3(10, 20, 30));
  Gizmos::setMatrix(m);
  Gizmos::DrawLine(Vector3(0, 0, 0), Vector3(1, 0, 0));
  CHECK_NEAR(GizmoDraw::lines()[1].a[0], 10.0f, 1e-5);
  CHECK_NEAR(GizmoDraw::lines()[1].a[2], 30.0f, 1e-5);
  CHECK_NEAR(GizmoDraw::lines()[1].b[0], 11.0f, 1e-5);
  GizmoDraw::clear();  // a new frame starts white, identity
  Gizmos::DrawLine(Vector3(0, 0, 0), Vector3(1, 0, 0));
  CHECK_EQ(GizmoDraw::lines()[0].rgba, 0xFFFFFFFFu);
  CHECK_NEAR(GizmoDraw::lines()[0].a[0], 0.0f, 1e-6);
  GizmoDraw::clear();
#endif
}

TEST(gizmos_list_is_fixed_size_and_counts_what_does_not_fit) {
#if RENSGA_GIZMOS
  GizmoDraw::clear();
  for (unsigned i = 0; i < GizmoDraw::kMaxLines + 10; ++i) Gizmos::DrawLine(Vector3(0, 0, 0), Vector3(1, 1, 1));
  CHECK_EQ(GizmoDraw::lineCount(), GizmoDraw::kMaxLines);
  CHECK_EQ(GizmoDraw::dropped(), 10u);
  GizmoDraw::clear();
  CHECK_EQ(GizmoDraw::dropped(), 0u);
#endif
}
