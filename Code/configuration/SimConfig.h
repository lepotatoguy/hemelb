// This file is part of HemeLB and is Copyright (C)
// the HemeLB team and/or their institutions, as detailed in the
// file AUTHORS. This software is provided under the terms of the
// license in the file LICENSE.

#ifndef HEMELB_CONFIGURATION_SIMCONFIG_H
#define HEMELB_CONFIGURATION_SIMCONFIG_H

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "constants.h"
#include "tracers/TracerConfig.h"
#include "quantity.h"
#include "units.h"
#include "configuration/MonitoringConfig.h"
#include "lb/LbmParameters.h"
#include "util/Vector3D.h"
#include "extraction/PropertyOutputFile.h"

namespace hemelb
{
    class SimulationController;
}

namespace hemelb::io {
    class Checkpointer;
    namespace xml {
        class Document;
    }
}

namespace hemelb::configuration
{
    // Base for initial conditions configuration
    struct ICConfigBase {
      ICConfigBase(std::optional<LatticeTimeStep> t);
      std::optional<LatticeTimeStep> t0;
    };

    // Uniform equilibrium IC
    struct EquilibriumIC : ICConfigBase {
      EquilibriumIC(std::optional<LatticeTimeStep> t, PhysicalPressure p);
      EquilibriumIC(std::optional<LatticeTimeStep> t, PhysicalPressure p, const PhysicalVelocity& v);
      PhysicalPressure p_Pa;
      PhysicalVelocity v_ms;
    };

    // Read from checkpoint IC
    struct CheckpointIC : ICConfigBase {
      CheckpointIC(std::optional<LatticeTimeStep> t, std::filesystem::path cp, std::optional<std::filesystem::path> maybeOff);
      std::filesystem::path cpFile;
      std::optional<std::filesystem::path> maybeOffFile;
    };

    // Variant including null state
    using ICConfig = std::variant<std::monostate, EquilibriumIC, CheckpointIC>;

    struct TimeInfo {
        std::uint64_t total_steps = 0;
        std::uint64_t warmup_steps = 0;
        PhysicalTime step_s = 0.0;
    };

    struct SpaceInfo {
        PhysicalDistance step_m = 0;
        PhysicalPosition geometry_origin_m = {0.0, 0.0, 0.0};
    };

    struct FluidInfo {
        PhysicalDensity density_kgm3 = DEFAULT_FLUID_DENSITY_Kg_per_m3;
        PhysicalDynamicViscosity viscosity_Pas = DEFAULT_FLUID_VISCOSITY_Pas;
        PhysicalPressure reference_pressure_Pa = 0.0;
    };

    struct CheckpointInfo {
        LatticeTimeStep period;
    };

    struct SpongeInfo
    {
        double viscosity_ratio = 1;
        PhysicalDistance width_m = 0;
        LatticeTimeStep lifetime = 0;
    };

    struct GlobalSimInfo {
        TimeInfo time;
        SpaceInfo space;
        FluidInfo fluid;
        std::optional<CheckpointInfo> checkpoint;
        std::optional<SpongeInfo> sponge;
        double smagorinsky = 0.1;
        double elastic_wall_stiffness = 0;
        double boundary_velocity_ratio = 0;
    };

    struct FlowExtensionConfig {
        PhysicalDistance length_m;
        PhysicalDistance radius_m;
        PhysicalDistance fadelength_m;
        util::Vector3D<double> normal;
        PhysicalPosition origin_m;
    };

    // Using std::minstd_rand with a 32 bit unsigned seed/state.
    using PrngSeedType = std::uint32_t;

    struct CellInserterConfig {
        PrngSeedType seed;
        std::string template_name;
        quantity_union<double, "s", "lattice"> offset;
        Angle theta_rad;
        Angle phi_rad;
        PhysicalDisplacement translation_m;
        PhysicalTime drop_period_s;
        PhysicalTime dt_s;
        Angle dtheta_rad;
        Angle dphi_rad;
        PhysicalDistance dx_m;
        PhysicalDistance dy_m;
    };

