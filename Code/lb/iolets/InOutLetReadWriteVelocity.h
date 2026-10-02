// This file is part of HemeLB and is Copyright (C)
// the HemeLB team and/or their institutions, as detailed in the
// file AUTHORS. This software is provided under the terms of the
// license in the file LICENSE.

#ifndef HEMELB_LB_IOLETS_INOUTLETREADWRITEVELOCITY_H
#define HEMELB_LB_IOLETS_INOUTLETREADWRITEVELOCITY_H
#include <chrono>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <thread>
#include "lb/iolets/VelocityWeights.h"
#include "configuration/SimConfig.h"
#include "lb/iolets/InOutLetVelocity.h"
#include "lb/iolets/BoundaryCommunicator.h"

namespace hemelb::lb
{
class InOutLetReadWriteVelocity : public InOutLetVelocity
{
    configuration::ReadWriteVelocityIoletConfig config;
    util::UnitConverter const *units = nullptr;
    double densitySum = 0, count = 0;
    std::optional<double> pendingSpeed;
    VelocityWeights weights;
    double flowArea = 0;

  public:
    explicit InOutLetReadWriteVelocity(configuration::ReadWriteVelocityIoletConfig c)
        : config(std::move(c))
    {
    }
    InOutLet *clone() const override { return new InOutLetReadWriteVelocity(*this); }
    void Initialise(util::UnitConverter const *u) override
    {
        units = u;
        SetRadius(u->ConvertDistanceToLatticeUnits(config.radius_m));
        flowArea = 0.5 * config.area_m2;
        if (config.weights_path)
        {
            weights.Load(*config.weights_path);
            flowArea = weights.Sum() * u->GetVoxelSize() * u->GetVoxelSize();
        }
        if (!std::isfinite(flowArea) || flowArea <= 0)
            throw Exception() << "Invalid readWrite flow area";
        if (!config.start_time_s)
        {
            std::ifstream input(config.flow_path);
            double time, flow;
            if (!(input >> time >> flow) || !std::isfinite(time) || !std::isfinite(flow))
                throw Exception() << "Invalid initial readWrite flow record: " << config.flow_path;
            config.start_time_s = time;
            config.max_speed_ms = flow * config.flow_conversion / flowArea;
            if (!std::isfinite(config.max_speed_ms))
                throw Exception() << "Initial readWrite flow conversion overflow";
        }
    }
    void ObserveSite(LatticeDensity density, LatticeVelocity const &) override
    {
        densitySum += density;
        count += 1;
    }
    void EndStep(BoundaryCommunicator const &comm)
    {
        std::array<double, 2> samples{densitySum, count};
        comm.AllReduceInPlace(std::span<double>(samples), MPI_SUM);
        if (samples[1])
            config.average_density = samples[0] / samples[1];
        densitySum = count = 0;
        if (pendingSpeed)
        {
            config.max_speed_ms = *pendingSpeed;
            pendingSpeed.reset();
        }
    }
    void BeginStep(BoundaryCommunicator const &comm, LatticeTimeStep step)
    {
        auto warmup = config.warmup_steps.value_or(0);
        if (step < warmup + config.next_exchange)
            return;
        double expected = *config.start_time_s + (config.next_exchange - 2) * units->GetTimeStep();
        std::string error;
        double next = config.max_speed_ms;
        if (comm.IsCurrentProcTheBCProc())
        {
            auto deadline =
                std::chrono::steady_clock::now() + std::chrono::duration<double>(config.timeout_s);
            bool received = false;
            while (std::chrono::steady_clock::now() < deadline)
            {
                std::ifstream stream(config.flow_path);
                double time, value;
                if (stream >> time >> value)
                {
                    if (std::isfinite(time) && std::isfinite(value) &&
                        std::abs(time - expected) < 0.5 * units->GetTimeStep())
                    {
                        next = config.smoothing * (value * config.flow_conversion / flowArea) +
                               (1 - config.smoothing) * config.max_speed_ms;
                        received = true;
                        break;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if (received && !std::isfinite(next))
            {
                received = false;
                error = "Coupling flow conversion overflow";
            }
            if (!received)
            {
                if (error.empty())
                    error = "Timed out waiting for flowRateFilePath at physical time " +
                            std::to_string(expected);
            }
            else
            {
                auto temporary = config.pressure_path;
                temporary += ".tmp";
                std::ofstream output(temporary);
                double pressure =
                    units->ConvertPressureToPhysicalUnits(config.average_density * Cs2);
                if (config.pressure_mmHg)
                    pressure /= mmHg_TO_PASCAL;
                output << std::setprecision(17)
                       << *config.start_time_s +
                              (config.next_exchange + config.frequency - 2) * units->GetTimeStep()
                       << " " << pressure * config.pressure_conversion << "\n";
                output.close();
                if (!output)
                    error = "Could not write coupling pressure file";
                else
                {
                    std::error_code ec;
                    std::filesystem::rename(temporary, config.pressure_path, ec);
                    if (ec)
                        error = "Could not publish coupling pressure file: " + ec.message();
                }
            }
        }
        comm.Broadcast(error, comm.GetBCProcRank());
        if (!error.empty())
            throw Exception() << error;
        comm.Broadcast(next, comm.GetBCProcRank());
        pendingSpeed = next;
        config.next_exchange += config.frequency;
    }
    LatticeVelocity GetVelocity(LatticePosition const &x, LatticeTimeStep step) const override
    {
        if (config.weights_path)
        {
            double speed = units->ConvertSpeedToLatticeUnits(config.max_speed_ms);
            auto warmup = config.warmup_steps.value_or(0);
            if (step <= warmup)
                speed *= double(step) / (warmup + 1);
            return GetNormal() * (speed * weights.At(x, GetNormal()));
        }
        auto displacement = x - GetPosition();
        double axial = Dot(displacement, GetNormal());
        double radial2 = std::max(0.0, displacement.GetMagnitudeSquared() - axial * axial);
        if (radial2 > radius * radius * (1 + 1e-10))
            throw Exception() << "readWrite velocity site lies outside configured radius";
        double speed = units->ConvertSpeedToLatticeUnits(config.max_speed_ms);
        auto warmup = config.warmup_steps.value_or(0);
        if (step <= warmup)
            speed *= double(step) / (warmup + 1);
        return GetNormal() * (speed * std::max(0.0, 1 - radial2 / (radius * radius)));
    }
    configuration::ReadWriteVelocityIoletConfig const &GetConfig() const { return config; }
};
} // namespace hemelb::lb
#endif
