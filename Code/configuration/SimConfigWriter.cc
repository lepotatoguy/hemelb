// This file is part of HemeLB and is Copyright (C)
// the HemeLB team and/or their institutions, as detailed in the
// file AUTHORS. This software is provided under the terms of the
// license in the file LICENSE.

#include "configuration/SimConfigWriter.h"

#include <optional>
#include <sstream>
#include <type_traits>
#include <utility>
#include <variant>

#include "constants.h"
#include "Exception.h"
#include "hassert.h"
#include "configuration/MonitoringConfig.h"
#include "extraction/GeometrySelectors.h"
#include "io/xml.h"
#include "util/variant.h"

namespace hemelb::configuration {
    using namespace io::xml;

    namespace {
        template<typename T>
        void SetDimensionalValue(Element el, char const *unit, T const &value) {
            el.SetAttribute("units", unit);
            el.SetAttribute("value", value);
        }

        template<typename T>
        void AddChildDimensionalValue(Element parent, char const *name, char const *unit, T const &value) {
            auto el = parent.AddChild(name);
            SetDimensionalValue(el, unit, value);
        }

        template<typename CStr, typename... CStrs>
        std::optional<Element> GetElemMaybe(Element el, CStr nextPath, CStrs... restPath) {
            auto nextEl = el.GetChildOrNull(nextPath);
            if (nextEl == Element::Missing())
                return std::nullopt;
            if constexpr (sizeof...(CStrs) == 0) {
                return nextEl;
            } else {
                return GetElemMaybe(nextEl, restPath...);
            }
        }

        template<typename... CStr>
        std::optional<Element> GetElemMaybe(std::shared_ptr<const Document> const &doc, CStr... path) {
            return GetElemMaybe(doc->GetRoot(), path...);
        }
    }

//    template <typename... CStrs>
//    bool SimConfigWriter::InOriginal(CStrs... path) const {
//        return GetElemMaybe(originalXml->GetRoot(), path...).has_value();
//    }

    namespace {
        // Make a Document that ensures floats are written with full precision.
        auto MakeXmlDoc() {
            return std::make_unique<Document>([]() {
                std::ostringstream s;
                s << std::hexfloat;
                return s;
            });
        }
    }

    SimConfigWriter::SimConfigWriter(path p)
            : outputXmlPath(std::move(p)), outputXml(MakeXmlDoc())
    {
    }

    void SimConfigWriter::AddPathChild(Element& parent,
                                       const path& p) const {
        auto path = parent.AddChild("path");
        path.SetAttribute("value", FullPathToRelPath(p));
    }

    std::string SimConfigWriter::FullPathToRelPath(const SimConfigWriter::path& p) const {
        auto out_dir = std::filesystem::weakly_canonical(
            std::filesystem::absolute(outputXmlPath).parent_path());
        auto target = std::filesystem::weakly_canonical(std::filesystem::absolute(p));
        return target.lexically_relative(out_dir);
    }


