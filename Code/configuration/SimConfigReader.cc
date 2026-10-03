// This file is part of HemeLB and is Copyright (C)
// the HemeLB team and/or their institutions, as detailed in the
// file AUTHORS. This software is provided under the terms of the
// license in the file LICENSE.

#include "configuration/SimConfigReader.h"

#include <set>
#include <cmath>
#include <algorithm>
#include <array>
#include <concepts>
#include <memory>
#include <optional>
#include <utility>
#include <variant>

#include "build_info.h"
#include "constants.h"
#include "extraction/GeometrySelectors.h"
#include "io/TimePattern.h"
#include "log/Logger.h"
#include "net/MpiCommunicator.h"
#include "util/Vector3D.h"

namespace hemelb::configuration {
    using Element = io::xml::Element;

    void CheckNoAttributes(Element const& elem) {
        std::string message;
        for (char const* name: elem.Attributes()) {
            if (!message.empty())
                message += ", ";
            message += name;
        }
        if (!message.empty()) {
            throw (Exception() << "Element " << elem.GetFullPath()
                               << " has unexpected attributes: " << message);
        }
    }

    void CheckNoChildren(Element const& elem) {
        std::string message;
        for (auto child: elem.Children()) {
            if (!message.empty())
                message += ", ";
            message += child.GetName();
        }
        if (!message.empty()) {
            throw (Exception() << "Element " << elem.GetFullPath()
                               << " has unexpected children: " << message);
        }
    }

    void CheckEmpty(const Element& elem) {
        CheckNoChildren(elem);
        CheckNoAttributes(elem);
    }

    // Given an element, get those child elements specified by name (repeats OK)
    // Any missing or extra elements are an error.
    //
    // Probably easier to use the helper template below
    template <std::size_t N>
    auto GetExactChildren(Element const& el, std::array<char const*, N> names) {
        using PAIR = std::pair<std::size_t, std::string_view>;
        std::vector<PAIR> idx_names(N); //= {{0, NAMES}...};
        for (std::size_t i = 0; i < N; ++i) {
            idx_names[i] = {i, names[i]};
        }

        std::array<Element, N> ans;

        // Must inspect all children of the node
        for (auto child: el.Children()) {
            // Check the found child against allowed names
            auto match = std::find_if(idx_names.begin(), idx_names.end(),
                                      [&](PAIR const& i_name) {
                                          return i_name.second == child.GetName();
                                      });
            if (match == idx_names.end())
                throw (Exception() << "Unexpected element found: " << child.GetFullPath());
            // It was one of the expected, add to output
            ans[match->first] = child;
            // Remove from allowed names
            idx_names.erase(match);
        }
        // Now check for required names not found
        for (auto& [_, name]: idx_names) {
            throw (Exception() << "Required child not found: " << name);
        }
        return ans;
    }
    // Helper for the above.
    template <typename... Ts>
    auto GetExactChildren(Element const& el, Ts&... names) {
        constexpr auto N = sizeof...(Ts);
        return GetExactChildren<N>(el, std::array<char const*, N>{std::forward<Ts>(names)...});
    }

    // Like std::foreach but for the child sublements of the given name, any others being an error.
    template <std::invocable<Element const&> F>
    void ForExactChildren(Element const& el, std::string_view sub_el_name, F&& func) {
        for (auto sub_el: el.Children()) {
            if (auto name = sub_el.GetName(); name != sub_el_name) {
                throw (Exception() << "Unexpected child element: " << sub_el.GetFullPath());
            }
            std::invoke(func, sub_el);
        }
    }

    SimConfigReader::SimConfigReader(path path) :
            xmlFilePath(std::move(path))
    {
    }

    // join(sep, []) -> ""
    // join(sep, [x]) -> "x"
    // join(sep, [x,y]) -> "xsepy"
    template <std::input_iterator Iter, std::sentinel_for<Iter> Sentinel>
    std::string join(std::string const& sep, Iter begin, Sentinel end) {
        std::string ans;
        if (begin != end) {
            ans += *begin;
            ++begin;
        }
        for (; begin != end; ++begin) {
            ans += sep;
            ans += *begin;
        }
        return ans;
    }

    namespace {
        // Only pressure quantities accept mmHg. Stress and elastic modulus remain Pa.
        double PopPressureValue(Element& element, bool gradient = false) {
            const auto units = element.PopAttributeOrThrow("units");
            const auto siUnits = gradient ? "Pa/m" : "Pa";
            const auto conventionalUnits = gradient ? "mmHg/m" : "mmHg";
            if (units != siUnits && units != conventionalUnits)
                throw Exception() << "Invalid pressure units at " << element.GetPath()
                                  << ": expected " << conventionalUnits << " or " << siUnits;
            auto value = element.PopAttributeOrThrow<double>("value");
            if (units == conventionalUnits) value *= mmHg_TO_PASCAL;
            if (!std::isfinite(value))
                throw Exception() << "Non-finite pressure at " << element.GetPath();
            CheckEmpty(element);
            return value;
        }

        void NormaliseLegacyConfig(Element root) {
            if (root.GetName() != "hemelbsettings") return;
            const auto version = root.GetAttributeOrThrow<unsigned>("version");
            if (version != 3 && version != 5) return;
            // Legacy geometry tools emitted camera settings for the retired
            // in-solver renderer. They do not change the numerical simulation.
            if (auto view = root.GetChildOrNull("visualisation"))
                view.Delete();
            auto simulation = root.GetChildOrThrow("simulation");
            if (auto stress = simulation.GetChildOrNull("stresstype")) {
                if (stress.GetAttributeOrThrow<unsigned>("value") > 2)
                    throw Exception() << "Invalid legacy stresstype";
                stress.Delete();
            }
            for (auto tag: {"particles", "bodyForces", "boundaryConditions"})
                if (root.GetChildOrNull(tag)) throw Exception() << "Unsupported legacy feature: " << tag;

            const auto dx = simulation.GetChildOrThrow("voxel_size").GetAttributeOrThrow<double>("value");
            const auto origin = simulation.GetChildOrThrow("origin").GetAttributeOrThrow<PhysicalPosition>("value");
            for (auto group: {"inlets", "outlets"}) {
                for (auto iolet: root.GetChildOrThrow(group).Children()) {
                    auto position = iolet.GetChildOrThrow("position");
                    if (version == 3 && position.GetAttributeOrThrow("units") == "lattice") {
                        position.SetAttribute("value", origin + position.GetAttributeOrThrow<PhysicalPosition>("value") * dx);
                        position.SetAttribute("units", "m");
                    }
                    auto condition = iolet.GetChildOrThrow("condition");
                    const auto type = condition.GetAttributeOrThrow("type");
                    const auto subtype = condition.GetAttributeOrThrow("subtype");
                    if ((type == "pressure" || type == "yangpressure") && subtype == "file")
                        condition.SetAttribute("units", "mmHg");
                    if (type == "velocity" && subtype == "readWrite")
                        condition.SetAttribute("pressure_units", "mmHg");
                    if ((type == "pressure" || type == "yangpressure") && subtype == "cosine") {
                        for (auto tag: {"radius", "area"})
                            if (auto unused = condition.GetChildOrNull(tag)) unused.Delete();
                    }
                    if (type == "velocity" && subtype == "file")
                        if (auto unused = condition.GetChildOrNull("area")) unused.Delete();
                }
            }
            auto initial = root.GetChildOrThrow("initialconditions");
            if (auto pressure = initial.GetChildOrNull("pressure")) {
                if (auto checkpoint = pressure.GetChildOrNull("checkpoint")) {
                    if (pressure.GetChildOrNull("uniform") || initial.GetChildOrNull("checkpoint"))
                        throw Exception() << "Conflicting legacy initial conditions";
                    initial.CopyAsChild(checkpoint);
                    checkpoint.Delete();
                    CheckEmpty(pressure);
                    pressure.Delete();
                }
            }
            if (auto properties = root.GetChildOrNull("properties")) {
                // Keep legacy checkpoint paths and periods as ordinary distribution outputs.
                for (auto checkpoint: properties.PopChildren("checkpoint")) {
                    auto output = properties.AddChild("propertyoutput");
                    output.SetAttribute("file", checkpoint.PopAttributeOrThrow("file"));
                    output.SetAttribute("period", checkpoint.PopAttributeOrThrow<unsigned long>("period"));
                    output.SetAttribute("timestep_mode", "single");
                    output.AddChild("geometry").SetAttribute("type", "whole");
                    auto field = output.AddChild("field");
                    field.SetAttribute("type", "distributions");
                    field.SetAttribute("name", "distributions");
                    field.SetAttribute("datatype", "double");
                    CheckEmpty(checkpoint);
                }
                for (auto output: properties.Children("propertyoutput")) {
                    auto selector = output.GetChildOrThrow("geometry");
                    const auto type = selector.GetAttributeOrThrow("type");
                    if (type == "inlet" || type == "outlet" || type == "surfacepoint")
                        if (auto unused = selector.GetChildOrNull("normal")) unused.Delete();
                }
            }
            root.SetAttribute("version", 6U);
        }
    }

