// This file is part of HemeLB and is Copyright (C)
// the HemeLB team and/or their institutions, as detailed in the
// file AUTHORS. This software is provided under the terms of the
// license in the file LICENSE.

#ifndef HEMELB_TRACERS_TRACERCONTROLLER_H
#define HEMELB_TRACERS_TRACERCONTROLLER_H
#include <cmath>
#include <fstream>
#include <iomanip>
#include "tracers/TracerConfig.h"
#include "geometry/Domain.h"
#include "lb/MacroscopicPropertyCache.h"
#include "net/IOCommunicator.h"
#include "util/UnitConverter.h"
namespace hemelb::tracers
{
inline double Kernel(double r)
{
    r = std::abs(r);
    if (r <= 1)
        return (3 - 2 * r + std::sqrt(1 + 4 * r - 4 * r * r)) / 8;
    if (r <= 2)
        return (5 - 2 * r - std::sqrt(-7 + 12 * r - 4 * r * r)) / 8;
    return 0;
}
inline double Uniform(std::uint64_t x)
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return double(x >> 11) * 0x1p-53;
}
inline void Emit(TracerConfig &c, LatticeTimeStep step)
{
    if (!c.emissionCount || step < c.nextEmission)
        return;
    if (c.particles.size() + c.emissionCount > 100000)
        throw Exception() << "Passive tracer limit is 100000 particles";
    for (unsigned i = 0; i < c.emissionCount; ++i)
    {
        if (c.nextId == UINT64_MAX)
            throw Exception() << "Tracer IDs exhausted";
        Particle p;
        p.id = c.nextId++;
        p.created = step;
        p.radius = c.particleRadius;
        double z = 2 * Uniform(c.seed + 2 * p.id) - 1,
               angle = 2 * PI * Uniform(c.seed + 2 * p.id + 1);
        double r = std::sqrt(std::max(0.0, 1 - z * z));
        p.position = c.sphereCentre +
                     LatticePosition{r * std::cos(angle), r * std::sin(angle), z} * c.sphereRadius;
        c.particles.push_back(p);
    }
    c.nextEmission = step + c.emissionInterval;
}
inline LatticeVelocity Lubrication(LatticeVelocity v, LatticePosition wallVector, double radius,
                                   double range)
{
    double distance = wallVector.GetMagnitude();
    if (distance == 0)
        return {};
    double gap = distance - radius;
    if (gap >= range)
        return v;
    auto normal = wallVector / distance;
    double attenuation = gap <= 0 ? 0 : 1 / (1 + radius * (1 / gap - 1 / range));
    return v - normal * (Dot(v, normal) * (1 - attenuation));
}
inline bool WithinGrid(LatticePosition const &x, geometry::Domain const &domain)
{
    auto dimensions = domain.GetBlockDimensions().template as<double>() * domain.GetBlockSize();
    for (int a = 0; a < 3; ++a)
        if (!std::isfinite(x[a]) || x[a] < 0 || x[a] >= dimensions[a])
            return false;
    return true;
}
inline void Advance(TracerConfig &c, geometry::Domain const &domain,
                    lb::MacroscopicPropertyCache const &cache, net::IOCommunicator const &comm,
                    LatticeTimeStep step)
{
    Emit(c, step);
    if (c.particles.size() > 100000)
        throw Exception() << "Passive tracer limit is 100000 particles";
    for (auto &p : c.particles)
        if (!WithinGrid(p.position, domain))
            p.active = false;
    std::vector<double> samples(3 * c.particles.size());
    for (std::size_t i = 0; i < c.particles.size(); ++i)
    {
        auto const &p = c.particles[i];
        if (!p.active)
            continue;
        auto base =
            LatticeVector{site_t(std::floor(p.position.x())), site_t(std::floor(p.position.y())),
                          site_t(std::floor(p.position.z()))};
        for (int x = -1; x <= 2; ++x)
            for (int y = -1; y <= 2; ++y)
                for (int z = -1; z <= 2; ++z)
                {
                    auto grid = base + LatticeVector{x, y, z};
                    if (!domain.IsValidLatticeSite(grid) ||
                        domain.GetProcIdFromGlobalCoords(grid) != comm.Rank())
                        continue;
                    double weight = 1;
                    for (int a = 0; a < 3; ++a)
                        weight *= Kernel(double(grid[a]) - p.position[a]);
                    auto v = cache.velocityCache.Get(domain.GetContiguousSiteId(grid)) * weight;
                    for (int a = 0; a < 3; ++a)
                        samples[3 * i + a] += v[a];
                }
    }
    if (!samples.empty())
        comm.AllReduceInPlace(std::span<double>(samples), MPI_SUM);
    // Only the owner of the nearest site applies boundary rules, then shares
    // the corrected velocity and active flag in one collective.
    std::vector<double> corrected(4 * c.particles.size());
    for (std::size_t i = 0; i < c.particles.size(); ++i)
    {
        auto &p = c.particles[i];
        if (!p.active)
            continue;
        p.velocity = {samples[3 * i], samples[3 * i + 1], samples[3 * i + 2]};
        auto nearest =
            LatticeVector{site_t(std::round(p.position.x())), site_t(std::round(p.position.y())),
                          site_t(std::round(p.position.z()))};
        if (!domain.IsValidLatticeSite(nearest) ||
            domain.GetProcIdFromGlobalCoords(nearest) != comm.Rank())
            continue;
        auto site = domain.GetSite(domain.GetContiguousSiteId(nearest));
        bool active = true;
        for (auto const &rule : c.boundaries)
        {
            if (rule.kind == "spherical")
            {
                if (rule.radius > 0 && (p.position - rule.centre).GetMagnitude() >= rule.radius)
                    active = false;
                continue;
            }
            bool match = rule.appliesTo == "Wall"   ? site.IsWall()
                         : rule.appliesTo == "Ilet" ? site.GetSiteType() == geometry::INLET_TYPE
                                                    : site.GetSiteType() == geometry::OUTLET_TYPE;
            if (!match)
                continue;
            auto const &lattice = domain.GetLatticeInfo();
            for (Direction d = 1; d < lattice.GetNumVectors(); ++d)
            {
                auto vector = lattice.GetVector(d).template as<double>();
                if (vector.GetMagnitudeSquared() != 1)
                    continue;
                bool link = rule.appliesTo == "Wall" ? site.HasWall(d) : site.HasIolet(d);
                if (!link)
                    continue;
                double cut = std::min(site.GetWallDistances()[d - 1], 0.5);
                auto toWall = nearest.template as<double>() - p.position + vector * cut;
                if (rule.kind == "lubrication")
                    p.velocity = Lubrication(p.velocity, toWall, p.radius, rule.range);
                else if (toWall.GetMagnitude() <= rule.range)
                    active = false;
            }
        }
        for (int a = 0; a < 3; ++a)
            corrected[4 * i + a] = p.velocity[a];
        corrected[4 * i + 3] = active;
    }
    if (!corrected.empty())
        comm.AllReduceInPlace(std::span<double>(corrected), MPI_SUM);
    for (std::size_t i = 0; i < c.particles.size(); ++i)
    {
        auto &p = c.particles[i];
        if (!p.active)
            continue;
        p.velocity = {corrected[4 * i], corrected[4 * i + 1], corrected[4 * i + 2]};
        p.active = corrected[4 * i + 3] != 0;
        if (p.active && step > p.created + 2)
            p.position += p.velocity;
        if (!WithinGrid(p.position, domain))
            p.active = false;
        else
        {
            LatticeVector nearest{site_t(std::round(p.position.x())),
                                  site_t(std::round(p.position.y())),
                                  site_t(std::round(p.position.z()))};
            if (!domain.IsValidLatticeSite(nearest) ||
                domain.GetProcIdFromGlobalCoords(nearest) == SITE_OR_BLOCK_SOLID)
                p.active = false;
        }
    }
}
inline void Write(TracerConfig const &c, std::filesystem::path const &directory,
                  util::UnitConverter const &units, LatticeTimeStep step)
{
    if (step % c.outputPeriod)
        return;
    std::ofstream out(directory / "tracers.csv", std::ios::app);
    if (!out)
        throw Exception() << "Cannot write tracers.csv";
    if (out.tellp() == 0)
        out << "step,time_s,id,active,x_m,y_m,z_m,vx_ms,vy_ms,vz_ms\n";
    out << std::setprecision(17);
    for (auto const &p : c.particles)
    {
        auto x = units.ConvertPositionToPhysicalUnits(p.position);
        auto v = p.velocity * units.ConvertSpeedToPhysicalUnits(1);
        out << step << ',' << step * units.GetTimeStep() << ',' << p.id << ',' << p.active;
        for (int a = 0; a < 3; ++a)
            out << ',' << x[a];
        for (int a = 0; a < 3; ++a)
            out << ',' << v[a];
        out << '\n';
    }
    if (!out)
        throw Exception() << "Failed writing tracers.csv";
}
} // namespace hemelb::tracers
#endif