    void SimConfigWriter::Write(SimConfig const& conf) {
        auto root = outputXml->AddChild("hemelbsettings");
        root.SetAttribute("version", 6U);
        auto decomp = root.AddChild("decomposition");
        decomp.SetAttribute("method", conf.GetDecompositionMethod());
        decomp.SetAttribute("reader_count", conf.GetGeometryReaderCount());
        decomp.SetAttribute("reader_spacing", conf.GetGeometryReaderSpacing());
        DoIOForSimulation(conf.GetSimInfo());
        DoIOForGeometry(conf.GetDataFilePath());
        DoIOForInitialConditions(conf.GetInitialCondition());
        auto const &info = conf.GetSimInfo();
        if (info.sponge)
        {
            auto sp = root.GetChildOrThrow("initialconditions").AddChild("sponge_layer");
            AddChildDimensionalValue(sp, "viscosity_ratio", "dimensionless",
                                     info.sponge->viscosity_ratio);
            AddChildDimensionalValue(sp, "width", "m", info.sponge->width_m);
            AddChildDimensionalValue(sp, "lifetime", "lattice", info.sponge->lifetime);
        }
        DoIOForInOutlets("inlet", conf.GetInlets());
        DoIOForInOutlets("outlet", conf.GetOutlets());
        DoIOForProperties(conf.GetPropertyOutputs());
        DoIOForMonitoring(conf.GetMonitoringConfiguration());
        if (conf.GetTracers())
        {
            auto const &c = *conf.GetTracers();
            auto el = root.AddChild("tracers");
            el.SetAttribute("output_period", c.outputPeriod);
            el.SetAttribute("seed", c.seed);
            el.SetAttribute("next_id", c.nextId);
            el.SetAttribute("next_emission", c.nextEmission);
            el.SetAttribute("particle_radius", c.particleRadius);
            if (!c.boundaries.empty())
            {
                auto rules = el.AddChild("boundaryConditions");
                for (auto const &rule : c.boundaries)
                {
                    auto node = rules.AddChild(rule.kind.c_str());
                    node.SetAttribute("appliesTo", rule.appliesTo);
                    if (rule.kind == "spherical")
                    {
                        AddChildDimensionalValue(node, "sphereRadius", "lattice", rule.radius);
                        auto centre = node.AddChild("sphereCentre");
                        centre.SetAttribute("units", "lattice");
                        centre.SetAttribute("x", rule.centre.x());
                        centre.SetAttribute("y", rule.centre.y());
                        centre.SetAttribute("z", rule.centre.z());
                    }
                    else
                        node.SetAttribute("effectiveRange", rule.range);
                }
            }
            auto particles = el.AddChild("particles");
            auto vector = [&](Element parent, char const *tag, LatticePosition const &x)
            {
                auto node = parent.AddChild(tag);
                node.SetAttribute("units", "lattice");
                node.SetAttribute("x", x.x());
                node.SetAttribute("y", x.y());
                node.SetAttribute("z", x.z());
            };
            for (auto const &p : c.particles)
            {
                auto node = particles.AddChild("subgridParticle");
                node.SetAttribute("units", "lattice");
                node.SetAttribute("ParticleId", p.id);
                node.SetAttribute("Radius", p.radius);
                node.SetAttribute("created", p.created);
                node.SetAttribute("active", unsigned(p.active));
                vector(node, "initialPosition", p.position);
                vector(node, "velocity", p.velocity);
            }
            vector(particles, "sphereCentre", c.sphereCentre);
            AddChildDimensionalValue(particles, "sphereRadius", "lattice", c.sphereRadius);
            AddChildDimensionalValue(particles, "emissionCount", "dimensionless", c.emissionCount);
            AddChildDimensionalValue(particles, "emissionItrvl", "dimensionless",
                                     c.emissionInterval);
        }
        // DoIOForRBCs

        outputXml->SaveFile(outputXmlPath);
        // Reset to a new empty doc.
        outputXml = MakeXmlDoc();
    }

