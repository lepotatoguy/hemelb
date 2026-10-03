// This file is part of HemeLB and is Copyright (C)
// the HemeLB team and/or their institutions, as detailed in the
// file AUTHORS. This software is provided under the terms of the
// license in the file LICENSE.

#include <memory>
#include <fstream>

#include <catch2/catch.hpp>

#include "configuration/SimConfig.h"
#include "configuration/SimConfigWriter.h"
#include "configuration/SimConfigReader.h"
#include "configuration/SimBuilder.h"
#include "lb/iolets/InOutLetFile.h"
#include "resources/Resource.h"
#include "tests/helpers/FolderTestFixture.h"
#include "tests/helpers/LaddFail.h"

namespace hemelb::tests
{
    using namespace configuration;
    namespace
    {
      class SyntaxReader : public SimConfigReader
      {
      public:
          using SimConfigReader::SimConfigReader;
          void CheckIoletMatchesCMake(const io::xml::Element&, std::string_view) const override {}
      };
      // For section XMLFileContent
      struct CfgChecker {
	using result_type = bool;
	template<typename T>
	bool operator()(T) const {
	  return false;
	}
	bool operator()(const EquilibriumIC& eqIC) const {
	  return 80.0 == eqIC.p_Pa;
	}
      };
    }
    TEST_CASE_METHOD(helpers::FolderTestFixture, "SimConfig") {

      SECTION("0_2_0_Read") {
	LADD_FAIL();
	// smoke test the configuration as having loaded OK
	auto config = SimConfig::New(resources::Resource("config0_2_0.xml").Path());
	REQUIRE(3000lu == config.GetTotalTimeSteps());
	REQUIRE(0.0001 == config.GetTimeStepLength());
    auto inlet = std::get_if<configuration::CosinePressureIoletConfig>(&config.GetInlets()[0]);
	//auto inlet = util::clone_dynamic_cast<lb::iolets::InOutLetCosine>(config->GetInlets()[0]);

	REQUIRE(inlet != nullptr);
	REQUIRE(Approx(0.6) == inlet->period_s);

	// Check that in the absence of the <monitoring> XML element things get initiliased properly
	auto& monConfig = config.GetMonitoringConfiguration();
	REQUIRE(!monConfig.doConvergenceCheck);
	REQUIRE(!monConfig.doIncompressibilityCheck);
	REQUIRE(!monConfig.convergenceTerminate);
	REQUIRE(Approx(0.0) == monConfig.convergenceRelativeTolerance);
      }

      SECTION("0_2_1_Read") {
	LADD_FAIL();
	// smoke test the configuration as having loaded OK
	auto config = SimConfig::New(resources::Resource("config.xml").Path());
	REQUIRE(3000lu == config.GetTotalTimeSteps());
	REQUIRE(0.0001 == config.GetTimeStepLength());
	auto inlet = std::get_if<configuration::CosinePressureIoletConfig>(&config.GetInlets()[0]);
	REQUIRE(inlet != nullptr);
	REQUIRE(Approx(0.6) == inlet->period_s);

	auto& monConfig = config.GetMonitoringConfiguration();
	REQUIRE(monConfig.doConvergenceCheck);
	REQUIRE(monConfig.doIncompressibilityCheck);
	REQUIRE(monConfig.convergenceTerminate);
	REQUIRE(1e-9 == monConfig.convergenceRelativeTolerance);
	REQUIRE(std::holds_alternative<extraction::source::Velocity>(monConfig.convergenceVariable));
	REQUIRE(1 == monConfig.convergenceReferenceValue); // 1 m/s
      }
      
      SECTION("XMLFileContent") {
	LADD_FAIL();
	//Round trip the config twice.
	CopyResourceToTempdir("config.xml");
	auto config = SimConfig::New("config.xml");

	auto& ICconfig = config.GetInitialCondition();
	REQUIRE(std::visit(CfgChecker{}, ICconfig));
      }

    }
    TEST_CASE_METHOD(helpers::FolderTestFixture, "Runtime decomposition method is validated", "[configuration]") {
        CopyResourceToTempdir("config.xml");
        MoveToTempdir();
        REQUIRE(SimConfig::New("config.xml").GetDecompositionMethod() == "parmetis");
        std::ifstream input("config.xml");
        std::string source{std::istreambuf_iterator<char>{input}, {}};
        auto position = source.find("</hemelbsettings>");
        REQUIRE(position != std::string::npos);
        std::ofstream("octree.xml") << source.substr(0, position) << "<decomposition method=\"octree\"/></hemelbsettings>";
        REQUIRE(SimConfig::New("octree.xml").GetDecompositionMethod() == "octree");
        std::ofstream("invalid.xml") << source.substr(0, position) << "<decomposition method=\"invalid\"/></hemelbsettings>";
        REQUIRE_THROWS_WITH(SimConfig::New("invalid.xml"), Catch::Matchers::Contains("Unknown decomposition method"));
    }

