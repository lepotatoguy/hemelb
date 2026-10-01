// This file is part of HemeLB and is Copyright (C)
// the HemeLB team and/or their institutions, as detailed in the
// file AUTHORS. This software is provided under the terms of the
// license in the file LICENSE.

#include <memory>
#include <fstream>

#include <catch2/catch.hpp>

#include "configuration/SimConfig.h"
#include "configuration/SimConfigWriter.h"
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
        condition.AddChild("path").SetAttribute("value", "pressure.txt");
        if (version == 3) {
            auto position = inlet.GetChildOrThrow("position");
            position.SetAttribute("units", "lattice"); position.SetAttribute("value", "(2,4,6)");
        }
        auto checkpoint = root.AddChild("properties").AddChild("checkpoint");
        checkpoint.SetAttribute("file", "old-%d.xtr"); checkpoint.SetAttribute("period", 10U);
        doc.SaveFile("legacy.xml");
        std::ofstream("pressure.txt") << "0 3\n1 4\n2 3\n";
        auto config = SimConfig::New("legacy.xml");
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

}