    SimConfig SimConfigReader::Read() const
    {
        if (!std::filesystem::exists(xmlFilePath))
        {
            throw Exception() << "Config file '" << xmlFilePath << "' does not exist";
        }
        auto rawXmlDoc = std::make_shared<io::xml::Document>(xmlFilePath);
        // This copy will be "consumed" by the reading to check no unknown elements/attributes
        auto tmpDoc = rawXmlDoc->DeepCopy();
        NormaliseLegacyConfig(tmpDoc.GetRoot());
        auto ans = DoIO(tmpDoc.GetRoot());
        if (auto& errs = tmpDoc.GetErrors(); !errs.empty()) {
            throw Exception() << "Errors reading " << xmlFilePath << ": " << join(", ", errs.begin(), errs.end());
        }
        ans.source_xml = rawXmlDoc;
        return ans;
    }

    // Turn an input XML-relative path into a full path
    std::filesystem::path SimConfigReader::RelPathToFullPath(std::string_view path) const {
        auto xml_dir = xmlFilePath.parent_path();
        return std::filesystem::weakly_canonical(xml_dir / path);
    }

    SimConfig SimConfigReader::DoIO(Element topNode) const
    {
        SimConfig ans;
        // Top element must be:
        // <hemelbsettings version="6" />
        constexpr unsigned VERSION = 6;
        if (topNode.GetName() != "hemelbsettings")
            throw Exception() << "Invalid root element: " << topNode.GetFullPath();

        auto version = topNode.PopAttributeOrThrow<unsigned>("version");
        if (version != VERSION)
            throw Exception() << "Unrecognised XML version. Expected " << VERSION << ", got " << version;
        CheckNoAttributes(topNode);

        ans.sim_info = DoIOForSimulation(*topNode.PopChildOrThrow("simulation"));

        ans.dataFilePath = DoIOForGeometry(*topNode.PopChildOrThrow("geometry"));

        auto readTracers = [&](Element &el)
        {
            tracers::TracerConfig c;
            auto dimensional = [&](Element &node, char const *units)
            {
                if (node.PopAttributeOrThrow("units") != units)
                    throw Exception() << "Tracer units must be " << units;
            };
            auto vector = [&](Element &node)
            {
                dimensional(node, "lattice");
                LatticePosition x{node.PopAttributeOrThrow<double>("x"),
                                  node.PopAttributeOrThrow<double>("y"),
                                  node.PopAttributeOrThrow<double>("z")};
                for (int i = 0; i < 3; ++i)
                    if (!std::isfinite(x[i]))
                        throw Exception() << "Tracer coordinates must be finite";
                CheckEmpty(node);
                return x;
            };
            c.outputPeriod = el.PopAttributeMaybe<LatticeTimeStep>("output_period").value_or(1);
            c.seed = el.PopAttributeMaybe<std::uint64_t>("seed").value_or(0);
            c.nextId = el.PopAttributeMaybe<std::uint64_t>("next_id").value_or(0);
            c.nextEmission = el.PopAttributeMaybe<LatticeTimeStep>("next_emission").value_or(1);
            c.particleRadius = el.PopAttributeMaybe<double>("particle_radius").value_or(0);
            {
                auto particles = el.PopChildOrThrow("particles");
                std::set<std::uint64_t> ids;
                for (auto particle : particles->PopChildren("subgridParticle"))
                {
                    tracers::Particle p;
                    dimensional(particle, "lattice");
                    p.id = particle.PopAttributeOrThrow<std::uint64_t>("ParticleId");
                    p.radius = particle.PopAttributeOrThrow<double>("Radius");
                    p.created = particle.PopAttributeMaybe<LatticeTimeStep>("created").value_or(0);
                    p.active = particle.PopAttributeMaybe<unsigned>("active").value_or(1) != 0;
                    p.position = vector(*particle.PopChildOrThrow("initialPosition"));
                    if (auto velocity = particle.PopChildOrNull("velocity"))
                        p.velocity = vector(*velocity);
                    if (!std::isfinite(p.radius) || p.radius < 0 || !ids.insert(p.id).second ||
                        p.id == UINT64_MAX)
                        throw Exception()
                            << "Tracer radii must be nonnegative and particle IDs unique";
                    c.nextId = std::max(c.nextId, p.id + 1);
                    c.particles.push_back(p);
                    CheckEmpty(particle);
                }
                if (auto centre = particles->PopChildOrNull("sphereCentre"))
                    c.sphereCentre = vector(*centre);
                if (auto radius = particles->PopChildOrNull("sphereRadius"))
                    c.sphereRadius = PopDimensionalValue<double>(*radius, "lattice");
                if (auto count = particles->PopChildOrNull("emissionCount"))
                    c.emissionCount = PopDimensionalValue<unsigned>(*count, "dimensionless");
                if (auto interval = particles->PopChildOrNull("emissionItrvl"))
                    c.emissionInterval =
                        PopDimensionalValue<LatticeTimeStep>(*interval, "dimensionless");
                CheckEmpty(*particles);
            }
            if (c.particleRadius == 0 && !c.particles.empty())
                c.particleRadius = c.particles.front().radius;
            // Explicit tracer mode permits old force declarations, which the source
            // tracer build also ignored. Unmarked force-coupled inputs are rejected.
            auto mode = el.PopAttributeMaybe("mode").value_or("passive");
            if (mode != "passive" && mode != "tracer")
                throw Exception() << "Only passive tracer mode is supported";
            if (auto forces = el.PopChildOrNull("bodyForces"))
            {
                if (mode != "tracer")
                    CheckEmpty(*forces);
                else
                {
                    std::vector<Element> ignored;
                    for (auto child : forces->Children())
                        ignored.push_back(child);
                    for (auto child : ignored)
                        child.Delete();
                    CheckEmpty(*forces);
                }
            }
            if (auto rules = el.PopChildOrNull("boundaryConditions"))
            {
                for (auto const *kind : {"lubrication", "deletion", "spherical"})
                    for (auto node : rules->PopChildren(kind))
                    {
                        tracers::BoundaryRule rule;
                        rule.kind = kind;
                        rule.appliesTo = node.PopAttributeOrThrow("appliesTo");
                        if (rule.appliesTo != "Wall" && rule.appliesTo != "Ilet" &&
                            rule.appliesTo != "Olet" && rule.appliesTo != "Sphr")
                            throw Exception() << "Invalid tracer boundary appliesTo";
                        if (rule.kind == "spherical")
                        {
                            if (rule.appliesTo != "Sphr")
                                throw Exception() << "Spherical tracer boundary requires Sphr";
                            rule.radius = PopDimensionalValue<double>(
                                *node.PopChildOrThrow("sphereRadius"), "lattice");
                            rule.centre = vector(*node.PopChildOrThrow("sphereCentre"));
                            if (!std::isfinite(rule.radius) || rule.radius < 0)
                                throw Exception() << "Invalid tracer boundary sphere radius";
                        }
                        else
                        {
                            rule.range = node.PopAttributeOrThrow<double>("effectiveRange");
                            if (!std::isfinite(rule.range) || rule.range <= 0)
                                throw Exception() << "Tracer boundary range must be positive";
                            if (rule.appliesTo == "Sphr")
                                throw Exception() << "Sphr requires a spherical tracer boundary";
                        }
                        CheckEmpty(node);
                        c.boundaries.push_back(rule);
                    }
                CheckEmpty(*rules);
            }
            if (el.GetName() == "colloids" && !c.particles.empty())
            {
                c.particleRadius = c.particles.front().radius;
                c.particles.erase(c.particles.begin());
                c.nextEmission = c.emissionInterval;
            }
            if (!c.emissionInterval)
            {
                c.emissionCount = 0;
                c.emissionInterval = 1;
            }
            if (!c.outputPeriod || !std::isfinite(c.sphereRadius) || c.sphereRadius < 0 ||
                !std::isfinite(c.particleRadius) || c.particleRadius < 0)
                throw Exception() << "Invalid tracer emission parameters";
            CheckEmpty(el);
            return c;
        };
        if (auto tracers = topNode.PopChildOrNull("tracers"))
            ans.tracers = readTracers(*tracers);
        if (auto colloids = topNode.PopChildOrNull("colloids"))
        {
            if (ans.tracers)
                throw Exception() << "Specify tracers or passive colloids, not both";
            ans.tracers = readTracers(*colloids);
        }

        {
            auto initial = topNode.PopChildOrThrow("initialconditions");
            if (auto sponge = initial->PopChildOrNull("sponge_layer"))
            {
                SpongeInfo sp;
                sp.viscosity_ratio = PopDimensionalValue<double>(
                    *sponge->PopChildOrThrow("viscosity_ratio"), "dimensionless");
                sp.width_m = PopDimensionalValue<double>(*sponge->PopChildOrThrow("width"), "m");
                sp.lifetime = PopDimensionalValue<LatticeTimeStep>(
                    *sponge->PopChildOrThrow("lifetime"), "lattice");
                if (!std::isfinite(sp.viscosity_ratio) || sp.viscosity_ratio < 1 ||
                    !std::isfinite(sp.width_m) || sp.width_m <= 0 || sp.lifetime == 0)
                    throw Exception()
                        << "Sponge ratio must be at least one, width positive and lifetime nonzero";
                CheckEmpty(*sponge);
                ans.sim_info.sponge = sp;
            }
            ans.initial_condition = DoIOForInitialConditions(*initial);
        }

        ans.inlets = DoIOForInOutlets(ans.sim_info, *topNode.PopChildOrThrow("inlets"));
        ans.outlets = DoIOForInOutlets(ans.sim_info, *topNode.PopChildOrThrow("outlets"));

        // Optional element <properties>
        if (auto propertiesEl = topNode.PopChildOrNull("properties"))
            ans.propertyOutputs = DoIOForProperties(ans.sim_info, *propertiesEl);

        // Optional element <monitoring>
        if (auto monitoringEl = topNode.PopChildOrNull("monitoring"))
            ans.monitoringConfig = DoIOForMonitoring(*monitoringEl);

        // The RBC section must be parsed *after* the inlets and outlets have been
        // defined
        if (auto rbcEl = topNode.PopChildOrNull("redbloodcells")) {
            if constexpr (build_info::BUILD_RBC) {
                ans.rbcConf = DoIOForRedBloodCells(ans, *rbcEl);
                CheckEmpty(*rbcEl);
            } else {
                throw Exception() << "Input XML has redbloodcells section but HEMELB_BUILD_RBC=OFF";
            }
        }
        if (auto el = topNode.PopChildOrNull("decomposition")) {
            ans.decompositionMethod = el->PopAttributeOrThrow("method");
            ans.geometryReaderCount = el->PopAttributeMaybe<unsigned>("reader_count").value_or(0);
            ans.geometryReaderSpacing =
                el->PopAttributeMaybe<unsigned>("reader_spacing").value_or(1);
            if (!ans.geometryReaderSpacing)
                throw Exception() << "reader_spacing must be positive";
            if (ans.decompositionMethod != "octree" && ans.decompositionMethod != "parmetis")
                throw Exception() << "Unknown decomposition method: " << ans.decompositionMethod;
            CheckEmpty(*el);
        }
        CheckNoChildren(topNode);
        return ans;
    }