    void SimConfigWriter::DoIOForSimulation(const GlobalSimInfo& sim_info) {
        Element outSimEl = outputXml->GetRoot().AddChild("simulation");

        AddChildDimensionalValue(outSimEl, "step_length", "s", sim_info.time.step_s);

        // Required element
        // <steps value="unsigned" units="lattice />
        AddChildDimensionalValue(outSimEl, "steps", "lattice", sim_info.time.total_steps);

        // Optional element
        // <extra_warmup_steps value="unsigned" units="lattice" />
        if (auto warmup = sim_info.time.warmup_steps; warmup > 0)
            AddChildDimensionalValue(outSimEl, "extra_warmup_steps", "lattice", warmup);

        // Required element
        // <voxel_size value="float" units="m" />
        AddChildDimensionalValue(outSimEl, "voxel_size", "m", sim_info.space.step_m);
        // Required element
        // <origin value="(x,y,z)" units="m" />
        AddChildDimensionalValue(outSimEl, "origin", "m", sim_info.space.geometry_origin_m);

        // Optional element
        // <fluid_density value="float" units="kg/m3" />
        if (auto density = sim_info.fluid.density_kgm3; density != DEFAULT_FLUID_DENSITY_Kg_per_m3)
            AddChildDimensionalValue(outSimEl, "fluid_density", "kg/m3", density);
        // Optional element
        // <fluid_viscosity value="float" units="Pa.s" />
        if (auto visc = sim_info.fluid.viscosity_Pas; visc != DEFAULT_FLUID_VISCOSITY_Pas)
            AddChildDimensionalValue(outSimEl, "fluid_viscosity", "Pa.s", visc);

        // Optional element
        // <reference_pressure value="float" units="mmHg" />
        if (auto ref_p = sim_info.fluid.reference_pressure_Pa; ref_p != 0.0)
            AddChildDimensionalValue(outSimEl, "reference_pressure", "mmHg", ref_p / mmHg_TO_PASCAL);

        if (sim_info.smagorinsky != 0.1)
            AddChildDimensionalValue(outSimEl, "smagorinsky_constant", "dimensionless",
                                     sim_info.smagorinsky);
        if (sim_info.elastic_wall_stiffness > 0)
            AddChildDimensionalValue(outSimEl, "elastic_wall_stiffness", "lattice",
                                     sim_info.elastic_wall_stiffness);
        if (sim_info.boundary_velocity_ratio > 0)
            AddChildDimensionalValue(outSimEl, "boundary_velocity_ratio", "lattice",
                                     sim_info.boundary_velocity_ratio);
        if (sim_info.checkpoint.has_value()) {
            auto cpEl = outSimEl.AddChild("checkpoint");
            cpEl.SetAttribute("period", sim_info.checkpoint->period);
        }
    }

    void SimConfigWriter::DoIOForGeometry(const path& p) {
        auto rel_path = FullPathToRelPath(p);
        auto df_el = outputXml->GetRoot().AddChild("geometry").AddChild("datafile");
        df_el.SetAttribute("path", rel_path);
    }

    void SimConfigWriter::DoIOForBaseInOutlet(Element outEl, IoletConfigBase const& ioletConf) const {
        AddChildDimensionalValue(outEl, "position", "m", ioletConf.position);
        AddChildDimensionalValue(outEl, "normal", "dimensionless", ioletConf.normal);

        if (ioletConf.flow_extension.has_value()) {
            auto& fe = ioletConf.flow_extension.value();
            auto flowEl = outEl.AddChild("flowextension");
            AddChildDimensionalValue(flowEl, "length", "m", fe.length_m);
            AddChildDimensionalValue(flowEl, "radius", "m", fe.radius_m);
        }
    }