    struct IoletConfigBase {
        PhysicalPosition position;
        util::Vector3D<double> normal;
        std::optional<std::uint64_t> warmup_steps;
        std::optional<FlowExtensionConfig> flow_extension;
        std::vector<CellInserterConfig> cell_inserters;
    };

    // Consider removing
    struct PressureIoletConfig : IoletConfigBase {
    };

    struct CosinePressureIoletConfig : PressureIoletConfig {
        // Note pressure difference
        PhysicalPressure amp_Pa;
        // Note absolute pressure
        PhysicalPressure mean_Pa;
        Angle phase_rad;
        PhysicalTime period_s;
    };

    struct FilePressureIoletConfig : PressureIoletConfig {
        std::filesystem::path file_path;
        bool file_mmHg = false;
        bool periodic = false;
    };

    struct WindkesselPressureIoletConfig : PressureIoletConfig
    {
        std::string model = "WK2";
        double characteristic_resistance = 0;
        double peripheral_resistance = 0;
        double capacitance = 0;
        PhysicalDistance radius_m = 0;
        double area_m2 = 0;
        std::optional<std::filesystem::path> weights_path;
        PhysicalPressure pressure_Pa = 0;
        double flow_m3s = 0;
        double previous_flow_m3s = 0;
    };

    struct MultiscalePressureIoletConfig : PressureIoletConfig {
        PhysicalPressure pressure_reference_Pa;
        PhysicalVelocity velocity_reference_ms;
        std::string label;
    };

    struct VelocityIoletConfig : IoletConfigBase {
    };

    struct ParabolicVelocityIoletConfig : VelocityIoletConfig {
        PhysicalDistance radius_m;
        PhysicalSpeed max_speed_ms;
    };

    struct WomersleyVelocityIoletConfig : VelocityIoletConfig {
        PhysicalDistance radius_m;
        PhysicalPressureGradient pgrad_amp_Pam;
        PhysicalTime period_s;
        double womersley;
    };

    struct ElasticWomersleyVelocityIoletConfig : WomersleyVelocityIoletConfig
    {
        double poisson_ratio = 0;
        PhysicalPressure youngs_modulus_Pa = 0;
        PhysicalDistance axial_position_m = 0;
    };

    struct ReadWriteVelocityIoletConfig : VelocityIoletConfig
    {
        PhysicalDistance radius_m = 0;
        double area_m2 = 0;
        LatticeTimeStep frequency = 1;
        std::filesystem::path flow_path, pressure_path;
        std::optional<std::filesystem::path> weights_path;
        std::optional<PhysicalTime> start_time_s;
        double flow_conversion = 1, pressure_conversion = 1, smoothing = 1;
        bool pressure_mmHg = false;
        PhysicalTime timeout_s = 60;
        PhysicalSpeed max_speed_ms = 0;
        LatticeTimeStep next_exchange = 2;
        double average_density = 1;
    };

    struct FileVelocityIoletConfig : VelocityIoletConfig {
        std::filesystem::path file_path;
        bool periodic = false;
        PhysicalDistance radius_m;
    };

    using IoletConfig =
        std::variant<std::monostate, CosinePressureIoletConfig, FilePressureIoletConfig,
                     MultiscalePressureIoletConfig, WindkesselPressureIoletConfig,
                     ParabolicVelocityIoletConfig, WomersleyVelocityIoletConfig,
                     ElasticWomersleyVelocityIoletConfig, ReadWriteVelocityIoletConfig,
                     FileVelocityIoletConfig>;

    struct VTKMeshFormat {};
    struct KruegerMeshFormat {};
    using MeshFormat = std::variant<std::monostate, VTKMeshFormat, KruegerMeshFormat>;

    struct CellModuli {
        //! Bending energy parameter
        PhysicalEnergy bending_Nm;
        //! Surface energy parameter
        LatticeModulus surface_lat;
        //! Volume energy parameter
        LatticeModulus volume_lat;
        //! Skalak dilation modulus
        LatticeModulus dilation_lat;
        //! Skalak strain modulus
        PhysicalModulus strain_Npm;
    };