    GlobalSimInfo SimConfigReader::DoIOForSimulation(Element simEl) const
    {
        GlobalSimInfo ans;
        {
            // Required element
            // <steps value="unsigned" units="lattice />
            auto stepsEl = simEl.PopChildOrThrow("steps");
            PopDimensionalValue(*stepsEl, "lattice", ans.time.total_steps);

            // Required element
            // <step_length value="float" units="s" />
            auto tsEl = simEl.PopChildOrThrow("step_length");
            PopDimensionalValue(*tsEl, "s", ans.time.step_s);

            // Optional element
            // <extra_warmup_steps value="unsigned" units="lattice" />
            if (auto wuEl = simEl.PopChildOrNull("extra_warmup_steps")) {
                PopDimensionalValue(*wuEl, "lattice", ans.time.warmup_steps);
                ans.time.total_steps += ans.time.warmup_steps;
            } else {
                ans.time.warmup_steps = 0;
            }

            // Required element
            // <voxel_size value="float" units="m" />
            auto vsEl = simEl.PopChildOrThrow("voxel_size");
            PopDimensionalValue(*vsEl, "m", ans.space.step_m);

            // Required element
            // <origin value="(x,y,z)" units="m" />
            auto originEl = simEl.PopChildOrThrow("origin");
            PopDimensionalValue(*originEl, "m", ans.space.geometry_origin_m);

            // Optional element
            // <fluid_density value="float" units="kg/m3" />
            ans.fluid.density_kgm3 = simEl.PopChildOrNull("fluid_density")->transform(
                    [](Element& el) {
                        return PopDimensionalValue<PhysicalDensity>(el, "kg/m3");
                    }).value_or(DEFAULT_FLUID_DENSITY_Kg_per_m3);

            // Optional element
            // <fluid_viscosity value="float" units="Pa.s" />
            ans.fluid.viscosity_Pas = simEl.PopChildOrNull("fluid_viscosity")->transform(
                    [](Element& el) {
                        return PopDimensionalValue<PhysicalDynamicViscosity>(el, "Pa.s");
                    }).value_or(DEFAULT_FLUID_VISCOSITY_Pas);

            // Optional element (default = 0)
            // <reference_pressure value="float" units="mmHg" />
            ans.fluid.reference_pressure_Pa = simEl.PopChildOrNull("reference_pressure")->transform(
                    [](Element& el) {
                        return PopPressureValue(el);
                    }).value_or(0);

            ans.checkpoint = simEl.PopChildOrNull("checkpoint")->transform(
                    [](Element& el) {
                        CheckpointInfo ans;
                        el.PopAttributeOrThrow("period", ans.period);
                        return ans;
                    }
            );
        }
        if (auto el = simEl.PopChildOrNull("smagorinsky_constant"))
            ans.smagorinsky = PopDimensionalValue<double>(*el, "dimensionless");
        if (auto el = simEl.PopChildOrNull("elastic_wall_stiffness"))
            ans.elastic_wall_stiffness = PopDimensionalValue<double>(*el, "lattice");
        if (auto el = simEl.PopChildOrNull("boundary_velocity_ratio"))
            ans.boundary_velocity_ratio = PopDimensionalValue<double>(*el, "lattice");
        if (!std::isfinite(ans.smagorinsky) || ans.smagorinsky < 0 || ans.smagorinsky > 1 ||
            !std::isfinite(ans.elastic_wall_stiffness) || ans.elastic_wall_stiffness < 0 ||
            !std::isfinite(ans.boundary_velocity_ratio) || ans.boundary_velocity_ratio < 0 ||
            ans.boundary_velocity_ratio > 1)
            throw Exception()
                << "Invalid LES coefficient, elastic stiffness or boundary velocity ratio";
        CheckEmpty(simEl);
        return ans;
    }

