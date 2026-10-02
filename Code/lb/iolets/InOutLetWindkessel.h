// This file is part of HemeLB and is Copyright (C)
// the HemeLB team and/or their institutions, as detailed in the
// file AUTHORS. This software is provided under the terms of the
// license in the file LICENSE.

#ifndef HEMELB_LB_IOLETS_INOUTLETWINDKESSEL_H
#define HEMELB_LB_IOLETS_INOUTLETWINDKESSEL_H
#include <array>
#include <cmath>
#include "configuration/SimConfig.h"
#include "lb/iolets/InOutLet.h"

namespace hemelb::lb
{
// SI state retains pressure and both completed flow time levels in checkpoints.
class InOutLetWindkessel : public InOutLet
{
    configuration::WindkesselPressureIoletConfig config;
    util::UnitConverter const *units = nullptr;
    double speedSum = 0;
    double siteCount = 0;

  public:
    explicit InOutLetWindkessel(configuration::WindkesselPressureIoletConfig c)
        : config(std::move(c))
    {
    }
    InOutLet *clone() const override { return new InOutLetWindkessel(*this); }
    void Initialise(util::UnitConverter const *u) override { units = u; }
    void Reset(SimulationState &) override { speedSum = siteCount = 0; }
    LatticeDensity GetDensity(LatticeTimeStep) const override
    {
        return units->ConvertPressureToLatticeUnits(config.pressure_Pa) / Cs2;
    }
    LatticeDensity GetDensityMin() const override { return GetDensity(0); }
    LatticeDensity GetDensityMax() const override { return GetDensity(0); }
    void ObserveVelocity(LatticeVelocity const &velocity) override
    {
        speedSum += Dot(velocity, -GetNormal());
        siteCount += 1;
    }
    std::array<double, 2> GetFlowSample() const { return {speedSum, siteCount}; }
    void Advance(std::array<double, 2> total)
    {
        // Source solver convention: update from completed-flow history, then
        // store the current samples. Communication adds no further time lag.
        config.pressure_Pa = NextPressure(config, units->GetTimeStep());
        config.previous_flow_m3s = config.flow_m3s;
        config.flow_m3s = total[1] == 0 ? 0
                                        : units->ConvertSpeedToPhysicalUnits(total[0] / total[1]) *
                                              config.area_m2;
        if (!std::isfinite(config.pressure_Pa) || !std::isfinite(config.flow_m3s) ||
            GetDensity(0) <= 0)
            throw Exception()
                << "Windkessel produced a non-finite pressure/flow or non-positive density";
        speedSum = siteCount = 0;
    }
    static double NextPressure(configuration::WindkesselPressureIoletConfig const &c, double dt)
    {
        auto rc = c.characteristic_resistance;
        auto rp = c.peripheral_resistance;
        auto compliance = c.capacitance;
        auto q = c.flow_m3s;
        if (c.model == "WK3")
            return (rp * compliance / dt * c.pressure_Pa +
                    (rc + rp) * (2 * q - c.previous_flow_m3s) +
                    rc * rp * compliance / dt * (q - c.previous_flow_m3s)) /
                   (1 + rp * compliance / dt);
        return rp / (dt + rp * compliance) * (dt * q + compliance * c.pressure_Pa);
    }
    configuration::WindkesselPressureIoletConfig const &GetConfig() const { return config; }
};
} // namespace hemelb::lb
#endif
