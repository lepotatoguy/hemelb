// This file is part of HemeLB and is Copyright (C)
// the HemeLB team and/or their institutions, as detailed in the
// file AUTHORS. This software is provided under the terms of the
// license in the file LICENSE.

#ifndef HEMELB_NET_MIXINS_INTERFACEDELEGATIONNET_H
#define HEMELB_NET_MIXINS_INTERFACEDELEGATIONNET_H

#include <limits>
#include "Exception.h"

namespace hemelb
{
  namespace net
  {
    /***
     * Define the external template-based interface to be used by client classes.
     * We define a series of templates with nice C++ style interfaces which delegate to C-style template interfaces
     * And then, delegate the templated C-style interfaces to nontemplated interfaces taking an MPI datatype.
     */
    class InterfaceDelegationNet : public virtual BaseNet
    {
      public:
        InterfaceDelegationNet(const MpiCommunicator& comms) :
            BaseNet(comms)
        {
        }

        template<class T>
        void RequestSendV(std::span<const T> payload, proc_t toRank)
        {
          RequestSend(payload.data(), payload.size(), toRank);
        }

        template<class T>
        void RequestSendR(T const& value, proc_t toRank)
        {
          RequestSend(&value, 1, toRank);
        }

        template<class T>
        void RequestReceiveR(T& value, proc_t fromRank)
        {
          RequestReceive(&value, 1, fromRank);
        }

        template<class T>
        void RequestReceiveV(std::span<T> payload, proc_t toRank)
        {
          RequestReceive(payload.data(), payload.size(), toRank);
        }

        template<class T>
        void RequestGatherVReceive(std::vector<T>& bigBuffer, const std::vector<int>& countsIn)
        {
          int totalCount = 0;
          if (countsIn.size() != static_cast<std::size_t>(Size()))
            throw Exception() << "Gather receive counts must match communicator size";
          for (int count: countsIn) {
            if (count < 0 || count > std::numeric_limits<int>::max() - totalCount)
              throw Exception() << "Gather receive count is invalid or too large";
            totalCount += count;
          }

          bigBuffer.resize(totalCount);

          std::vector<int> & displacements = this->GetDisplacementsBuffer();
          std::vector<int> &counts = this->GetCountsBuffer();

          int countSoFar = 0;
          for (int nextCount: countsIn) {
            counts.push_back(nextCount);
            displacements.push_back(countSoFar);
            countSoFar += nextCount;
          }

          RequestGatherVReceive(bigBuffer.data(), displacements.data(), counts.data());
        }

        template<class T>
        void RequestGatherReceive(std::vector<T> &buffer)
        {
          buffer.resize(Size());
          RequestGatherReceive(buffer.data());
        }

        template<class T>
        void RequestGatherSend(T& value, proc_t toRank)
        {
          RequestGatherSend(&value, toRank);
        }

        template<class T>
        void RequestGatherVSend(std::vector<T> &payload, proc_t toRank)
        {
          if (payload.size() > std::size_t(std::numeric_limits<int>::max()))
            throw Exception() << "Gather send count is too large";
          RequestGatherVSend(payload.data(), static_cast<int>(payload.size()), toRank);
        }

        /***
         * This is for a scalar all to all
         * @param buffer vector with length same as communicator size
         */
        template<class T>
        void RequestAllToAllSend(std::vector<T> &buffer)
        {
          if (buffer.size() != static_cast<std::size_t>(Size()))
            throw Exception() << "All-to-all send buffer must match communicator size";
          RequestAllToAllSend(buffer.data(), 1);
        }
        /***
         * This is for a scalar all to all
         * @param buffer vector with length same as communicator size
         */
        template<class T>
        void RequestAllToAllReceive(std::vector<T> &buffer)
        {
          if (buffer.size() != static_cast<std::size_t>(Size()))
            throw Exception() << "All-to-all receive buffer must match communicator size";
          RequestAllToAllReceive(buffer.data(), 1);
        }

        template<class T>
        void RequestSend(T* pointer, int count, proc_t rank)
        {
          using non_const_t = std::remove_const_t<T>;
          RequestSendImpl(pointer, count, rank, MpiDataType<non_const_t>());
        }

        template<class T>
        void RequestReceive(T* pointer, int count, proc_t rank)
        {
          RequestReceiveImpl(pointer, count, rank, MpiDataType<T>());
        }

        /*
         * Blocking gathers are implemented in MPI as a single call for both send/receive
         * But, here we separate send and receive parts, since this interface may one day be used for
         * nonblocking collectives.
         */

        template<class T>
        void RequestGatherVSend(T* buffer, int count, proc_t toRank)
        {
          RequestGatherVSendImpl(buffer, count, toRank, MpiDataType<T>());
        }

        template<class T>
        void RequestGatherReceive(T* buffer)
        {
          RequestGatherReceiveImpl(buffer, MpiDataType<T>());
        }

        template<class T>
        void RequestGatherSend(T* buffer, proc_t toRank)
        {
          RequestGatherSendImpl(buffer, toRank, MpiDataType<T>());
        }

        template<class T>
        void RequestGatherVReceive(T* buffer, int * displacements, int *counts)
        {
          RequestGatherVReceiveImpl(buffer, displacements, counts, MpiDataType<T>());
        }

        template<class T>
        void RequestAllToAllSend(T* buffer, int count)
        {
          RequestAllToAllSendImpl(buffer, count, MpiDataType<T>());
        }
        template<class T>
        void RequestAllToAllReceive(T* buffer, int count)
        {
          RequestAllToAllReceiveImpl(buffer, count, MpiDataType<T>());
        }

    }
    ;
  }
}
#endif