    auto SimConfigReader::DoIOForGeometry(Element geometryEl) const -> path
    {
        path ans;
        {
            // Required element
            // <geometry>
            //  <datafile path="relative path to GMY" />
            // </geometry>
            auto fileEl = geometryEl.PopChildOrThrow("datafile");

            ans = RelPathToFullPath(fileEl->PopAttributeOrThrow("path"));
            CheckEmpty(*fileEl);
        }
        CheckEmpty(geometryEl);
        return ans;
    }

    /**
     * Helper function to ensure that the iolet being created matches the compiled
     * iolet BC.
     * @param ioletEl
     * @param requiredBC
     */
    void SimConfigReader::CheckIoletMatchesCMake(const Element& ioletEl,
                                                 std::string_view requiredBC) const
    {
        // Check that HEMELB_*LET_BOUNDARY is consistent with this
        auto ioletTypeName = ioletEl.GetName();
        std::string_view hemeIoletBC;

        if (ioletTypeName == "inlet")
            hemeIoletBC = build_info::INLET_BOUNDARY.view();
        else if (ioletTypeName == "outlet")
            hemeIoletBC = build_info::OUTLET_BOUNDARY.view();
        else
            throw Exception() << "Unexpected element name '" << ioletTypeName
                              << "'. Expected 'inlet' or 'outlet'";

        if (requiredBC != hemeIoletBC)
        {
            throw Exception() << "XML configuration for " << ioletTypeName << " (line "
                              << ioletEl.GetLine()
                              << ") not consistent with compile-time choice of boundary condition '" << hemeIoletBC
                              << "'";
        }
    }

    auto SimConfigReader::DoIOForInOutlets(GlobalSimInfo const& sim_info, Element ioletsEl) const -> std::vector<IoletConfig>
    {
        auto nodeName = ioletsEl.GetName();
        auto childNodeName = std::string{nodeName.substr(0, nodeName.size() - 1)};
        std::vector<IoletConfig> ioletList;
        for (auto currentIoletNode: ioletsEl.PopChildren(childNodeName)) {
            // Determine which InOutlet to create
            auto conditionEl = currentIoletNode.GetChildOrThrow("condition");
            auto conditionType = conditionEl.PopAttributeOrThrow("type");

            IoletConfig newIolet;

            if (conditionType == "yangpressure")
            {
                CheckIoletMatchesCMake(currentIoletNode, "YANGPRESSUREIOLET");
            }
            if (conditionType == "pressure" || conditionType == "yangpressure")
            {
                newIolet = DoIOForPressureInOutlet(currentIoletNode);
            }
            else if (conditionType == "velocity")
            {
                newIolet = DoIOForVelocityInOutlet(currentIoletNode);
            }
            else
            {
                throw Exception() << "Invalid boundary condition type '" << conditionType << "' in "
                                  << conditionEl.GetFullPath();
            }
            std::visit([&](auto &conf) {
                           if constexpr (std::is_same_v<decltype(conf), std::monostate &>) {
                               throw Exception();
                           } else {
                               DoIOForBaseInOutlet(sim_info, currentIoletNode, conf);
                           }
                       },
                       newIolet);
            ioletList.push_back(std::move(newIolet));
            CheckEmpty(currentIoletNode);
        }
        return ioletList;
    }

    auto SimConfigReader::DoIOForPressureInOutlet(Element& ioletEl) const -> IoletConfig
    {
        constexpr auto inlet = build_info::INLET_BOUNDARY;
        constexpr auto outlet = build_info::OUTLET_BOUNDARY;
        bool isInlet = ioletEl.GetName() == "inlet";
        bool yang = (isInlet ? inlet : outlet) == "YANGPRESSUREIOLET";
        CheckIoletMatchesCMake(ioletEl,
                               yang ? "YANGPRESSUREIOLET" : "NASHZEROTHORDERPRESSUREIOLET");
        auto conditionEl = ioletEl.PopChildOrThrow("condition");
        auto conditionSubtype = conditionEl->PopAttributeOrThrow("subtype");

        if (conditionSubtype == "cosine")
        {
            return DoIOForCosinePressureInOutlet(*conditionEl);
        }
        else if (conditionSubtype == "file")
        {
            return DoIOForFilePressureInOutlet(*conditionEl);
        }
        else if (conditionSubtype == "WK2" || conditionSubtype == "WK3" ||
                 conditionSubtype == "fileWK")
        {
            WindkesselPressureIoletConfig c;
            c.model = conditionSubtype;
            auto value = [&](char const *name, char const *units)
            { return PopDimensionalValue<double>(*conditionEl->PopChildOrThrow(name), units); };
            if (conditionSubtype == "WK3")
            {
                c.characteristic_resistance = value("Rc", "kg/m^4*s");
                c.peripheral_resistance = value("Rp", "kg/m^4*s");
                c.capacitance = value("Cp", "m^4*s^2/kg");
            }
            else
            {
                c.peripheral_resistance = value("R", "kg/m^4*s");
                c.capacitance = value("C", "m^4*s^2/kg");
            }
            c.area_m2 = value("area", "m^2");
            if (conditionSubtype != "fileWK")
                c.radius_m = value("radius", "m");
            else
                c.weights_path = std::filesystem::absolute(
                    xmlFilePath.parent_path() /
                    conditionEl->PopChildOrThrow("path")->PopAttributeOrThrow("value"));
            if (auto state = conditionEl->PopChildOrNull("state"))
            {
                if (auto conventional = state->PopAttributeMaybe<double>("pressure_mmHg")) {
                    c.pressure_Pa = *conventional * mmHg_TO_PASCAL;
                } else {
                    c.pressure_Pa = state->PopAttributeOrThrow<double>("pressure_Pa");
                }
                c.flow_m3s = state->PopAttributeOrThrow<double>("flow_m3s");
                c.previous_flow_m3s = state->PopAttributeOrThrow<double>("previous_flow_m3s");
                CheckEmpty(*state);
            }
            for (auto x : {c.peripheral_resistance, c.capacitance, c.area_m2})
                if (!std::isfinite(x) || x <= 0)
                    throw Exception() << "Windkessel R, C and area must be finite and positive";
            if (!std::isfinite(c.characteristic_resistance) || c.characteristic_resistance < 0 ||
                (conditionSubtype != "fileWK" && (!std::isfinite(c.radius_m) || c.radius_m <= 0)))
                throw Exception() << "Invalid Windkessel characteristic resistance or radius";
            for (auto x : {c.pressure_Pa, c.flow_m3s, c.previous_flow_m3s})
                if (!std::isfinite(x))
                    throw Exception() << "Windkessel state must be finite";
            CheckEmpty(*conditionEl);
            return c;
        }
        else if (conditionSubtype == "multiscale")
        {
            return DoIOForMultiscalePressureInOutlet(*conditionEl);
        }
        else
        {
            throw Exception() << "Invalid boundary condition subtype '" << conditionSubtype << "' in "
                              << ioletEl.GetFullPath();
        }
    }