    void SimConfigWriter::DoIOForInOutlets(std::string type, const std::vector<IoletConfig>& iolets) {
        auto plural = type + "s";

        auto dst_plural = outputXml->GetRoot().AddChild(plural.c_str());

        for (auto& iolet_conf: iolets) {
            auto dst_iolet_el = dst_plural.AddChild(type.c_str());
            std::visit([&](auto const& _) {
                if constexpr(std::is_same_v<std::decay_t<decltype(_)>, std::monostate>) {
                    throw (Exception() << "invalid iolet config");
                } else {
                    DoIOForBaseInOutlet(dst_iolet_el, _);
                }
            }, iolet_conf);
            overload_visit(
                iolet_conf,
                [](std::monostate const &) { throw(Exception() << "Invalid iolet config"); },
                [&](CosinePressureIoletConfig const &_)
                { DoIOForCosinePressureInOutlet(dst_iolet_el, _); },
                [&](FilePressureIoletConfig const &_)
                { DoIOForFilePressureInOutlet(dst_iolet_el, _); },
                [&](MultiscalePressureIoletConfig const &_)
                { DoIOForMultiscalePressureInOutlet(dst_iolet_el, _); },
                [&](WindkesselPressureIoletConfig const &_)
                { DoIOForWindkesselPressureInOutlet(dst_iolet_el, _); },
                [&](ParabolicVelocityIoletConfig const &_)
                { DoIOForParabolicVelocityInOutlet(dst_iolet_el, _); },
                [&](WomersleyVelocityIoletConfig const &_)
                { DoIOForWomersleyVelocityInOutlet(dst_iolet_el, _); },
                [&](ElasticWomersleyVelocityIoletConfig const &c)
                {
                    auto condition = dst_iolet_el.AddChild("condition");
                    condition.SetAttribute("type", "velocity");
                    condition.SetAttribute("subtype", "womersleyElastic");
                    AddChildDimensionalValue(condition, "radius", "m", c.radius_m);
                    AddChildDimensionalValue(condition, "pressure_gradient_amplitude", "mmHg/m",
                                             c.pgrad_amp_Pam / mmHg_TO_PASCAL);
                    AddChildDimensionalValue(condition, "period", "s", c.period_s);
                    AddChildDimensionalValue(condition, "womersley_number", "dimensionless",
                                             c.womersley);
                    AddChildDimensionalValue(condition, "poisson_ratio", "dimensionless",
                                             c.poisson_ratio);
                    AddChildDimensionalValue(condition, "youngs_modulus", "Pa",
                                             c.youngs_modulus_Pa);
                    AddChildDimensionalValue(condition, "axial_position", "m", c.axial_position_m);
                },
                [&](ReadWriteVelocityIoletConfig const &c)
                {
                    auto el = dst_iolet_el.AddChild("condition");
                    el.SetAttribute("type", "velocity");
                    el.SetAttribute("subtype", "readWrite");
                    el.SetAttribute("pressure_units", c.pressure_mmHg ? "mmHg" : "Pa");
                    el.SetAttribute("timeout_s", c.timeout_s);
                    AddChildDimensionalValue(el, "radius", "m", c.radius_m);
                    AddChildDimensionalValue(el, "area", "m^2", c.area_m2);
                    AddChildDimensionalValue(el, "frequency", "lattice", c.frequency);
                    auto path = el.AddChild("flowRateFilePath");
                    path.SetAttribute("value", FullPathToRelPath(c.flow_path));
                    path = el.AddChild("pressureFilePath");
                    path.SetAttribute("value", FullPathToRelPath(c.pressure_path));
                    if (c.weights_path)
                    {
                        auto weights = el.AddChild("weightsFilePath");
                        weights.SetAttribute("value", FullPathToRelPath(*c.weights_path));
                    }
                    AddChildDimensionalValue(el, "flowRateConversionFactor", "dimensionless",
                                             c.flow_conversion);
                    AddChildDimensionalValue(el, "pressureConversionFactor", "dimensionless",
                                             c.pressure_conversion);
                    AddChildDimensionalValue(el, "smoothingFactor", "dimensionless", c.smoothing);
                    auto state = el.AddChild("state");
                    state.SetAttribute("max_speed_ms", c.max_speed_ms);
                    state.SetAttribute("next_exchange", c.next_exchange);
                    state.SetAttribute("average_density", c.average_density);
                    if (c.start_time_s)
                        state.SetAttribute("start_time_s", *c.start_time_s);
                },
                [&](FileVelocityIoletConfig const &_)
                { DoIOForFileVelocityInOutlet(dst_iolet_el, _); });
        }
    }
    Element MakeCondition(Element& iolet, char const* type, char const* subtype) {
        auto condition = iolet.AddChild("condition");
        condition.SetAttribute("type", type);
        condition.SetAttribute("subtype", subtype);
        return condition;
    }

