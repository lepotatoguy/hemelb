// This file is part of HemeLB and is Copyright (C)
// the HemeLB team and/or their institutions, as detailed in the
// file AUTHORS. This software is provided under the terms of the
// license in the file LICENSE.

#include <catch2/catch.hpp>

#include "net/mpi.h"
#include "net/net.h"

namespace hemelb
{
  namespace tests
  {
    using namespace hemelb::net;
    /**
     * Unittests of the MPI abstraction layer
     *
     * This is hard to test with a single task....
     */
    TEST_CASE("MpiTests") {
      MpiCommunicator commNull;
      REQUIRE(!commNull);

      MpiCommunicator commWorld = MpiCommunicator::World();
      REQUIRE(commWorld);

      REQUIRE(commNull== commNull);
      REQUIRE(commWorld == commWorld);
      REQUIRE(commWorld != commNull);

      SECTION("Copy assignment works") {
	MpiCommunicator commWorld2 = commWorld;
	REQUIRE(commWorld2 == commWorld);
      }

      SECTION("World factor returns objects that compare equal") {
	MpiCommunicator commWorld2 = MpiCommunicator::World();
	REQUIRE(commWorld2 == commWorld);
      }

      SECTION("Comms with the same group are different") {
	MpiGroup groupWorld = commWorld.Group();
	MpiCommunicator commWorld2 = commWorld.Create(groupWorld);
	// Same ranks, but different context.
	REQUIRE(commWorld2 != commWorld);
      }
    }

    TEST_CASE("MPI gather handles empty vectors", "[net]") {
      auto comm = MpiCommunicator::World();
      Net net(comm);
      std::vector<int> empty;
      std::vector<int> received;
      std::vector<int> counts(comm.Size(), 0);
      net.RequestGatherVSend(empty, 0);
      if (comm.Rank() == 0)
        net.RequestGatherVReceive(received, counts);
      net.Dispatch();
      if (comm.Rank() == 0)
        REQUIRE(received.empty());

      int value = comm.Rank();
      std::vector<int> scalarReceived;
      net.RequestGatherSend(value, 0);
      if (comm.Rank() == 0)
        net.RequestGatherReceive(scalarReceived);
      net.Dispatch();
      if (comm.Rank() == 0) {
        REQUIRE(scalarReceived.size() == static_cast<std::size_t>(comm.Size()));
        for (int rank = 0; rank < comm.Size(); ++rank)
          REQUIRE(scalarReceived[rank] == rank);
      }

      REQUIRE_THROWS_WITH(net.RequestAllToAllSend(empty),
                          Catch::Matchers::Contains("must match communicator size"));
      REQUIRE_THROWS_WITH(net.RequestAllToAllReceive(empty),
                          Catch::Matchers::Contains("must match communicator size"));
    }
  }
}