    auto SimConfigReader::DoIOForVelocityInOutlet(Element& ioletEl) const -> IoletConfig
    {
        CheckIoletMatchesCMake(ioletEl, "LADDIOLET");
        auto conditionEl = ioletEl.PopChildOrThrow("condition");
        auto conditionSubtype = conditionEl->PopAttributeOrThrow("subtype");

        if (conditionSubtype == "parabolic")
        {
            return DoIOForParabolicVelocityInOutlet(*conditionEl);
        }
        else if (conditionSubtype == "womersley")
        {
            return DoIOForWomersleyVelocityInOutlet(*conditionEl);
        }
        else if (conditionSubtype == "womersleyElastic")
        {
            ElasticWomersleyVelocityIoletConfig c;
            c.radius_m = PopDimensionalValue<double>(*conditionEl->PopChildOrThrow("radius"), "m");
            c.pgrad_amp_Pam = PopPressureValue(
                *conditionEl->PopChildOrThrow("pressure_gradient_amplitude"), true);
            c.period_s = PopDimensionalValue<double>(*conditionEl->PopChildOrThrow("period"), "s");
            c.womersley = PopDimensionalValue<double>(
                *conditionEl->PopChildOrThrow("womersley_number"), "dimensionless");
            c.poisson_ratio = PopDimensionalValue<double>(
                *conditionEl->PopChildOrThrow("poisson_ratio"), "dimensionless");
            c.youngs_modulus_Pa =
                PopDimensionalValue<double>(*conditionEl->PopChildOrThrow("youngs_modulus"), "Pa");
            c.axial_position_m =
                PopDimensionalValue<double>(*conditionEl->PopChildOrThrow("axial_position"), "m");
            for (double x : {c.radius_m, c.period_s, c.womersley, c.youngs_modulus_Pa})
                if (!std::isfinite(x) || x <= 0)
                    throw Exception() << "Elastic Womersley radius, period, alpha and Young "
                                         "modulus must be positive";
            if (!std::isfinite(c.pgrad_amp_Pam) || !std::isfinite(c.axial_position_m) ||
                !std::isfinite(c.poisson_ratio) || c.poisson_ratio <= -1 || c.poisson_ratio >= 0.5)
                throw Exception() << "Invalid elastic Womersley profile parameters";
            CheckEmpty(*conditionEl);
            return c;
        }
        else if (conditionSubtype == "readWrite")
        {
            ReadWriteVelocityIoletConfig c;
            auto value = [&](char const *name, char const *units)
            { return PopDimensionalValue<double>(*conditionEl->PopChildOrThrow(name), units); };
            auto path = [&](char const *name)
            {
                return std::filesystem::absolute(
                    xmlFilePath.parent_path() /
                    conditionEl->PopChildOrThrow(name)->PopAttributeOrThrow("value"));
            };
            c.radius_m = value("radius", "m");
            c.area_m2 = value("area", "m^2");
            c.frequency = PopDimensionalValue<LatticeTimeStep>(
                *conditionEl->PopChildOrThrow("frequency"), "lattice");
            c.flow_path = path("flowRateFilePath");
            c.pressure_path = path("pressureFilePath");
            if (conditionEl->GetChildOrNull("weightsFilePath"))
                c.weights_path = path("weightsFilePath");
            c.flow_conversion = value("flowRateConversionFactor", "dimensionless");
            c.pressure_conversion = value("pressureConversionFactor", "dimensionless");
            c.smoothing = value("smoothingFactor", "dimensionless");
            auto pressureUnits = conditionEl->PopAttributeMaybe("pressure_units").value_or("Pa");
            if (pressureUnits != "Pa" && pressureUnits != "mmHg")
                throw Exception() << "readWrite pressure_units must be Pa or mmHg";
            c.pressure_mmHg = pressureUnits == "mmHg";
            if (auto timeout = conditionEl->PopAttributeMaybe<double>("timeout_s"))
                c.timeout_s = *timeout;
            if (auto state = conditionEl->PopChildOrNull("state"))
            {
                c.max_speed_ms = state->PopAttributeOrThrow<double>("max_speed_ms");
                c.next_exchange = state->PopAttributeOrThrow<LatticeTimeStep>("next_exchange");
                c.average_density = state->PopAttributeMaybe<double>("average_density").value_or(1);
                c.start_time_s = state->PopAttributeMaybe<double>("start_time_s").value_or(0);
                CheckEmpty(*state);
            }
            for (double x : {c.radius_m, c.area_m2, c.timeout_s})
                if (!std::isfinite(x) || x <= 0)
                    throw Exception() << "readWrite radius, area and timeout must be positive";
            for (double x : {c.flow_conversion, c.pressure_conversion, c.smoothing, c.max_speed_ms,
                             c.average_density})
                if (!std::isfinite(x))
                    throw Exception() << "readWrite factors and state must be finite";
            if (c.start_time_s && !std::isfinite(*c.start_time_s))
                throw Exception() << "readWrite initial time must be finite";
            if (c.frequency == 0 || c.smoothing < 0 || c.smoothing > 1 || c.next_exchange < 2)
                throw Exception() << "Invalid readWrite frequency, smoothing or state";
            CheckEmpty(*conditionEl);
            return c;
        }
        else if (conditionSubtype == "file")
        {
            return DoIOForFileVelocityInOutlet(*conditionEl);
        }
        else
        {
            throw Exception() << "Invalid boundary condition subtype '" << conditionSubtype << "' in "
                              << ioletEl.GetFullPath();
        }
    }



    std::vector<extraction::PropertyOutputFile> SimConfigReader::DoIOForProperties(GlobalSimInfo const& sim_info, const Element& propertiesEl) const
    {
        std::vector<extraction::PropertyOutputFile> propertyOutputs;
        for (auto _: propertiesEl.PopChildren("propertyoutput")) {
            propertyOutputs.push_back(DoIOForPropertyOutputFile(sim_info, _));
            CheckEmpty(_);
        }
        CheckEmpty(propertiesEl);
        return propertyOutputs;
    }

    extraction::PropertyOutputFile SimConfigReader::DoIOForPropertyOutputFile(GlobalSimInfo const& sim_info,
                                                                              Element& propertyoutputEl) const
    {
        auto file = extraction::PropertyOutputFile{};

        auto&& ts_mode = propertyoutputEl.PopAttributeMaybe("timestep_mode").value_or("multi");
        if (ts_mode == "multi") {
            file.ts_mode = extraction::multi_timestep_file{};
        } else if (ts_mode == "single") {
            file.ts_mode = extraction::single_timestep_files{};
        } else {
            throw Exception()
                    << "Invalid value of timestep_mode attribute '" << ts_mode
                    << "' at: " << propertyoutputEl.GetFullPath();
        }

        file.filename = propertyoutputEl.PopAttributeOrThrow("file");
        if (std::holds_alternative<extraction::single_timestep_files>(file.ts_mode)) {
            if (!io::TimePattern::Check(file.filename.native()))
                throw (Exception() << "For single timestep output files, "
                                      "the path must contain exactly one '%d' and no other '%' characters");
        }

        propertyoutputEl.PopAttributeOrThrow("period", file.frequency);
        file.start = propertyoutputEl.PopAttributeMaybe<unsigned long>("start").value_or(0);
        file.stop = propertyoutputEl.PopAttributeMaybe<unsigned long>("stop").value_or(
            std::numeric_limits<unsigned long>::max());
        if (file.frequency == 0 || file.stop < file.start)
            throw Exception() << "Property output requires a positive period and stop >= start";

        auto geometryEl = propertyoutputEl.PopChildOrThrow("geometry");
        auto type = geometryEl->PopAttributeOrThrow("type");

        if (type == "plane")
        {
            file.geometry.reset(DoIOForPlaneGeometry(*geometryEl));
        }
        else if (type == "line")
        {
            file.geometry.reset(DoIOForLineGeometry(*geometryEl));
        }
        else if (type == "sphere" || type == "surfaceWithinSphere")
        {
            auto point =
                PopDimensionalValue<PhysicalPosition>(*geometryEl->PopChildOrThrow("point"), "m");
            auto radius =
                PopDimensionalValue<PhysicalDistance>(*geometryEl->PopChildOrThrow("radius"), "m");
            file.geometry.reset(new extraction::SphereGeometrySelector(
                point, radius, type == "surfaceWithinSphere"));
        }
        else if (type == "whole")
        {
            file.geometry.reset(new extraction::WholeGeometrySelector());
        }
        else if (type == "inlet" || type == "outlet") {
            file.geometry.reset(new extraction::IoletGeometrySelector(type == "inlet"));
        }
        else if (type == "surface")
        {
            file.geometry.reset(new extraction::GeometrySurfaceSelector());
        }
        else if (type == "surfacepoint")
        {
            file.geometry.reset(DoIOForSurfacePoint(*geometryEl));
        }
        else
        {
            throw Exception() << "Unrecognised property output geometry selector '" << type
                              << "' in element " << geometryEl->GetFullPath();
        }
        CheckEmpty(*geometryEl);

        for (auto fieldEl: propertyoutputEl.PopChildren("field")) {
            file.fields.push_back(DoIOForPropertyField(sim_info, fieldEl));
            CheckEmpty(fieldEl);
        }

        return file;
    }