    void
    SimConfigWriter::DoIOForWindkesselPressureInOutlet(Element &dest,
                                                       WindkesselPressureIoletConfig const &c) const
    {
        auto condition = MakeCondition(dest, "pressure", c.model.c_str());
        if (c.model == "WK3")
        {
            AddChildDimensionalValue(condition, "Rc", "kg/m^4*s", c.characteristic_resistance);
            AddChildDimensionalValue(condition, "Rp", "kg/m^4*s", c.peripheral_resistance);
            AddChildDimensionalValue(condition, "Cp", "m^4*s^2/kg", c.capacitance);
        }
        else
        {
            AddChildDimensionalValue(condition, "R", "kg/m^4*s", c.peripheral_resistance);
            AddChildDimensionalValue(condition, "C", "m^4*s^2/kg", c.capacitance);
        }
        AddChildDimensionalValue(condition, "area", "m^2", c.area_m2);
        if (c.weights_path)
            AddPathChild(condition, *c.weights_path);
        else
            AddChildDimensionalValue(condition, "radius", "m", c.radius_m);
        auto state = condition.AddChild("state");
        state.SetAttribute("pressure_mmHg", c.pressure_Pa / mmHg_TO_PASCAL);
        state.SetAttribute("flow_m3s", c.flow_m3s);
        state.SetAttribute("previous_flow_m3s", c.previous_flow_m3s);
    }

    void SimConfigWriter::DoIOForCosinePressureInOutlet(SimConfigWriter::Element& dest,
                                                        CosinePressureIoletConfig const& conf) const {
        auto condition = MakeCondition(dest, "pressure", "cosine");
        AddChildDimensionalValue(condition, "amplitude", "mmHg", conf.amp_Pa / mmHg_TO_PASCAL);
        AddChildDimensionalValue(condition, "mean", "mmHg", conf.mean_Pa / mmHg_TO_PASCAL);
        AddChildDimensionalValue(condition, "phase", "rad", conf.phase_rad);
        AddChildDimensionalValue(condition, "period", "s", conf.period_s);
    }

    void SimConfigWriter::DoIOForFilePressureInOutlet(SimConfigWriter::Element& dest,
                                                      FilePressureIoletConfig const& conf) const {
        auto condition = MakeCondition(dest, "pressure", "file");
        condition.SetAttribute("units", conf.file_mmHg ? "mmHg" : "Pa");
        condition.SetAttribute("timing", conf.periodic ? "periodic" : "stretch");
        AddPathChild(condition, conf.file_path);
    }

    void SimConfigWriter::DoIOForMultiscalePressureInOutlet(SimConfigWriter::Element& dest,
                                                            MultiscalePressureIoletConfig const& conf) const {
        auto condition = MakeCondition(dest, "pressure", "multiscale");
        AddChildDimensionalValue(condition, "pressure", "mmHg", conf.pressure_reference_Pa / mmHg_TO_PASCAL);
        AddChildDimensionalValue(condition, "velocity", "m/s", conf.velocity_reference_ms);
        auto label = condition.AddChild("label");
        label.SetAttribute("value", conf.label);
    }

    void SimConfigWriter::DoIOForParabolicVelocityInOutlet(SimConfigWriter::Element& dest,
                                                           ParabolicVelocityIoletConfig const& conf) const {
        auto condition = MakeCondition(dest, "velocity", "parabolic");
        AddChildDimensionalValue(dest, "radius", "m", conf.radius_m);
        AddChildDimensionalValue(dest, "maximum", "m/s", conf.max_speed_ms);
    }

    void SimConfigWriter::DoIOForWomersleyVelocityInOutlet(SimConfigWriter::Element& dest,
                                                           WomersleyVelocityIoletConfig const& conf) const {
        auto condition = MakeCondition(dest, "velocity", "womersley");
        AddChildDimensionalValue(condition, "radius", "m", conf.radius_m);
        AddChildDimensionalValue(condition, "pressure_gradient_amplitude", "mmHg/m", conf.pgrad_amp_Pam / mmHg_TO_PASCAL);
        AddChildDimensionalValue(condition, "period", "s", conf.period_s);
        AddChildDimensionalValue(condition, "womersley_number", "dimensionless", conf.womersley);
    }