    TEST_CASE_METHOD(helpers::FolderTestFixture, "Legacy configs preserve pressure units and checkpoint settings", "[configuration]") {
        const unsigned version = GENERATE(3U, 5U);
        const std::string kind = GENERATE("pressure", "yangpressure");
        io::xml::Document doc(resources::Resource("config.xml").Path());
        auto root = doc.GetRoot();
        root.SetAttribute("version", version);
        auto sim = root.GetChildOrThrow("simulation");
        sim.AddChild("stresstype").SetAttribute("value", 1U);
        auto reference = sim.AddChild("reference_pressure");
        reference.SetAttribute("units", "mmHg"); reference.SetAttribute("value", 2.0);
        auto uniform = root.GetChildOrThrow("initialconditions").GetChildOrThrow("pressure").GetChildOrThrow("uniform");
        uniform.SetAttribute("units", "mmHg"); uniform.SetAttribute("value", 3.0);
        auto inlet = root.GetChildOrThrow("inlets").GetChildOrThrow("inlet");
        auto condition = inlet.GetChildOrThrow("condition");
        for (auto name: {"amplitude", "mean", "phase", "period"}) condition.GetChildOrThrow(name).Delete();
        condition.SetAttribute("subtype", "file");
        condition.SetAttribute("type", kind);
        condition.AddChild("path").SetAttribute("value", "pressure.txt");
        if (version == 3) {
            auto position = inlet.GetChildOrThrow("position");
            position.SetAttribute("units", "lattice"); position.SetAttribute("value", "(2,4,6)");
        }
        auto checkpoint = root.AddChild("properties").AddChild("checkpoint");
        checkpoint.SetAttribute("file", "old-%d.xtr"); checkpoint.SetAttribute("period", 10U);
        doc.SaveFile("legacy.xml");
        std::ofstream("pressure.txt") << "0 3\n1 4\n2 3\n";
        auto config = kind == "yangpressure" ? SyntaxReader("legacy.xml").Read()
                                             : SimConfig::New("legacy.xml");
        REQUIRE(Approx(3 * mmHg_TO_PASCAL) == std::get<EquilibriumIC>(config.GetInitialCondition()).p_Pa);
        REQUIRE(Approx(2 * mmHg_TO_PASCAL) == config.GetSimInfo().fluid.reference_pressure_Pa);
        const auto& file = std::get<FilePressureIoletConfig>(config.GetInlets()[0]);
        REQUIRE(file.file_mmHg);
        if (version == 3) REQUIRE(file.position == PhysicalPosition(-0.98, -1.96, -2.94));
        const auto& output = config.GetPropertyOutputs()[0];
        REQUIRE(output.filename == "old-%d.xtr");
        REQUIRE(output.frequency == 10);
        REQUIRE(std::holds_alternative<double>(output.fields[0].typecode));
        SimBuilder builder(config);
        auto iolet = builder.BuildIolet(config.GetInlets()[0]);
        iolet->Initialise(builder.GetUnitConverter().get());
        REQUIRE(Approx(builder.GetUnitConverter()->ConvertPressureToLatticeUnits(3 * mmHg_TO_PASCAL) / Cs2) == iolet->GetDensityMin());
        SimConfigWriter("roundtrip.xml").Write(config);
        auto roundtrip = SimConfig::New("roundtrip.xml");
        REQUIRE(std::get<FilePressureIoletConfig>(roundtrip.GetInlets()[0]).file_mmHg);
        REQUIRE(std::holds_alternative<double>(roundtrip.GetPropertyOutputs()[0].fields[0].typecode));
        REQUIRE(io::xml::Document("legacy.xml").GetRoot().GetAttributeOrThrow<unsigned>("version") == version);
    }

