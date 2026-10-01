// This file is part of HemeLB and is Copyright (C)
// the HemeLB team and/or their institutions, as detailed in the
// file AUTHORS. This software is provided under the terms of the
// license in the file LICENSE.

#include <algorithm>
#include <fstream>
#include <cmath>

#include "lb/iolets/InOutLetFile.h"
#include "log/Logger.h"
#include "configuration/SimConfig.h"

namespace hemelb::lb
{
      InOutLetFile::InOutLetFile() :
          InOutLet(), densityTable(0)
      {

      }

      InOutLet* InOutLetFile::clone() const
      {
        return new InOutLetFile(*this);
      }

        auto less_time = [](std::pair<LatticeTime, LatticeDensity> const& l, std::pair<LatticeTime, LatticeDensity>  const& r) {
            return l.first < r.first;
        };

      // This reads the file and converts to lattice units
        void InOutLetFile::Initialise(const util::UnitConverter* unitConverter)
        {
            if (!std::filesystem::exists(pressureFilePath))
                throw (Exception() << "File does not exist: " << pressureFilePath);

            // First read in values from file, keeping sorted by time and unique for time.
            std::ifstream datafile(pressureFilePath);
            log::Logger::Log<log::Debug, log::OnePerCore>("Reading iolet values from file: %s", pressureFilePath.c_str());


            file_data_lat.clear();
            double t_s, p_input;
            while (datafile >> std::ws && datafile.peek() != std::char_traits<char>::eof())
            {
                if (!(datafile >> t_s >> p_input))
                    throw (Exception() << "Invalid pressure profile record in " << pressureFilePath);
                const double p_Pa = p_input * pressureScale;
                if (!std::isfinite(t_s) || !std::isfinite(p_Pa))
                    throw (Exception() << "Non-finite pressure profile value in " << pressureFilePath);
                log::Logger::Log<log::Trace, log::OnePerCore>("Time: %f s. Value: %f Pa.", t_s, p_Pa);
                auto t_lat = unitConverter->ConvertTimeToLatticeUnits(t_s);
                auto rho_lat = unitConverter->ConvertPressureToLatticeUnits(p_Pa) / Cs2;
                if (file_data_lat.empty()) {
                    file_data_lat.emplace_back(t_lat, rho_lat);
                } else {
                    auto it = std::lower_bound(file_data_lat.begin(), file_data_lat.end(), DataPair{t_lat, rho_lat},
                                               less_time);
                    if (it == file_data_lat.end()) {
                        file_data_lat.emplace_back(t_lat, rho_lat);
                    } else if (it->first == t_lat) {
                        *it = {t_lat, rho_lat};
                    } else {
                        file_data_lat.insert(it, std::make_pair(t_lat, rho_lat));
                    }
                }
            }
            if (!datafile.eof())
                throw (Exception() << "Invalid pressure profile record in " << pressureFilePath);
            if (file_data_lat.size() < 2)
                throw (Exception() << "Pressure profile needs at least two distinct times in " << pressureFilePath);
            datafile.close();

            auto less_density = [](DataPair const& l, DataPair const& r) {
                return l.second < r.second;
            };
            densityMin = std::min_element(file_data_lat.begin(), file_data_lat.end(), less_density)->second;
            densityMax = std::max_element(file_data_lat.begin(), file_data_lat.end(), less_density)->second;

            // Check if last point's value matches the first
            if (file_data_lat.back().second != file_data_lat.front().second)
                throw (Exception() << "Last point's value does not match the first point's value in "
                                   << pressureFilePath);
        }

        // This interpolates between points to generate a cycle
        // IMPORTANT: to allow reading in data taken at irregular intervals the user
        // needs to make sure that the last point in the file coincides with the first
        // point of a new cycle for a continuous trace.
        void InOutLetFile::Reset(SimulationState &state)
        {
            auto totalTimeSteps = state.GetEndTimeStep();
            // If the time values in the input file end BEFORE the planned
            // end of the simulation, then loop the profile afterwards
            // (using %TimeStepsInInletPressureProfile).
            // int TimeStepsInInletPressureProfile = times.back() / timeStepLength;
            // throw Exception() << "Finding Time steps: " << times.back() << " " << timeStepLength << " " << TimeStepsInInletPressureProfile;

            // extend the table to one past the total time steps, so that
            // the table is valid in the end-state, where the zero indexed
            // time step is equal to the limit.
            densityTable.resize(totalTimeSteps + 1);
            if (totalTimeSteps == 0) {
                densityTable[0] = file_data_lat.front().second;
                return;
            }
            // Now convert these vectors into arrays using linear
            // interpolation

            auto const& t_0 = file_data_lat.front().first;
            auto const& t_1 = file_data_lat.back().first;

            // To speed up the search, track the iterators that bracket the current time

            // Let lower be the iterator to the last element less than or equal to x
            auto lower = file_data_lat.begin();
            // Let upper be the iterator to the first element greater than x
            auto upper = lower + 1;
            // These are always one apart
            for (unsigned int timeStep = 0; timeStep <= totalTimeSteps; timeStep++)
            {
                LatticeTime x = std::lerp(t_0, t_1, LatticeTime(timeStep) / LatticeTime(totalTimeSteps));
                if (x >= t_1) {
                    densityTable[timeStep] = file_data_lat.back().second;
                    continue;
                }
                // First element strictly greater than x
                upper = std::find_if(upper, file_data_lat.end(), [&](DataPair const& p) {
                    return p.first > x;
                });
                // Last element less than or equal to t_0
                lower = upper - 1;
                auto [x0, y0] = *lower;
                auto [x1, y1] = *upper;
                densityTable[timeStep] = std::lerp(y0, y1, (x - x0) / (x1 - x0));
            }
        }

    }
