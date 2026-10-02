// This file is part of HemeLB and is Copyright (C)
// the HemeLB team and/or their institutions, as detailed in the
// file AUTHORS. This software is provided under the terms of the
// license in the file LICENSE.
#ifndef HEMELB_LB_IOLETS_VELOCITYWEIGHTS_H
#define HEMELB_LB_IOLETS_VELOCITYWEIGHTS_H
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include "Exception.h"
#include "units.h"
namespace hemelb::lb
{
class VelocityWeights
{
    using Coordinate = std::array<site_t, 3>;
    std::map<Coordinate, double> values;
    double sum = 0;

  public:
    void Load(std::filesystem::path const &path)
    {
        std::ifstream input(path);
        if (!input)
            throw Exception() << "Cannot read velocity weights: " << path;
        values.clear();
        Coordinate x;
        double weight;
        while (input >> std::ws && input.peek() != std::char_traits<char>::eof())
        {
            if (!(input >> x[0] >> x[1] >> x[2] >> weight) || !std::isfinite(weight) || weight < 0)
                throw Exception() << "Invalid velocity weight record: " << path;
            if (!values.emplace(x, weight).second)
                throw Exception() << "Duplicate velocity weight coordinate: " << path;
        }
        sum = 0;
        for (auto const &[coordinate, value] : values)
            sum += value;
        if (!std::isfinite(sum) || sum <= 0)
            throw Exception() << "Velocity weights must have a positive finite sum: " << path;
    }
    double Sum() const { return sum; }
    double At(LatticePosition const &position, LatticeVelocity const &normal) const
    {
        Coordinate x;
        std::array<double, 3> residual, magnitude;
        std::array<int, 3> direction;
        for (int a = 0; a < 3; ++a)
        {
            double initial = normal[a] < 0 ? std::floor(position[a]) : std::ceil(position[a]);
            if (!std::isfinite(initial) || initial < std::numeric_limits<site_t>::min() ||
                initial >= double(std::numeric_limits<site_t>::max()))
                throw Exception() << "Velocity weight coordinate out of range";
            x[a] = site_t(initial);
            direction[a] = normal[a] < 0 ? -1 : 1;
            magnitude[a] = std::abs(normal[a]);
            residual[a] = -std::abs(initial - position[a]);
        }
        // Search the next three lattice points along the inward normal, as in
        // HemePure's weighted file profile, with zero components held fixed.
        for (int trial = 0; trial < 3; ++trial)
        {
            if (auto found = values.find(x); found != values.end())
                return found->second;
            double distance = std::numeric_limits<double>::infinity();
            int axis = -1;
            for (int a = 0; a < 3; ++a)
                if (magnitude[a] > 0 && (1 - residual[a]) / magnitude[a] <= distance)
                {
                    distance = (1 - residual[a]) / magnitude[a];
                    axis = a;
                }
            if (axis < 0)
                break;
            for (int a = 0; a < 3; ++a)
                residual[a] += magnitude[a] * distance;
            if ((direction[axis] > 0 && x[axis] == std::numeric_limits<site_t>::max()) ||
                (direction[axis] < 0 && x[axis] == std::numeric_limits<site_t>::min()))
                throw Exception() << "Velocity weight coordinate overflow";
            x[axis] += direction[axis];
            residual[axis] -= 1;
        }
        return 0;
    }
};
} // namespace hemelb::lb
#endif