    TEST_CASE_METHOD(helpers::FolderTestFixture, "Pressure XML6 uses mmHg and preserves explicit Pa inputs", "[configuration]") {
        const std::string units = GENERATE("Pa", "mmHg");
        const double scale = units == "mmHg" ? mmHg_TO_PASCAL : 1.0;
        io::xml::Document doc(resources::Resource("config.xml").Path());
        auto root = doc.GetRoot();
        auto reference = root.GetChildOrThrow("simulation").AddChild("reference_pressure");
        reference.SetAttribute("units", units); reference.SetAttribute("value", 2.0);
        auto uniform = root.GetChildOrThrow("initialconditions").GetChildOrThrow("pressure").GetChildOrThrow("uniform");
        uniform.SetAttribute("units", units); uniform.SetAttribute("value", 3.0);
        auto inlet = root.GetChildOrThrow("inlets").GetChildOrThrow("inlet").GetChildOrThrow("condition");
        for (auto name: {"amplitude", "mean"}) {
            auto pressure = inlet.GetChildOrThrow(name);
            pressure.SetAttribute("units", units); pressure.SetAttribute("value", 4.0);
        }
        doc.SaveFile("pressure.xml");
        const auto config = SyntaxReader("pressure.xml").Read();
        REQUIRE(std::get<EquilibriumIC>(config.GetInitialCondition()).p_Pa == Approx(3 * scale));
        REQUIRE(config.GetSimInfo().fluid.reference_pressure_Pa == Approx(2 * scale));
        REQUIRE(std::get<CosinePressureIoletConfig>(config.GetInlets()[0]).mean_Pa == Approx(4 * scale));
        REQUIRE(std::get<CosinePressureIoletConfig>(config.GetInlets()[0]).amp_Pa == Approx(4 * scale));
        SimConfigWriter("pressure-out.xml").Write(config);
        io::xml::Document written("pressure-out.xml");
        auto output = written.GetRoot().GetChildOrThrow("inlets").GetChildOrThrow("inlet").GetChildOrThrow("condition").GetChildOrThrow("mean");
        REQUIRE(output.GetAttributeOrThrow("units") == "mmHg");
        REQUIRE(output.GetAttributeOrThrow<double>("value") == Approx(4 * scale / mmHg_TO_PASCAL));
        const auto roundtrip = SyntaxReader("pressure-out.xml").Read();
        REQUIRE(std::get<EquilibriumIC>(roundtrip.GetInitialCondition()).p_Pa == Approx(3 * scale));
        REQUIRE(roundtrip.GetSimInfo().fluid.reference_pressure_Pa == Approx(2 * scale));
        REQUIRE(std::get<CosinePressureIoletConfig>(roundtrip.GetInlets()[0]).mean_Pa == Approx(4 * scale));
    }