    struct TemplateCellConfig {
        std::string name;
        std::filesystem::path mesh_path;
        MeshFormat format;
        PhysicalDistance scale_m;
        std::optional<std::filesystem::path> reference_mesh_path;
        MeshFormat reference_mesh_format;
        CellModuli moduli;
    };

    struct NodeForceConfig {
        quantity_union<double, "Nm", "lattice"> intensity;
        LatticeDistance cutoffdist;
        std::size_t exponent;
    };

    struct CellOutputConfig {
        LatticeTimeStep output_period;
        bool physical_units = false;
    };

    struct RBCConfig {
        LatticeDistance boxSize;
        std::map<std::string, TemplateCellConfig> meshes;
        NodeForceConfig cell2cell;
        NodeForceConfig cell2wall;
        std::optional<CellOutputConfig> full_output;
        std::optional<CellOutputConfig> summary_output;
    };

    class SimConfig
    {
        friend class SimBuilder;
        friend class SimConfigReader;
friend class io::Checkpointer;
friend class hemelb::SimulationController;

public:
using path = std::filesystem::path;
static SimConfig New(const path &p);
std::string const &GetDecompositionMethod() const { return decompositionMethod; }
unsigned GetGeometryReaderCount() const { return geometryReaderCount; }
unsigned GetGeometryReaderSpacing() const { return geometryReaderSpacing; }

inline GlobalSimInfo const &GetSimInfo() const { return sim_info; }

const std::vector<IoletConfig> &GetInlets() const { return inlets; }
const std::vector<IoletConfig> &GetOutlets() const { return outlets; }
const path &GetDataFilePath() const { return dataFilePath; }
LatticeTimeStep GetTotalTimeSteps() const { return sim_info.time.total_steps; }
LatticeTimeStep GetWarmUpSteps() const { return sim_info.time.warmup_steps; }
PhysicalTime GetTimeStepLength() const { return sim_info.time.step_s; }
PhysicalDistance GetVoxelSize() const { return sim_info.space.step_m; }
PhysicalPosition GetGeometryOrigin() const { return sim_info.space.geometry_origin_m; }
unsigned int PropertyOutputCount() const { return propertyOutputs.size(); }
extraction::PropertyOutputFile &GetPropertyOutput(unsigned int index)
{
    return propertyOutputs[index];
}
        std::vector<extraction::PropertyOutputFile> const& GetPropertyOutputs() const
        {
          return propertyOutputs;
        }

        inline path const& GetColloidConfigPath() const
        {
          return colloid_xml_path.value();
        }

        inline bool HasColloidSection() const {
            return colloid_xml_path.has_value();
        }

        auto const &GetTracers() const { return tracers; }

        // Get the initial condtion config
        inline const ICConfig& GetInitialCondition() const {
	  return initial_condition;
	}

        /**
         * Return the configuration of various checks/test
         * @return monitoring configuration
         */
        const MonitoringConfig& GetMonitoringConfiguration() const;

        /**
         * True if the XML file has a section specifying red blood cells.
         * @return
         */
        inline bool HasRBCSection() const
        {
          return rbcConf.has_value();
        }

        inline const RBCConfig& GetRBCConfig() const {
            return *rbcConf;
        }

    protected:
        std::shared_ptr<io::xml::Document> source_xml;
        path dataFilePath;

        std::vector<extraction::PropertyOutputFile> propertyOutputs;

        // Path of the colloid XML if colloids section present
        std::optional<path> colloid_xml_path;

        MonitoringConfig monitoringConfig; ///< Configuration of various checks/tests

        std::optional<RBCConfig> rbcConf;
        std::optional<tracers::TracerConfig> tracers;

        std::string decompositionMethod = "parmetis";
        unsigned geometryReaderCount = 0, geometryReaderSpacing = 1;
        GlobalSimInfo sim_info;
        ICConfig initial_condition;
        std::vector<IoletConfig> inlets;
        std::vector<IoletConfig> outlets;
    };


}

#endif /* HEMELB_CONFIGURATION_SIMCONFIG_H */
