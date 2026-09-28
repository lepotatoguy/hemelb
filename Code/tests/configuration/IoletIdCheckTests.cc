// This file is part of HemeLB and is Copyright (C)
// the HemeLB team and/or their institutions, as detailed in the
// file AUTHORS. This software is provided under the terms of the
// license in the file LICENSE.

#include <catch2/catch.hpp>

#include "configuration/SimBuilder.h"
#include "Exception.h"
#include "tests/helpers/FourCubeBasedTestFixture.h"

namespace hemelb::tests
{
    // The four-cube test domain has inlet sites with id 0 and outlet sites
    // with id 0, so it needs at least one inlet and one outlet.
    TEST_CASE_METHOD(helpers::FourCubeBasedTestFixture<>, "Geometry iolet ids are checked against the configuration", "[configuration]") {
        using configuration::CheckIoletIds;

        SECTION("Enough inlets and outlets") {
            REQUIRE_NOTHROW(CheckIoletIds(*dom, 1, 1, Comms()));
            REQUIRE_NOTHROW(CheckIoletIds(*dom, 3, 2, Comms()));
        }

        SECTION("Missing inlet") {
            REQUIRE_THROWS_WITH(CheckIoletIds(*dom, 0, 1, Comms()),
                                Catch::Contains("uses 1 inlet(s) but the configuration defines 0"));
        }

        SECTION("Missing outlet") {
            REQUIRE_THROWS_WITH(CheckIoletIds(*dom, 1, 0, Comms()),
                                Catch::Contains("uses 1 outlet(s) but the configuration defines 0"));
        }
    }
}