    extraction::StraightLineGeometrySelector*
    SimConfigReader::DoIOForLineGeometry(Element& geometryEl) const
    {
        std::vector<PhysicalPosition> points;
        for (auto child: geometryEl.PopChildren("point")) {
            points.push_back(PopDimensionalValue<PhysicalPosition>(child, "m"));
        }
        if (auto n = points.size(); n != 2)
            throw (Exception() << "Must have exactly 2 points for line, instead have " << n);

        return new extraction::StraightLineGeometrySelector(points[0].as<float>(), points[1].as<float>());
    }



    extraction::PlaneGeometrySelector*
    SimConfigReader::DoIOForPlaneGeometry(Element& geometryEl) const
    {
        auto point = PopDimensionalValue<PhysicalPosition>(*geometryEl.PopChildOrThrow("point"), "m");
        auto normal = PopDimensionalValue<util::Vector3D<float>>(*geometryEl.PopChildOrThrow("normal"), "dimensionless");

        auto radius = geometryEl.PopChildOrNull("radius")->transform(
                [](Element& el) {
                    return PopDimensionalValue<PhysicalDistance>(el, "m");
                });
        if (radius)
            return new extraction::PlaneGeometrySelector(point.as<float>(), normal, *radius);
        else
            return new extraction::PlaneGeometrySelector(point.as<float>(), normal);
    }

    extraction::SurfacePointSelector*
    SimConfigReader::DoIOForSurfacePoint(Element& geometryEl) const
    {
        auto point = PopDimensionalValue<PhysicalPosition>(*geometryEl.PopChildOrThrow("point"), "m");
        return new extraction::SurfacePointSelector(point.as<float>());
    }

    extraction::OutputField
    SimConfigReader::DoIOForPropertyField(GlobalSimInfo const& sim_info, Element& fieldEl) const
    {
        extraction::OutputField field;
        auto type = fieldEl.PopAttributeOrThrow("type");
        // Default name is identical to type.
        field.name = fieldEl.PopAttributeMaybe("name").value_or(type);

        // Default offset is none
        field.noffsets = 0;
        field.offset = {};

        // The default type to be written is float
        field.typecode = float{0.0};

        // Check and assign the type.
        if (type == "pressure")
        {
            field.src = extraction::source::Pressure{};
            // Pressure has an offset of the reference pressure
            field.noffsets = 1;
            field.offset = {sim_info.fluid.reference_pressure_Pa};
        }
        else if (type == "velocity")
        {
            field.src = extraction::source::Velocity{};
        }
        else if (type == "vonmisesstress")
        {
            field.src = extraction::source::VonMisesStress{};
        }
        else if (type == "shearstress")
        {
            field.src = extraction::source::ShearStress{};
        }
        else if (type == "shearrate")
        {
            field.src = extraction::source::ShearRate{};
        }
        else if (type == "stresstensor")
        {
            field.src = extraction::source::StressTensor{};
        }
        else if (type == "traction")
        {
            field.src = extraction::source::Traction{};
        }
        else if (type == "tangentialprojectiontraction")
        {
            field.src = extraction::source::TangentialProjectionTraction{};
        }
        else if (type == "normalprojectiontraction")
        {
            field.src = extraction::source::NormalProjectionTraction{};
        }
        else if (type == "wallextension")
        {
            field.src = extraction::source::WallExtension{};
        }
        else if (type == "distributions")
        {
            field.src = extraction::source::Distributions{};
        }
        else if (type == "mpirank")
        {
            field.src = extraction::source::MpiRank{};
            field.typecode = int{0};
        }
        else
        {
            throw Exception() << "Unrecognised field type '" << type << "' in " << fieldEl.GetFullPath();
        }
        if (auto datatype = fieldEl.PopAttributeMaybe("datatype")) {
            if (*datatype == "float") field.typecode = float{0};
            else if (*datatype == "double") field.typecode = double{0};
            else if (*datatype == "int32") field.typecode = std::int32_t{0};
            else if (*datatype == "uint32") field.typecode = std::uint32_t{0};
            else if (*datatype == "int64") field.typecode = std::int64_t{0};
            else if (*datatype == "uint64") field.typecode = std::uint64_t{0};
            else throw Exception() << "Unknown extraction datatype: " << *datatype;
        }
        return field;
    }

    void SimConfigReader::DoIOForBaseInOutlet(GlobalSimInfo const& sim_info, Element& ioletEl, IoletConfigBase& ioletConf) const
    {
        auto positionEl = ioletEl.PopChildOrThrow("position");
        auto normalEl = ioletEl.PopChildOrThrow("normal");

        PopDimensionalValue(*positionEl, "m", ioletConf.position);
        PopDimensionalValue(*normalEl, "dimensionless", ioletConf.normal);

        if (sim_info.time.warmup_steps) {
            ioletConf.warmup_steps = sim_info.time.warmup_steps;
        }

        // Optional element <flowextension>
        if (auto flowEl = ioletEl.PopChildOrNull("flowextension")) {
            FlowExtensionConfig flowConf;
            PopDimensionalValue(*flowEl->PopChildOrThrow("length"), "m", flowConf.length_m);
            PopDimensionalValue(*flowEl->PopChildOrThrow("radius"), "m", flowConf.radius_m);

            // Optional element - default is length of flowext
            // <fadelength ioletConf="float" units="m" />
            flowConf.fadelength_m =
                    PopDimensionalValueWithDefault<PhysicalDistance>(*flowEl, "fadelength", "m", flowConf.length_m);

            // Infer normal and position from inlet
            // However, normals point in *opposite* direction, and, as a flowConf, origin are at opposite
            // end of the cylinder
            flowConf.normal = -ioletConf.normal;
            flowConf.normal.Normalise();

            flowConf.origin_m = ioletConf.position - flowConf.normal * flowConf.length_m;
            ioletConf.flow_extension = flowConf;
            CheckEmpty(*flowEl);
        }

        // Optional element(s) <insertcell>
        for (auto insertEl: ioletEl.PopChildren("insertcell")) {
            CellInserterConfig inserterConf;
            inserterConf.seed = insertEl.PopChildOrThrow("seed")->PopAttributeOrThrow<PrngSeedType>("value");
            inserterConf.template_name = insertEl.PopAttributeOrThrow("template");

            // Rotate cell to align z axis with given position, and then z axis with flow
            // If phi == 0, then cell symmetry axis is aligned with the flow
            inserterConf.theta_rad = PopDimensionalValueWithDefault<Angle>(insertEl, "theta", "rad", 0e0);
            inserterConf.phi_rad = PopDimensionalValueWithDefault<Angle>(insertEl, "phi", "rad", 0e0);
            inserterConf.translation_m = {
                    PopDimensionalValueWithDefault<PhysicalDistance>(insertEl, "x", "m", 0e0),
                    PopDimensionalValueWithDefault<PhysicalDistance>(insertEl, "y", "m", 0e0),
                    PopDimensionalValueWithDefault<PhysicalDistance>(insertEl, "z", "m", 0e0)
            };

            inserterConf.offset = PopDimensionalValueWithDefault<
                    quantity_union<double, "s", "lattice">
            >(insertEl, "offset", quantity<double, "s">(0));
            inserterConf.drop_period_s = PopDimensionalValue<PhysicalTime>(
                    *insertEl.PopChildOrThrow("every"), "s");
            inserterConf.dt_s = PopDimensionalValueWithDefault<PhysicalTime>(
                    insertEl, "delta_t", "s", 0e0);

            inserterConf.dtheta_rad = PopDimensionalValueWithDefault<Angle>(insertEl,
                                                                            "delta_theta",
                                                                            "rad",
                                                                            0e0);
            inserterConf.dphi_rad = PopDimensionalValueWithDefault<Angle>(insertEl,
                                                                          "delta_phi",
                                                                          "rad",
                                                                          0e0);
            inserterConf.dx_m = PopDimensionalValueWithDefault<LatticeDistance>(insertEl,
                                                                                "delta_x",
                                                                                "m",
                                                                                0e0);
            inserterConf.dy_m = PopDimensionalValueWithDefault<LatticeDistance>(insertEl,
                                                                                "delta_y",
                                                                                "m",
                                                                                0e0);
            CheckEmpty(insertEl);

            ioletConf.cell_inserters.push_back(inserterConf);
        }
    }

