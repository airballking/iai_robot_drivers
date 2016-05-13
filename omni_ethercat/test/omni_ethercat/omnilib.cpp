#include <gtest/gtest.h>
#include <omni_ethercat/omnilib.hpp>

class OmnilibTest : public ::testing::Test
{
  protected:
    virtual void SetUp()
    {
      params.lx = 0.39225;
      params.ly = 0.303495;
      params.drive_constant = 626594.7934;
    }

    virtual void TearDown(){}

    omni_ethercat::JacParams params;

};

TEST_F(OmnilibTest, omniFKForewards)
{
  using namespace omni_ethercat;
  using Eigen::operator<<;
  Vector4d delta_wheels;

  delta_wheels << 0.1, 0.1, 0.1, 0.1;
  Vector3d twist2d = omniFK(params, delta_wheels);
  EXPECT_GT(twist2d(0), 0.0);
  EXPECT_DOUBLE_EQ(twist2d(1), 0.0);
  EXPECT_DOUBLE_EQ(twist2d(2), 0.0);
}

TEST_F(OmnilibTest, omniFKBackwards)
{
  using namespace omni_ethercat;
  using Eigen::operator<<;
  Vector4d delta_wheels;

  delta_wheels << -0.1, -0.1, -0.1, -0.1;
  Vector3d twist2d = omniFK(params, delta_wheels);
  EXPECT_LT(twist2d(0), 0.0);
  EXPECT_DOUBLE_EQ(twist2d(1), 0.0);
  EXPECT_DOUBLE_EQ(twist2d(2), 0.0);
}

TEST_F(OmnilibTest, omniFKLeft)
{
  using namespace omni_ethercat;
  using Eigen::operator<<;
  Vector4d delta_wheels;

  delta_wheels << -0.1, 0.1, 0.1, -0.1;
  Vector3d twist2d = omniFK(params, delta_wheels);
  EXPECT_DOUBLE_EQ(twist2d(0), 0.0);
  EXPECT_GT(twist2d(1), 0.0);
  EXPECT_DOUBLE_EQ(twist2d(2), 0.0);
}

TEST_F(OmnilibTest, omniFKRight)
{
  using namespace omni_ethercat;
  using Eigen::operator<<;
  Vector4d delta_wheels;

  delta_wheels << 0.1, -0.1, -0.1, 0.1;
  Vector3d twist2d = omniFK(params, delta_wheels);
  EXPECT_DOUBLE_EQ(twist2d(0), 0.0);
  EXPECT_LT(twist2d(1), 0.0);
  EXPECT_DOUBLE_EQ(twist2d(2), 0.0);
}

TEST_F(OmnilibTest, omniFKNorthWest)
{
  using namespace omni_ethercat;
  using Eigen::operator<<;
  Vector4d delta_wheels;

  delta_wheels << 0.0, 0.1, 0.1, 0.0;
  Vector3d twist2d = omniFK(params, delta_wheels);
  EXPECT_GT(twist2d(0), 0.0);
  EXPECT_GT(twist2d(1), 0.0);
  EXPECT_DOUBLE_EQ(twist2d(2), 0.0);
}

TEST_F(OmnilibTest, omniFKNorthEast)
{
  using namespace omni_ethercat;
  using Eigen::operator<<;
  Vector4d delta_wheels;

  delta_wheels << 0.1, 0.0, 0.0, 0.1;
  Vector3d twist2d = omniFK(params, delta_wheels);
  EXPECT_GT(twist2d(0), 0.0);
  EXPECT_LT(twist2d(1), 0.0);
  EXPECT_DOUBLE_EQ(twist2d(2), 0.0);
}

TEST_F(OmnilibTest, omniFKSouthWest)
{
  using namespace omni_ethercat;
  using Eigen::operator<<;
  Vector4d delta_wheels;

  delta_wheels << -0.1, 0.0, 0.0, -0.1;
  Vector3d twist2d = omniFK(params, delta_wheels);
  EXPECT_LT(twist2d(0), 0.0);
  EXPECT_GT(twist2d(1), 0.0);
  EXPECT_DOUBLE_EQ(twist2d(2), 0.0);
}

TEST_F(OmnilibTest, omniFKSouthEast)
{
  using namespace omni_ethercat;
  using Eigen::operator<<;
  Vector4d delta_wheels;

  delta_wheels << 0.0, -0.1, -0.1, 0.0;
  Vector3d twist2d = omniFK(params, delta_wheels);
  EXPECT_LT(twist2d(0), 0.0);
  EXPECT_LT(twist2d(1), 0.0);
  EXPECT_DOUBLE_EQ(twist2d(2), 0.0);
}

TEST_F(OmnilibTest, omniFKRotPositive)
{
  using namespace omni_ethercat;
  using Eigen::operator<<;
  Vector4d delta_wheels;

  delta_wheels << -0.1, 0.1, -0.1, 0.1;
  Vector3d twist2d = omniFK(params, delta_wheels);
  EXPECT_DOUBLE_EQ(twist2d(0), 0.0);
  EXPECT_DOUBLE_EQ(twist2d(1), 0.0);
  EXPECT_GT(twist2d(2), 0.0);
}

TEST_F(OmnilibTest, omniFKRotNegative)
{
  using namespace omni_ethercat;
  using Eigen::operator<<;
  Vector4d delta_wheels;

  delta_wheels << 0.1, -0.1, 0.1, -0.1;
  Vector3d twist2d = omniFK(params, delta_wheels);
  EXPECT_DOUBLE_EQ(twist2d(0), 0.0);
  EXPECT_DOUBLE_EQ(twist2d(1), 0.0);
  EXPECT_LT(twist2d(2), 0.0);
}