    TEST_CASE_METHOD(helpers::FolderTestFixture, "Pressure gradients accept conventional units without rescaling elastic modulus", "[configuration]") {
        const std::string subtype = GENERATE("womersley", "womersleyElastic");
        const std::string units = GENERATE("Pa/m", "mmHg/m");
        const double scale = units == "mmHg/m" ? mmHg_TO_PASCAL : 1.0;
        io::xml::Document doc(resources::Resource("config.xml").Path());
        auto condition = doc.GetRoot().GetChildOrThrow("inlets").GetChildOrThrow("inlet").GetChildOrThrow("condition");
        for (auto name: {"amplitude", "mean", "phase", "period"})
            condition.GetChildOrThrow(name).Delete();
        condition.SetAttribute("type", "velocity"); condition.SetAttribute("subtype", subtype);
        auto quantity = [&](char const* name, char const* unit, double value) {
            auto child = condition.AddChild(name);
            child.SetAttribute("units", unit); child.SetAttribute("value", value);
        };
        quantity("radius", "m", 0.01);
        quantity("pressure_gradient_amplitude", units.c_str(), 2.0);
        quantity("period", "s", 1.0);
        quantity("womersley_number", "dimensionless", 4.0);
        if (subtype == "womersleyElastic") {
            quantity("youngs_modulus", "Pa", 10000.0);
            quantity("poisson_ratio", "dimensionless", 0.3);
            quantity("axial_position", "m", 0.0);
        }
        doc.SaveFile("gradient.xml");
        const auto config = SyntaxReader("gradient.xml").Read();
        if (subtype == "womersley")
            REQUIRE(std::get<WomersleyVelocityIoletConfig>(config.GetInlets()[0]).pgrad_amp_Pam == Approx(2 * scale));
        else {
            const auto& elastic = std::get<ElasticWomersleyVelocityIoletConfig>(config.GetInlets()[0]);
            REQUIRE(elastic.pgrad_amp_Pam == Approx(2 * scale));
            REQUIRE(elastic.youngs_modulus_Pa == 10000.0);
        }
        SimConfigWriter("gradient-out.xml").Write(config);
        io::xml::Document output("gradient-out.xml");
        auto outputCondition = output.GetRoot().GetChildOrThrow("inlets").GetChildOrThrow("inlet").GetChildOrThrow("condition");
        auto gradient = outputCondition.GetChildOrThrow("pressure_gradient_amplitude");
        REQUIRE(gradient.GetAttributeOrThrow("units") == "mmHg/m");
        REQUIRE(gradient.GetAttributeOrThrow<double>("value") == Approx(2 * scale / mmHg_TO_PASCAL));
        const auto roundtrip = SyntaxReader("gradient-out.xml").Read();
        if (subtype == "womersley")
            REQUIRE(std::get<WomersleyVelocityIoletConfig>(roundtrip.GetInlets()[0]).pgrad_amp_Pam == Approx(2 * scale));
        else {
            REQUIRE(std::get<ElasticWomersleyVelocityIoletConfig>(roundtrip.GetInlets()[0]).youngs_modulus_Pa == 10000.0);
            condition.GetChildOrThrow("youngs_modulus").SetAttribute("units", "mmHg");
            doc.SaveFile("invalid-modulus.xml");
            REQUIRE_THROWS_WITH(SyntaxReader("invalid-modulus.xml").Read(), Catch::Matchers::Contains("Invalid units"));
        }
    }

    TEST_CASE_METHOD(helpers::FolderTestFixture, "Pressure-file XML6 defaults retain Pa and explicit mmHg", "[configuration]") {
        const std::string units = GENERATE("", "Pa", "mmHg");
        io::xml::Document doc(resources::Resource("config.xml").Path());
        auto condition = doc.GetRoot().GetChildOrThrow("inlets").GetChildOrThrow("inlet").GetChildOrThrow("condition");
        for (auto name: {"amplitude", "mean", "phase", "period"})
            condition.GetChildOrThrow(name).Delete();
        condition.SetAttribute("subtype", "file");
        if (!units.empty()) condition.SetAttribute("units", units);
        condition.AddChild("path").SetAttribute("value", "pressure.txt");
        doc.SaveFile("file.xml");
        const auto config = SyntaxReader("file.xml").Read();
        REQUIRE(std::get<FilePressureIoletConfig>(config.GetInlets()[0]).file_mmHg == (units == "mmHg"));
        SimConfigWriter("file-out.xml").Write(config);
        const auto roundtrip = SyntaxReader("file-out.xml").Read();
        REQUIRE(std::get<FilePressureIoletConfig>(roundtrip.GetInlets()[0]).file_mmHg == (units == "mmHg"));
    }