    template <typename T, typename F>
    auto opt_transform (std::optional<T>&& o, F&& f) {
        using U = std::remove_cv_t<std::invoke_result_t<F, T>>;
        if (o.has_value())
            return std::make_optional<U>(
                    std::invoke(
                            std::forward<F>(f),
                            std::move(o.value())
                    )
            );
        return std::optional<U>{};
    }

    ICConfig SimConfigReader::DoIOForInitialConditions(Element initialconditionsEl) const
    {
        ICConfig initial_condition;
        // The <time> element may be present - if so, it will set the
        // initial timestep value
        auto t0 = initialconditionsEl.PopChildOrNull("time")->transform([](auto el) {
            return PopDimensionalValue<LatticeTimeStep>(el, "lattice");
        });

        // Exactly one of {<pressure>, <checkpoint>} must be present
        // TODO: use something other than an if-tree
        if (auto pressureEl = initialconditionsEl.PopChildOrNull("pressure")) {
            if (auto checkpointEl = initialconditionsEl.PopChildOrNull("checkpoint")) {
                // Both are present - this is an error
                throw Exception()
                        << "XML contains both <pressure> and <checkpoint> sub elements of <initialconditions>";
            } else {
                // Only pressure
                auto p0_Pa = PopPressureValue(*pressureEl->PopChildOrThrow("uniform"));
                initial_condition = EquilibriumIC(t0, p0_Pa);
                CheckEmpty(*pressureEl);
            }
        } else {
            if (auto checkpointEl = initialconditionsEl.PopChildOrNull("checkpoint")) {
                // Only checkpoint
                initial_condition = CheckpointIC(
                        t0,
                        RelPathToFullPath(checkpointEl->PopAttributeOrThrow("file")),
                        opt_transform(
                                checkpointEl->PopAttributeMaybe("offsets"),
                                [&](std::string_view sv) { return RelPathToFullPath(sv); }
                        )
                );
                CheckEmpty(*checkpointEl);
            } else {
                // No IC!
                throw Exception() << "XML <initialconditions> element contains no known initial condition type";
            }
        }
        CheckEmpty(initialconditionsEl);
        return initial_condition;
    }

    auto SimConfigReader::DoIOForCosinePressureInOutlet(Element& conditionEl) const -> IoletConfig
    {
        CosinePressureIoletConfig newIolet;

        newIolet.amp_Pa = PopPressureValue(*conditionEl.PopChildOrThrow("amplitude"));
        newIolet.mean_Pa = PopPressureValue(*conditionEl.PopChildOrThrow("mean"));
        newIolet.phase_rad = PopDimensionalValue<Angle>(*conditionEl.PopChildOrThrow("phase"), "rad");
        newIolet.period_s = PopDimensionalValue<LatticeTime>(*conditionEl.PopChildOrThrow("period"), "s");
        CheckEmpty(conditionEl);
        return newIolet;
    }

    auto SimConfigReader::DoIOForFilePressureInOutlet(Element& conditionEl) const -> IoletConfig
    {
        FilePressureIoletConfig newIolet;
        auto pathEl = conditionEl.PopChildOrThrow("path");
        newIolet.file_path = RelPathToFullPath(pathEl->PopAttributeOrThrow("value"));
        const auto units = conditionEl.PopAttributeMaybe("units").value_or("Pa");
        if (units != "Pa" && units != "mmHg") throw Exception() << "Invalid pressure file units: " << units;
        newIolet.file_mmHg = units == "mmHg";
        auto timing = conditionEl.PopAttributeMaybe("timing").value_or("stretch");
        if (timing != "stretch" && timing != "periodic")
            throw Exception() << "Pressure file timing must be stretch or periodic";
        newIolet.periodic = timing == "periodic";
        CheckEmpty(*pathEl);
        return newIolet;
    }

    auto SimConfigReader::DoIOForMultiscalePressureInOutlet(Element& conditionEl) const -> IoletConfig
    {
        MultiscalePressureIoletConfig newIolet;

        auto pressureEl = conditionEl.PopChildOrThrow("pressure");
        newIolet.pressure_reference_Pa = PopPressureValue(*pressureEl);

        auto velocityEl = conditionEl.PopChildOrThrow("velocity");
        PopDimensionalValue(*velocityEl, "m/s", newIolet.velocity_reference_ms);

        newIolet.label = conditionEl.PopChildOrThrow("label")->PopAttributeOrThrow("value");
        return newIolet;
    }

    auto SimConfigReader::DoIOForParabolicVelocityInOutlet(
            Element& conditionEl) const -> IoletConfig
    {
        ParabolicVelocityIoletConfig newIolet;

        PopDimensionalValue(*conditionEl.PopChildOrThrow("radius"), "m", newIolet.radius_m);
        PopDimensionalValue(*conditionEl.PopChildOrThrow("maximum"), "m/s", newIolet.max_speed_ms);
        CheckEmpty(conditionEl);
        return newIolet;
    }

    auto SimConfigReader::DoIOForWomersleyVelocityInOutlet(Element& conditionEl) const -> IoletConfig
    {
        WomersleyVelocityIoletConfig newIolet;

        PopDimensionalValue(*conditionEl.PopChildOrThrow("radius"), "m", newIolet.radius_m);
        newIolet.pgrad_amp_Pam = PopPressureValue(
            *conditionEl.PopChildOrThrow("pressure_gradient_amplitude"), true);
        PopDimensionalValue(*conditionEl.PopChildOrThrow("period"), "s", newIolet.period_s);
        PopDimensionalValue(*conditionEl.PopChildOrThrow("womersley_number"), "dimensionless", newIolet.womersley);
        CheckEmpty(conditionEl);
        return newIolet;
    }

    auto SimConfigReader::DoIOForFileVelocityInOutlet(Element& conditionEl) const -> IoletConfig
    {
        FileVelocityIoletConfig newIolet;
        auto timing = conditionEl.PopAttributeMaybe("timing").value_or("legacy");
        if (timing != "legacy" && timing != "periodic")
            throw Exception() << "Velocity file timing must be legacy or periodic";
        newIolet.periodic = timing == "periodic";

        newIolet.file_path = RelPathToFullPath(conditionEl.PopChildOrThrow("path")->PopAttributeOrThrow("value"));
        PopDimensionalValue(*conditionEl.PopChildOrThrow("radius"), "m", newIolet.radius_m);
        CheckEmpty(conditionEl);
        return newIolet;
    }


    MonitoringConfig SimConfigReader::DoIOForMonitoring(Element& monEl) const
    {
        MonitoringConfig monitoringConfig;
        if (auto convEl = monEl.PopChildOrNull("steady_flow_convergence"))
        {
            DoIOForSteadyFlowConvergence(*convEl, monitoringConfig);
        }

        monitoringConfig.doIncompressibilityCheck = (monEl.PopChildOrNull("incompressibility")
                                                     != Element::Missing());
        CheckEmpty(monEl);
        return monitoringConfig;
    }

    void SimConfigReader::DoIOForSteadyFlowConvergence(Element& convEl, MonitoringConfig& monitoringConfig) const
    {
        monitoringConfig.doConvergenceCheck = true;
        monitoringConfig.convergenceRelativeTolerance = convEl.PopAttributeOrThrow<double>("tolerance");
        monitoringConfig.convergenceTerminate = (convEl.PopAttributeOrThrow("terminate") == "true");

        auto n = 0;
        for (auto criterionEl: convEl.PopChildren("criterion")) {
            DoIOForConvergenceCriterion(criterionEl, monitoringConfig);
            n++;
        }
        if (n == 0) {
            throw Exception() << "At least one convergence criterion must be provided in "
                              << convEl.GetFullPath();
        }
        CheckEmpty(convEl);
    }

