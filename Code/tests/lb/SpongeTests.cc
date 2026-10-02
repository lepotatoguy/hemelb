// This file is part of HemeLB and is Copyright (C)
// the HemeLB team and/or their institutions, as detailed in the
// file AUTHORS. This software is provided under the terms of the
// license in the file LICENSE.

#include <catch2/catch.hpp>
#include "lb/kernels/Sponge.h"
#include "lb/lattices/D3Q15.h"
#include "lb/kernels/TRT.h"
#include "tests/helpers/FourCubeBasedTestFixture.h"

namespace hemelb::tests
{
template <class T> struct SpongeFixture : helpers::FourCubeBasedTestFixture<>
{
};
TEST_CASE("Sponge spatial profile and time decay", "[lb][sponge]")
{
    REQUIRE(lb::SpongeTau(0.8, 4, 0, 10, 0, 100) == Approx(1.7));
    REQUIRE(lb::SpongeTau(0.8, 4, 5, 10, 0, 100) == Approx(1.025));
    REQUIRE(lb::SpongeTau(0.8, 4, 0, 10, 75, 100) == Approx(1.25));
    REQUIRE(lb::SpongeTau(0.8, 4, 0, 10, 100, 100) == Approx(0.8));
    REQUIRE(lb::SpongeTau(0.8, 4, 11, 10, 0, 100) == Approx(0.8));
    REQUIRE(lb::SmagorinskyTau(0.8, 1, 0, 0.1) == Approx(0.8));
    REQUIRE(lb::SmagorinskyTau(0.8, 1, 10, 0) == Approx(0.8));
    double tau = lb::SmagorinskyTau(0.8, 1, 0.03, 0.1);
    // The positive root solves tau*(tau-baseTau)=3*C_s^2*|Pi_neq|/(2*rho*Cs^4).
    REQUIRE(tau * (tau - 0.8) == Approx(4.5 * std::sqrt(2.0) * 0.01 * 0.03));
}
TEMPLATE_TEST_CASE_METHOD(SpongeFixture, "Sponge collision conserves mass and momentum",
                          "[lb][sponge]", lb::LBGKSL<lb::D3Q15>, lb::TRTSL<lb::D3Q15>,
                          lb::LBGKLESSL<lb::D3Q15>)
{
    this->lbmParams.spongeRatio = 2;
    this->lbmParams.spongeWidth = 10;
    this->lbmParams.spongeLifetime = 100;
    this->lbmParams.outletPositions = {{2, 2, 2}};
    auto init = this->initParams;
    init.lbmParams = &this->lbmParams;
    init.state = this->simState.get();
    TestType kernel(init);
    std::array<double, 15> f{};
    for (Direction d = 0; d < 15; ++d)
        f[d] = lb::D3Q15::EQMWEIGHTS[d] * (1 + 0.001 * d);
    typename TestType::VarsType v(f.data());
    kernel.CalculateDensityMomentumFeq(v, 0);
    kernel.Collide(&this->lbmParams, v);
    double mass = 0;
    LatticeVelocity momentum = LatticeVelocity::Zero();
    for (Direction d = 0; d < 15; ++d)
    {
        mass += v.GetFPostCollision()[d];
        momentum += lb::D3Q15::VECTORS[d].as<double>() * v.GetFPostCollision()[d];
    }
    REQUIRE(mass == Approx(v.density));
    for (int a = 0; a < 3; ++a)
        REQUIRE(momentum[a] == Approx(v.momentum[a]).margin(1e-14));
}
TEST_CASE_METHOD(helpers::FourCubeBasedTestFixture<>, "TRT rest direction is not treated as a pair",
                 "[lb][trt]")
{
    lb::TRT<lb::D3Q15> kernel(initParams);
    std::array<double, 15> f{};
    for (Direction d = 0; d < 15; ++d)
        f[d] = lb::D3Q15::EQMWEIGHTS[d] * (1 + 0.001 * d);
    lb::TRT<lb::D3Q15>::VarsType v(f.data());
    kernel.CalculateDensityMomentumFeq(v, 0);
    kernel.Collide(&lbmParams, v);
    double mass = 0;
    for (auto value : v.GetFPostCollision())
        mass += value;
    REQUIRE(mass == Approx(v.density));
}
} // namespace hemelb::tests