    void SimConfigWriter::DoIOForFileVelocityInOutlet(SimConfigWriter::Element& dest,
                                                      FileVelocityIoletConfig const& conf) const {
        auto condition = MakeCondition(dest, "velocity", "file");
        condition.SetAttribute("timing", conf.periodic ? "periodic" : "legacy");
        AddChildDimensionalValue(condition, "radius", "m", conf.radius_m);
        AddPathChild(condition, conf.file_path);
    }

    void SimConfigWriter::DoIOForProperties(std::vector<extraction::PropertyOutputFile> const& outputs) const {
        using namespace extraction;

        if (outputs.empty())
            return;
        auto prop_el = outputXml->GetRoot().AddChild("properties");
        for (auto& po: outputs) {
            auto po_el = prop_el.AddChild("propertyoutput");

            auto mode = overload_visit(po.ts_mode,
                                       [](multi_timestep_file) {
                                           return "multi";
                                       },
                                       [](single_timestep_files) {
                                           return "single";
                                       }
            );
            po_el.SetAttribute("timestep_mode", mode);
            po_el.SetAttribute("file", po.filename.c_str());
            po_el.SetAttribute("period", po.frequency);
            if (po.start != 0)
                po_el.SetAttribute("start", po.start);
            if (po.stop != std::numeric_limits<unsigned long>::max())
                po_el.SetAttribute("stop", po.stop);

            GeometrySelector* gmy = po.geometry.get();
            auto gmy_el = po_el.AddChild("geometry");
            if (auto whole = dynamic_cast<WholeGeometrySelector*>(gmy)) {
                gmy_el.SetAttribute("type", "whole");
            } else if (auto iolet = dynamic_cast<IoletGeometrySelector*>(gmy)) {
                gmy_el.SetAttribute("type", iolet->IsInlet() ? "inlet" : "outlet");
            }
            else if (auto sphere = dynamic_cast<SphereGeometrySelector *>(gmy))
            {
                gmy_el.SetAttribute("type",
                                    sphere->IsSurfaceOnly() ? "surfaceWithinSphere" : "sphere");
                AddChildDimensionalValue(gmy_el, "point", "m", sphere->GetPoint());
                AddChildDimensionalValue(gmy_el, "radius", "m", sphere->GetRadius());
            }
            else if (auto surface = dynamic_cast<GeometrySurfaceSelector *>(gmy))
            {
                gmy_el.SetAttribute("type", "surface");
            }
            else if (auto plane = dynamic_cast<PlaneGeometrySelector *>(gmy))
            {
                gmy_el.SetAttribute("type", "plane");
                AddChildDimensionalValue(gmy_el, "point", "m", plane->GetPoint());
                AddChildDimensionalValue(gmy_el, "normal", "dimensionless", plane->GetNormal());
                if (auto r = plane->GetRadius(); r > 0.0f) {
                    AddChildDimensionalValue(gmy_el, "radius", "m", r);
                }
            }
            else if (auto line = dynamic_cast<StraightLineGeometrySelector *>(gmy))
            {
                gmy_el.SetAttribute("type", "line");
                AddChildDimensionalValue(gmy_el, "point", "m", line->GetEndpoint1());
                AddChildDimensionalValue(gmy_el, "point", "m", line->GetEndpoint2());
            }
            else if (auto surf_pt = dynamic_cast<SurfacePointSelector *>(gmy))
            {
                gmy_el.SetAttribute("type", "surfacepoint");
                AddChildDimensionalValue(gmy_el, "point", "m", surf_pt->GetPoint());
            }
            else
            {
                throw (Exception() << "unknown type for property extraction geometry");
            }

            for (auto& f: po.fields) {
                auto field_el = po_el.AddChild("field");
                field_el.SetAttribute("datatype", std::visit([](auto value) {
                    using T = decltype(value);
                    if constexpr (std::is_same_v<T, float>) return "float";
                    else if constexpr (std::is_same_v<T, double>) return "double";
                    else if constexpr (std::is_same_v<T, std::int32_t>) return "int32";
                    else if constexpr (std::is_same_v<T, std::uint32_t>) return "uint32";
                    else if constexpr (std::is_same_v<T, std::int64_t>) return "int64";
                    else return "uint64";
                }, f.typecode));
                field_el.SetAttribute(
                    "type",
                    overload_visit(
                        f.src, [](source::Pressure) { return "pressure"; },
                        [](source::Velocity) { return "velocity"; }, [](source::VonMisesStress)
                        { return "vonmisesstress"; }, [](source::ShearStress)
                        { return "shearstress"; }, [](source::ShearRate) { return "shearrate"; },
                        [](source::StressTensor) { return "stresstensor"; }, [](source::Traction)
                        { return "traction"; }, [](source::TangentialProjectionTraction)
                        { return "tangentialprojectiontraction"; },
                        [](source::NormalProjectionTraction) { return "normalprojectiontraction"; },
                        [](source::WallExtension) { return "wallextension"; },
                        [](source::Distributions) { return "distributions"; },
                        [](source::MpiRank) { return "mpirank"; }));
                field_el.SetAttribute("name", f.name);
            }
        }
    }

