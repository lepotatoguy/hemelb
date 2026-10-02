// This file is part of HemeLB and is Copyright (C)
// the HemeLB team and/or their institutions, as detailed in the
// file AUTHORS. This software is provided under the terms of the
// license in the file LICENSE.

#include <catch2/catch.hpp>
#include <fstream>
#include "lb/iolets/InOutLetReadWriteVelocity.h"
#include "lb/iolets/InOutLetWomersleyElasticVelocity.h"
#include "lb/streamers/GuoZhengShiElasticWall.h"
#include "tests/helpers/FolderTestFixture.h"
#include "util/Bessel.h"
#include "tracers/TracerController.h"
#include "lb/Streamers.h"
#include "lb/collisions/Normal.h"
#include "lb/kernels/LBGK.h"
#include "lb/lattices/D3Q19.h"

namespace hemelb::tests
{
TEST_CASE("Explicit wall and iolet choices select their requested delegates", "[boundary]")
{
    using C = lb::Normal<lb::LBGK<lb::D3Q19>>;
    using WS = lb::StreamerTypeFactory<lb::BounceBackLink<C>, lb::NullLink<C>>;
    using IS = lb::StreamerTypeFactory<lb::NullLink<C>, lb::NashZerothOrderPressureLink<C>>;
    STATIC_REQUIRE((std::same_as<lb::SelectedCornerStreamer<"NASHZEROTHORDERPRESSURESBB", WS, IS>,
                  lb::StreamerTypeFactory<lb::BounceBackLink<C>, lb::NashZerothOrderPressureLink<C>>>));
    STATIC_REQUIRE((std::same_as<lb::SelectedCornerStreamer<"NASHZEROTHORDERPRESSUREBFL", WS, IS>,
                  lb::StreamerTypeFactory<lb::BouzidiFirdaousLallemandLink<C>, lb::NashZerothOrderPressureLink<C>>>));
    STATIC_REQUIRE((std::same_as<lb::SelectedCornerStreamer<"NASHZEROTHORDERPRESSUREGZS", WS, IS>,
                  lb::StreamerTypeFactory<lb::GuoZhengShiLink<C>, lb::NashZerothOrderPressureLink<C>>>));
    STATIC_REQUIRE((std::same_as<lb::SelectedCornerStreamer<"NASHZEROTHORDERPRESSUREGZSE", WS, IS>,
                  lb::StreamerTypeFactory<lb::GuoZhengShiElasticWallLink<C>, lb::NashZerothOrderPressureLink<C>>>));
    STATIC_REQUIRE((std::same_as<lb::SelectedCornerStreamer<"YANGPRESSURESBB", WS, IS>,
                  lb::StreamerTypeFactory<lb::BounceBackLink<C>, lb::YangPressureLink<C>>>));
    STATIC_REQUIRE((std::same_as<lb::SelectedCornerStreamer<"YANGPRESSUREBFL", WS, IS>,
                  lb::StreamerTypeFactory<lb::BouzidiFirdaousLallemandLink<C>, lb::YangPressureLink<C>>>));
    STATIC_REQUIRE((std::same_as<lb::SelectedCornerStreamer<"YANGPRESSUREGZS", WS, IS>,
                  lb::StreamerTypeFactory<lb::GuoZhengShiLink<C>, lb::YangPressureLink<C>>>));
    STATIC_REQUIRE((std::same_as<lb::SelectedCornerStreamer<"YANGPRESSUREGZSE", WS, IS>,
                  lb::StreamerTypeFactory<lb::GuoZhengShiElasticWallLink<C>, lb::YangPressureLink<C>>>));
    STATIC_REQUIRE((std::same_as<lb::SelectedCornerStreamer<"LADDIOLETSBB", WS, IS>,
                  lb::StreamerTypeFactory<lb::BounceBackLink<C>, lb::LaddIoletLink<C>>>));
    STATIC_REQUIRE((std::same_as<lb::SelectedCornerStreamer<"LADDIOLETBFL", WS, IS>,
                  lb::StreamerTypeFactory<lb::BouzidiFirdaousLallemandLink<C>, lb::LaddIoletLink<C>>>));
    STATIC_REQUIRE((std::same_as<lb::SelectedCornerStreamer<"LADDIOLETGZS", WS, IS>,
                  lb::StreamerTypeFactory<lb::GuoZhengShiLink<C>, lb::LaddIoletLink<C>>>));
    STATIC_REQUIRE((std::same_as<lb::SelectedCornerStreamer<"LADDIOLETGZSE", WS, IS>,
                  lb::StreamerTypeFactory<lb::GuoZhengShiElasticWallLink<C>, lb::LaddIoletLink<C>>>));
}
TEST_CASE("Elastic wall compliance has rigid and pressure limits", "[elastic]")
{
    REQUIRE(lb::ElasticWallVelocityRatio(1, 1, 0) == 0);
    REQUIRE(lb::ElasticWallVelocityRatio(1, 1, 0.1) == Approx(0.1));
    REQUIRE(lb::ElasticWallVelocityRatio(0.9, 1, 0) == 0);
    REQUIRE(lb::ElasticWallVelocityRatio(1.3, 1, 0) == Approx(1.0 / 11));
    REQUIRE(lb::ElasticWallVelocityRatio(1.3, 1e12, 0) == Approx(0).margin(1e-12));
    REQUIRE_THROWS(lb::ElasticWallVelocityRatio(1, 0, 0));
}
TEST_CASE("Complex Bessel J1 agrees with real Bessel and J0 derivative", "[elastic]")
{
    // Independent integral representation J1(x)=integral cos(theta-x*sin(theta))/pi.
    for (double x : {0., 0.1, 1., 5., 10.})
    {
        double sum = 0;
        constexpr int n = 4096;
        for (int k = 0; k <= n; ++k)
        {
            double theta = PI * k / n;
            sum += (k == 0 || k == n ? 1 : k % 2 ? 4 : 2) * std::cos(theta - x * std::sin(theta));
        }
        REQUIRE(util::BesselJ1ComplexArgument({x, 0}).real() ==
                Approx(sum / (3 * n)).margin(1e-10));
    }
    for (std::complex<double> z : {std::complex<double>{1, 2}, {-3, 4}, {5, -2}})
    {
        double h = 1e-4;
        auto derivative = (util::BesselJ0ComplexArgument(z + h, 1e-24) -
                           util::BesselJ0ComplexArgument(z - h, 1e-24)) /
                          (2 * h);
        REQUIRE(std::abs(derivative + util::BesselJ1ComplexArgument(z)) < 1e-6);
    }
    REQUIRE_THROWS(util::BesselJ1ComplexArgument({NAN, 0}));
}
TEST_CASE("Elastic Womersley preserves period and axial phase", "[elastic]")
{
    lb::InOutLetWomersleyElasticVelocity iolet;
    iolet.SetNormal({0, 0, 1});
    iolet.SetPosition({0, 0, 0});
    iolet.SetRadius(5);
    iolet.SetPeriod(100);
    iolet.SetWomersleyNumber(2);
    iolet.SetPoissonRatio(0.3);
    iolet.SetWallYoungsModulus(2);
    iolet.SetPressureGradientAmplitude(1e-5);
    iolet.SetAxialPosition(0);
    auto a = iolet.GetVelocity({0, 0, 0}, 3), b = iolet.GetVelocity({0, 0, 0}, 103);
    REQUIRE(std::isfinite(a.z()));
    REQUIRE(a.z() == Approx(b.z()).margin(1e-12));
    REQUIRE(a.x() == 0);
    REQUIRE(a.y() == 0);
    iolet.SetPressureGradientAmplitude(0);
    REQUIRE(iolet.GetVelocity({2, 0, 0}, 3).GetMagnitude() == 0);
}
TEST_CASE_METHOD(helpers::FolderTestFixture,
                 "ReadWrite flow conversion, timing, pressure and saved state", "[coupling]")
{
    MoveToTempdir();
    configuration::ReadWriteVelocityIoletConfig c;
    c.radius_m = 1;
    c.area_m2 = 2;
    c.flow_path = "flow.txt";
    c.pressure_path = "pressure.txt";
    c.frequency = 4;
    c.flow_conversion = 2;
    c.pressure_conversion = 3;
    c.smoothing = 0.5;
    c.timeout_s = 0.01;
    c.average_density = 1.001;
    std::ofstream(c.flow_path) << "3 0.1\n";
    util::UnitConverter units(0.1, 1, PhysicalPosition::Zero(), 1000, 0);
    lb::BoundaryCommunicator comm(net::MpiCommunicator::World());
    lb::InOutLetReadWriteVelocity inlet(c);
    inlet.SetNormal({0, 0, 1});
    inlet.SetPosition({0, 0, 0});
    inlet.Initialise(&units);
    inlet.BeginStep(comm, 1);
    REQUIRE(inlet.GetConfig().max_speed_ms == Approx(0.2));
    inlet.BeginStep(comm, 2);
    REQUIRE(inlet.GetConfig().max_speed_ms == Approx(0.2));
    inlet.EndStep(comm);
    REQUIRE(inlet.GetConfig().max_speed_ms == Approx(0.2));
    REQUIRE(inlet.GetVelocity({0, 0, 0}, 3).z() == Approx(0.02));
    REQUIRE(inlet.GetVelocity({1, 0, 0}, 3).z() == 0);
    double time, pressure;
    std::ifstream(c.pressure_path) >> time >> pressure;
    REQUIRE(time == Approx(3.4));
    REQUIRE(pressure == Approx(3 * units.ConvertPressureToPhysicalUnits(1.001 * Cs2)));
    REQUIRE(inlet.GetConfig().next_exchange == 6);
    REQUIRE(inlet.GetConfig().start_time_s == 3);
    inlet.ObserveSite(1.002, {});
    inlet.ObserveSite(1.004, {});
    inlet.EndStep(comm);
    REQUIRE(inlet.GetConfig().average_density == Approx(1.003));
    lb::InOutLetReadWriteVelocity restored(inlet.GetConfig());
    restored.Initialise(&units);
    REQUIRE(restored.GetConfig().average_density == inlet.GetConfig().average_density);
    REQUIRE_THROWS_WITH(restored.BeginStep(comm, 6), Catch::Matchers::Contains("Timed out"));
}
TEST_CASE_METHOD(helpers::FolderTestFixture, "Weighted coupling normalises supplied flow",
                 "[coupling]")
{
    MoveToTempdir();
    std::ofstream("weights.txt") << "0 0 0 1\n0 0 1 3\n";
    std::ofstream("flow.txt") << "5 0.8\n";
    configuration::ReadWriteVelocityIoletConfig c;
    c.radius_m = 1;
    c.area_m2 = 2;
    c.flow_path = "flow.txt";
    c.pressure_path = "pressure.txt";
    c.weights_path = "weights.txt";
    util::UnitConverter units(0.1, 0.2, PhysicalPosition::Zero(), 1000, 0);
    lb::InOutLetReadWriteVelocity inlet(c);
    inlet.SetNormal({0, 0, -1});
    inlet.Initialise(&units);
    REQUIRE(inlet.GetConfig().start_time_s == 5);
    REQUIRE(inlet.GetConfig().max_speed_ms == Approx(5));
    REQUIRE(inlet.GetVelocity({0, 0, 1.5}, 3).z() == Approx(-7.5));
    lb::VelocityWeights weights;
    weights.Load("weights.txt");
    REQUIRE(weights.Sum() == 4);
    REQUIRE(weights.At({0, 0, -0.5}, {0, 0, 1}) == 1);
    REQUIRE(weights.At({4, 0, 0}, {0, 0, 1}) == 0);
    std::ofstream("weights.txt") << "0 0 0 nan\n";
    REQUIRE_THROWS(weights.Load("weights.txt"));
    std::ofstream("weights.txt") << "0 0 0 1\n0 0 0 2\n";
    REQUIRE_THROWS(weights.Load("weights.txt"));
}
TEST_CASE("Tracer interpolation reproduces affine fields and wall damping limits", "[tracers]")
{
    for (double x : {0., 0.1, 0.5, 0.9, 1.3})
    {
        double sum = 0, first = 0;
        for (int i = -2; i <= 4; ++i)
        {
            double weight = tracers::Kernel(i - x);
            sum += weight;
            first += i * weight;
        }
        REQUIRE(sum == Approx(1));
        REQUIRE(first == Approx(x).margin(1e-12));
    }
    REQUIRE(tracers::Lubrication({1, 2, 3}, {2, 0, 0}, 0.1, 1).x() == 1);
    auto damped = tracers::Lubrication({1, 2, 3}, {0.1, 0, 0}, 0.1, 1);
    REQUIRE(damped.x() == 0);
    REQUIRE(damped.y() == 2);
    REQUIRE(damped.z() == 3);
    tracers::TracerConfig a;
    a.emissionCount = 4;
    a.sphereRadius = 2;
    a.seed = 7;
    auto b = a;
    tracers::Emit(a, 1);
    tracers::Emit(b, 1);
    REQUIRE(a.nextId == b.nextId);
    for (std::size_t i = 0; i < a.particles.size(); ++i)
    {
        REQUIRE(a.particles[i].position == b.particles[i].position);
        REQUIRE(a.particles[i].position.GetMagnitude() == Approx(2));
    }
}

} // namespace hemelb::tests