    void SimConfigReader::DoIOForConvergenceCriterion(Element& criterionEl, MonitoringConfig& monitoringConfig) const
    {
        auto criterionType = criterionEl.PopAttributeOrThrow("type");

        // We only allow velocity-based convergence check for the time being
        if (criterionType != "velocity")
        {
            throw Exception() << "Invalid convergence criterion type " << criterionType << " in "
                              << criterionEl.GetFullPath();
        }
        monitoringConfig.convergenceVariable = extraction::source::Velocity{};
        monitoringConfig.convergenceReferenceValue = PopDimensionalValue<PhysicalSpeed>(criterionEl, "m/s");
    }

    TemplateCellConfig SimConfigReader::readCell(Element& cellNode) const {
        TemplateCellConfig ans;
        ans.name = cellNode.PopAttributeMaybe("name").value_or("default");
        auto shape = cellNode.PopChildOrThrow("shape");
        ans.mesh_path = RelPathToFullPath(
                shape->PopAttributeOrThrow("mesh_path")
        );

        auto get_fmt = [](Element& shape, char const* attr_name) -> MeshFormat {
            auto fmt = shape.PopAttributeOrThrow(attr_name);
            if (fmt == "VTK") {
                return VTKMeshFormat{};
            } else if (fmt == "Krueger") {
                log::Logger::Log<log::Warning, log::Singleton>("Krueger format meshes are deprecated, move to VTK when you can.");
                return KruegerMeshFormat{};
            }
            throw Exception() << "Invalid " << attr_name << " '" << fmt << "' on element " << shape.GetFullPath();
        };

        ans.format = get_fmt(*shape, "mesh_format");

        ans.scale_m = PopDimensionalValue<PhysicalDistance>(*cellNode.PopChildOrThrow("scale"), "m");

        if (auto reference_mesh_path = shape->PopAttributeMaybe("reference_mesh_path")) {
            ans.reference_mesh_path = RelPathToFullPath(*reference_mesh_path);
            ans.reference_mesh_format = get_fmt(*shape, "reference_mesh_format");
        }
        CheckEmpty(*shape);

        auto moduliNode = cellNode.PopChildOrNull("moduli");
        ans.moduli.bending_Nm = PopDimensionalValueWithDefault<PhysicalModulus>(
                *moduliNode, "bending", "Nm", 2e-19);
        ans.moduli.surface_lat = PopDimensionalValueWithDefault<LatticeModulus>(
                *moduliNode, "surface", "lattice", 1e0);
        ans.moduli.volume_lat = PopDimensionalValueWithDefault<LatticeModulus>(
                *moduliNode, "volume", "lattice", 1e0);
        ans.moduli.dilation_lat = PopDimensionalValueWithDefault<LatticeModulus>(
                *moduliNode, "dilation", "lattice", 0.75);
        if (1e0 < ans.moduli.dilation_lat or ans.moduli.dilation_lat < 0.5)
        {
            log::Logger::Log<log::Critical, log::Singleton>("Dilation modulus is outside the recommended range 1e0 >= m >= 0.5");
        }
        ans.moduli.strain_Npm = PopDimensionalValueWithDefault<PhysicalModulus>(
                *moduliNode, "strain", "N/m", 5e-6);
        return ans;
    }

    std::map<std::string, TemplateCellConfig> SimConfigReader::readTemplateCells(Element& cellsEl) const {
        std::map<std::string, TemplateCellConfig> ans;
        for (auto cellNode: cellsEl.PopChildren("cell"))
        {
            auto const key = std::string{cellNode.GetAttributeMaybe("name").value_or("default")};
            if (ans.contains(key))
                throw Exception() << "Multiple template mesh with same name: " << key;

            ans[key] = readCell(cellNode);
            CheckEmpty(cellNode);
        }
        return ans;
    }

    NodeForceConfig readNode2NodeForce(Element& node) {
        auto intensity = PopDimensionalValueWithDefault<
                quantity_union<double, "Nm", "lattice">
        >(node, "intensity", quantity<double, "Nm">(1.0));

        auto cutoffdist = PopDimensionalValueWithDefault<LatticeDistance>(
                node, "cutoffdistance", "lattice", 1.0);

        // exponent doesnt have units apparently
        auto exponent = node.and_then(
                [](Element& el) {
                    auto expEl = el.PopChildOrNull("exponent");
                    auto val = expEl->PopAttributeOrThrow<std::size_t>("value");
                    CheckEmpty(*expEl);
                    return std::make_optional(val);
                }).value_or(std::size_t(2));

        CheckEmpty(node);
        return {intensity, cutoffdist, exponent};
    }

    RBCConfig SimConfigReader::DoIOForRedBloodCells(SimConfig const& conf, Element &rbcEl) const {
        RBCConfig ans;

        auto controllerNode = rbcEl.PopChildOrThrow("controller");
        ans.boxSize = PopDimensionalValue<LatticeDistance>(*controllerNode->PopChildOrThrow("boxsize"), "lattice");

        if (auto cellsEl = rbcEl.PopChildOrNull("templates"))
            ans.meshes = readTemplateCells(*cellsEl);

        // Now we can check that the cell inserters only refer to templates that exist
        auto cell_inserter_templates_exist = [&] (IoletConfig const& iolet_v) {
            bool ok = true;
            std::visit([&](auto const& iolet) {
                if constexpr(std::is_same_v<std::decay_t<decltype(iolet)>, std::monostate>) {
                    throw Exception() << "invalid iolet";
                } else {
                    for (auto const &ins: iolet.cell_inserters) {
                        if (!ans.meshes.contains(ins.template_name)) {
                            ok = false;
                            log::Logger::Log<log::Error, log::Singleton>(
                                    "Cell inserter for iolet refers to template that does not exist '%s'",
                                    ins.template_name.c_str());
                        }
                    }
                }
            }, iolet_v);
            return ok;
        };
        if (!(std::all_of(conf.inlets.begin(), conf.inlets.end(), cell_inserter_templates_exist)
              &&
              std::all_of(conf.outlets.begin(), conf.outlets.end(), cell_inserter_templates_exist))) {
            throw Exception() << "One or more cell inserters refer to a template that does not exist";
        }

        ans.cell2cell = readNode2NodeForce(*rbcEl.PopChildOrNull("cell2Cell"));
        ans.cell2wall = readNode2NodeForce(*rbcEl.PopChildOrNull("cell2Wall"));
        if (ans.boxSize < ans.cell2wall.cutoffdist)
            throw Exception() << "Box-size < cell-wall interaction size: "
                                 "cell-wall interactions cannot be all accounted for.";

        if (ans.boxSize < ans.cell2cell.cutoffdist)
            throw Exception() << "Box-size < cell-cell interaction size: "
                                 "cell-cell interactions cannot be all accounted for.";

        auto outEl = rbcEl.PopChildOrThrow("output");
        if (auto vtkEl = outEl->PopChildOrNull("vtk")) {
            ans.full_output = CellOutputConfig{
                    .output_period = PopDimensionalValue<LatticeTimeStep>(*vtkEl->PopChildOrThrow("period"), "lattice"),
                    .physical_units = PopDimensionalValue<bool>(*vtkEl->PopChildOrThrow("physical"), "bool")
            };
        }
        if (auto summEl = outEl->PopChildOrNull("summary")) {
            ans.summary_output = CellOutputConfig{
                    .output_period = PopDimensionalValue<LatticeTimeStep>(*summEl->PopChildOrThrow("period"), "lattice"),
                    .physical_units = PopDimensionalValue<bool>(*summEl->PopChildOrThrow("physical"), "bool")
            };
        }

        return ans;
    }
}