    TEST_CASE_METHOD(helpers::FolderTestFixture, "Windkessel states write mmHg and retain old Pa state values", "[configuration]") {
        const std::string attribute = GENERATE("pressure_Pa", "pressure_mmHg");
        const double scale = attribute == "pressure_mmHg" ? mmHg_TO_PASCAL : 1.0;
        io::xml::Document doc(resources::Resource("config.xml").Path());
        auto condition = doc.GetRoot().GetChildOrThrow("inlets").GetChildOrThrow("inlet").GetChildOrThrow("condition");
        for (auto name: {"amplitude", "mean", "phase", "period"})
            condition.GetChildOrThrow(name).Delete();
        condition.SetAttribute("subtype", "WK2");
        auto quantity = [&](char const* name, char const* unit, double value) {
            auto child = condition.AddChild(name);
            child.SetAttribute("units", unit); child.SetAttribute("value", value);
        };
        quantity("R", "kg/m^4*s", 1.0);
        quantity("C", "m^4*s^2/kg", 1.0);
        quantity("area", "m^2", 0.01);
        quantity("radius", "m", 0.01);
        auto state = condition.AddChild("state");
        state.SetAttribute(attribute.c_str(), 2.0);
        state.SetAttribute("flow_m3s", 0.1);
        state.SetAttribute("previous_flow_m3s", 0.2);
        doc.SaveFile("windkessel.xml");
        const auto config = SyntaxReader("windkessel.xml").Read();
        REQUIRE(std::get<WindkesselPressureIoletConfig>(config.GetInlets()[0]).pressure_Pa == Approx(2 * scale));
        SimConfigWriter("windkessel-out.xml").Write(config);
        io::xml::Document output("windkessel-out.xml");
        auto outputState = output.GetRoot().GetChildOrThrow("inlets").GetChildOrThrow("inlet").GetChildOrThrow("condition").GetChildOrThrow("state");
        REQUIRE(outputState.GetAttributeOrThrow<double>("pressure_mmHg") == Approx(2 * scale / mmHg_TO_PASCAL));
        REQUIRE_FALSE(outputState.GetAttributeMaybe("pressure_Pa"));
        const auto roundtrip = SyntaxReader("windkessel-out.xml").Read();
        REQUIRE(std::get<WindkesselPressureIoletConfig>(roundtrip.GetInlets()[0]).pressure_Pa == Approx(2 * scale));
        state.SetAttribute("pressure_Pa", 3.0);
        state.SetAttribute("pressure_mmHg", 4.0);
        doc.SaveFile("ambiguous.xml");
        REQUIRE_THROWS(SyntaxReader("ambiguous.xml").Read());
    }

    TEST_CASE_METHOD(helpers::FolderTestFixture, "Multiscale pressure writes conventional units without changing reference velocity", "[configuration]") {
        io::xml::Document doc(resources::Resource("four_cube_multiscale.xml").Path());
        auto pressure = doc.GetRoot().GetChildOrThrow("inlets").GetChildOrThrow("inlet").GetChildOrThrow("condition").GetChildOrThrow("pressure");
        pressure.SetAttribute("units", "mmHg"); pressure.SetAttribute("value", 2.0);
        doc.SaveFile("multiscale.xml");
        const auto config = SyntaxReader("multiscale.xml").Read();
        const auto& original = std::get<MultiscalePressureIoletConfig>(config.GetInlets()[0]);
        REQUIRE(original.pressure_reference_Pa == Approx(2 * mmHg_TO_PASCAL));
        SimConfigWriter("multiscale-out.xml").Write(config);
        io::xml::Document written("multiscale-out.xml");
        auto writtenPressure = written.GetRoot().GetChildOrThrow("inlets").GetChildOrThrow("inlet").GetChildOrThrow("condition").GetChildOrThrow("pressure");
        REQUIRE(writtenPressure.GetAttributeOrThrow<std::string>("units") == "mmHg");
        REQUIRE(writtenPressure.GetAttributeOrThrow<double>("value") == Approx(2.0));
        const auto roundtrip = SyntaxReader("multiscale-out.xml").Read();
        const auto& actual = std::get<MultiscalePressureIoletConfig>(roundtrip.GetInlets()[0]);
        REQUIRE(actual.pressure_reference_Pa == Approx(original.pressure_reference_Pa));
        REQUIRE(actual.velocity_reference_ms == original.velocity_reference_ms);
    }

}