    void SimConfigWriter::DoIOForInitialConditions(ICConfig const& ic_conf) {
        auto ic_el = outputXml->GetRoot().AddChild("initialconditions");

        auto set_time_maybe = [&](ICConfigBase const& base_conf) {
            if (base_conf.t0.has_value())
                AddChildDimensionalValue(ic_el, "time", "lattice", base_conf.t0.value());
        };

        overload_visit(ic_conf,
                       [](std::monostate const &) {
                           throw (Exception() << "Invalid initial condition config");
                       },
                       [&](EquilibriumIC const& _) {
                           set_time_maybe(_);
                           auto p_el = ic_el.AddChild("pressure");
                           AddChildDimensionalValue(p_el, "uniform", "mmHg", _.p_Pa / mmHg_TO_PASCAL);
                       },
                       [&](CheckpointIC const& _) {
                           set_time_maybe(_);
                           auto cp_el = ic_el.AddChild("checkpoint");
                           cp_el.SetAttribute("file", FullPathToRelPath(_.cpFile));
                           if (_.maybeOffFile.has_value())
                               cp_el.SetAttribute("offsets", FullPathToRelPath(_.maybeOffFile.value()));
                       }
        );
    }

    void SimConfigWriter::DoIOForMonitoring(const hemelb::configuration::MonitoringConfig &mon_conf) {
        auto mon_el = Element::Missing();
        auto ensure_monitoring_el = [&] () {
            if (mon_el == Element::Missing())
                mon_el = outputXml->GetRoot().AddChild("monitoring");
            return mon_el;
        };

        if (mon_conf.doConvergenceCheck) {
            auto el = ensure_monitoring_el();
            auto conv_el = el.AddChild("steady_flow_convergence");
            conv_el.SetAttribute("tolerance", mon_conf.convergenceRelativeTolerance);
            conv_el.SetAttribute("terminate", mon_conf.convergenceTerminate ? "true" : "false");

            HASSERT(std::holds_alternative<extraction::source::Velocity>(mon_conf.convergenceVariable));
            auto crit_el = conv_el.AddChild("criterion");
            crit_el.SetAttribute("type", "velocity");
            SetDimensionalValue(crit_el, "m/s", mon_conf.convergenceReferenceValue);
        }

        if (mon_conf.doIncompressibilityCheck) {
            auto el = ensure_monitoring_el();
            auto incomp_el = el.AddChild("incompressibility");
        }
    }
}