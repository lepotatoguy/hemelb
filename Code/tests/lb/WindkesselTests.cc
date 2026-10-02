// This file is part of HemeLB and is Copyright (C)
// the HemeLB team and/or their institutions, as detailed in the
// file AUTHORS. This software is provided under the terms of the
// license in the file LICENSE.

#include <catch2/catch.hpp>
#include "lb/iolets/InOutLetWindkessel.h"

namespace hemelb::tests
{
TEST_CASE("Windkessel zero-dimensional solutions", "[lb][windkessel]")
{
    configuration::WindkesselPressureIoletConfig c;
    c.model = "WK2";
    c.peripheral_resistance = 2;
    c.capacitance = 3;
    c.flow_m3s = 4;
    c.previous_flow_m3s = 4;
    c.pressure_Pa = 8;
    // Constant-flow equilibrium is P=R*Q for every timestep length.
    for (double dt : {0.01, 0.1, 1.0})
        REQUIRE(lb::InOutLetWindkessel::NextPressure(c, dt) == Approx(8));
    c.flow_m3s = 0;
    c.pressure_Pa = 12;
    for (int n = 0; n < 10; ++n)
        c.pressure_Pa = lb::InOutLetWindkessel::NextPressure(c, 0.2);
    REQUIRE(c.pressure_Pa == Approx(12 * std::pow(1 + 0.2 / 6, -10)));
    c.model = "WK3";
    c.characteristic_resistance = 1;
    c.flow_m3s = c.previous_flow_m3s = 4;
    c.pressure_Pa = 12;
    REQUIRE(lb::InOutLetWindkessel::NextPressure(c, 0.1) == Approx(12));
    c.flow_m3s = 3;
    c.previous_flow_m3s = 2;
    c.pressure_Pa = 5;
    // Independent substitution in P + Rp*C*dP/dt = (Rc+Rp)Q + Rc*Rp*C*dQ/dt.
    REQUIRE(lb::InOutLetWindkessel::NextPressure(c, 1) == Approx((30.0 + 12 + 6) / 7));
}
TEST_CASE("Windkessel flow orientation and restart state", "[lb][windkessel]")
{
    configuration::WindkesselPressureIoletConfig c;
    c.model = "WK3";
    c.peripheral_resistance = 2;
    c.characteristic_resistance = 1;
    c.capacitance = 3;
    c.area_m2 = 2;
    util::UnitConverter units(0.1, 0.2, PhysicalPosition::Zero(), 1000, 0);
    lb::InOutLetWindkessel wk(c);
    wk.SetNormal({0, 0, -1});
    wk.Initialise(&units);
    REQUIRE(wk.GetDensity(0) == Approx(1));
    wk.ObserveVelocity({0, 0, 0.01});
    wk.ObserveVelocity({0, 0, 0.03});
    wk.Advance(wk.GetFlowSample());
    REQUIRE(wk.GetConfig().flow_m3s == Approx(0.08));
    lb::InOutLetWindkessel resumed(wk.GetConfig());
    resumed.SetNormal(wk.GetNormal());
    resumed.Initialise(&units);
    wk.ObserveVelocity({0, 0, 0.03});
    resumed.ObserveVelocity({0, 0, 0.03});
    wk.Advance(wk.GetFlowSample());
    resumed.Advance(resumed.GetFlowSample());
    REQUIRE(wk.GetDensity(0) == resumed.GetDensity(0));
    REQUIRE(wk.GetConfig().previous_flow_m3s == resumed.GetConfig().previous_flow_m3s);
}
TEST_CASE("Windkessel pulsatile solution converges to independent RC and RCR solutions",
          "[windkessel]")
{
    for (auto model : {"WK2", "WK3"})
    {
        std::array<double, 3> errors{};
        for (int refinement = 0; refinement < 3; ++refinement)
        {
            configuration::WindkesselPressureIoletConfig c;
            c.model = model;
            c.peripheral_resistance = 2;
            c.capacitance = 0.2;
            c.characteristic_resistance = c.model == "WK3" ? 1 : 0;
            int steps = 200 * (1 << refinement);
            double dt = 2. / steps, omega = 3;
            c.previous_flow_m3s = std::sin(-omega * dt);
            for (int n = 1; n <= steps; ++n)
            {
                c.pressure_Pa = lb::InOutLetWindkessel::NextPressure(c, dt);
                c.previous_flow_m3s = c.flow_m3s;
                c.flow_m3s = std::sin(omega * n * dt);
            }
            double tau = c.peripheral_resistance * c.capacitance;
            double real = c.characteristic_resistance +
                          c.peripheral_resistance / (1 + omega * omega * tau * tau);
            double imag = -c.peripheral_resistance * omega * tau / (1 + omega * omega * tau * tau);
            double exact =
                real * std::sin(omega * 2) + imag * (std::cos(omega * 2) - std::exp(-2 / tau));
            errors[refinement] = std::abs(c.pressure_Pa - exact);
        }
        REQUIRE(errors[1] < 0.7 * errors[0]);
        REQUIRE(errors[2] < 0.7 * errors[1]);
    }
}

} // namespace hemelb::tests
