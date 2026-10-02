// This file is part of HemeLB and is Copyright (C)
// the HemeLB team and/or their institutions, as detailed in the
// file AUTHORS. This software is provided under the terms of the
// license in the file LICENSE.

#ifndef HEMELB_LB_STREAMERS_H
#define HEMELB_LB_STREAMERS_H

#include "build_info.h"

#include "lb/streamers/BulkStreamer.h"
#include "lb/streamers/StreamerTypeFactory.h"
#include "lb/streamers/SimpleBounceBack.h"
#include "lb/streamers/BouzidiFirdaousLallemand.h"
#include "lb/streamers/GuoZhengShi.h"
#include "lb/streamers/GuoZhengShiElasticWall.h"
#include "lb/streamers/JunkYang.h"
#include "lb/streamers/NashZerothOrderPressure.h"
#include "lb/streamers/YangPressure.h"
#include "lb/streamers/LaddIolet.h"

namespace hemelb::lb {
    namespace detail {
        template <typename C>
        constexpr auto get_default_wall_streamer(InitParams& i) {
            constexpr auto WALL = build_info::WALL_BOUNDARY;
            if constexpr (WALL == "BFL") {
                return StreamerTypeFactory < BouzidiFirdaousLallemandLink < C >, NullLink < C >> {i};
            } else if constexpr (WALL == "GZS") {
                return StreamerTypeFactory < GuoZhengShiLink < C >, NullLink < C >> {i};
            }
            else if constexpr (WALL == "GZSElastic")
            {
                return StreamerTypeFactory<GuoZhengShiElasticWallLink<C>, NullLink<C>>{i};
            }
            else if constexpr (WALL == "SIMPLEBOUNCEBACK")
            {
                return StreamerTypeFactory < BounceBackLink < C >, NullLink < C >> {i};
            }
            else if constexpr (WALL == "JUNKYANG")
            {
                return JunkYangFactory<NullLink<C> >{i};
            }
            else
            {
                throw (Exception() << "Configured with invalid WALL_BOUNDARY");
            }
        }

        template <ct_string NAME, typename C>
        constexpr auto get_default_iolet_streamer(InitParams& i) {
            if constexpr (NAME == "NASHZEROTHORDERPRESSUREIOLET") {
                return StreamerTypeFactory<
                        NullLink<C>,
                        NashZerothOrderPressureLink < C >
                >{i};
            }
            else if constexpr (NAME == "YANGPRESSUREIOLET")
            {
                return StreamerTypeFactory<NullLink<C>, YangPressureLink<C>>{i};
            }
            else if constexpr (NAME == "LADDIOLET")
            {
                return StreamerTypeFactory<
                        NullLink<C>,
                        LaddIoletLink < C >
                >{i};
            }
            else
            {
                throw (Exception() << "Configured with invalid IOLET boundary");
            }
        }

    }

    template <typename C>
    using DefaultStreamer = BulkStreamer<C>;

    template <typename C>
    using DefaultWallStreamer = decltype(detail::get_default_wall_streamer<C>(std::declval<InitParams&>()));

    template <typename C>
    using DefaultInletStreamer = decltype(detail::get_default_iolet_streamer<build_info::INLET_BOUNDARY, C>(std::declval<InitParams&>()));

    template <typename C>
    using DefaultOutletStreamer = decltype(detail::get_default_iolet_streamer<build_info::OUTLET_BOUNDARY, C>(std::declval<InitParams&>()));

    // Given wall and iolet streamers construct a combination in the `type` output member
    // Primary template
    template <typename WS, typename IS>
    struct CombineWallAndIoletStreamers;

    // Specialisation for "standard" wall streamers:
    // need to deduce the underlying wall and iolet
    // delegates out of the type factory.
    template<
            typename C, // collision
            template <typename> class WallT, // wall delegate template
            template <typename> class IoletT // iolet delegate template
    >
    struct CombineWallAndIoletStreamers<
            StreamerTypeFactory<WallT<C>, NullLink<C>>,
            StreamerTypeFactory<NullLink<C>, IoletT<C>>
    > {
        using type = StreamerTypeFactory<WallT<C>, IoletT<C>>;
    };

    // Match the source CPU configuration: Yang applies to pure iolet sites;
    // wall/iolet intersections retain Nash pressure with the selected wall rule.
    template <typename C, template <typename> class WallT>
    struct CombineWallAndIoletStreamers<StreamerTypeFactory<WallT<C>, NullLink<C>>,
                                        StreamerTypeFactory<NullLink<C>, YangPressureLink<C>>>
    {
        using type = StreamerTypeFactory<WallT<C>, NashZerothOrderPressureLink<C>>;
    };

    // Junk Yang is different: the pure wall streamer has a special tag type for no-iolet
    template<
            typename C, // collision
            template <typename> class IoletT // iolet delegate template
    >
    struct CombineWallAndIoletStreamers<
            JunkYangFactory<NullLink<C> >,
            StreamerTypeFactory<NullLink<C>, IoletT<C>>
    > {
        using type = JunkYangFactory<IoletT<C>>;
    };
    template <typename C>
    struct CombineWallAndIoletStreamers<JunkYangFactory<NullLink<C>>,
                                        StreamerTypeFactory<NullLink<C>, YangPressureLink<C>>>
    {
        using type = JunkYangFactory<NashZerothOrderPressureLink<C>>;
    };
    namespace detail
    {
        template <ct_string NAME, typename WS, typename IS>
        auto get_corner_streamer(InitParams &init)
        {
            using C = typename WS::CollisionType;
            if constexpr (NAME == "AUTO")
                return typename CombineWallAndIoletStreamers<WS, IS>::type{init};
            else if constexpr (NAME == "NASHZEROTHORDERPRESSURESBB")
                return StreamerTypeFactory<BounceBackLink<C>, NashZerothOrderPressureLink<C>>{init};
            else if constexpr (NAME == "NASHZEROTHORDERPRESSUREBFL")
                return StreamerTypeFactory<BouzidiFirdaousLallemandLink<C>,
                                           NashZerothOrderPressureLink<C>>{init};
            else if constexpr (NAME == "NASHZEROTHORDERPRESSUREGZS")
                return StreamerTypeFactory<GuoZhengShiLink<C>, NashZerothOrderPressureLink<C>>{
                    init};
            else if constexpr (NAME == "NASHZEROTHORDERPRESSUREGZSE")
                return StreamerTypeFactory<GuoZhengShiElasticWallLink<C>,
                                           NashZerothOrderPressureLink<C>>{init};
            else if constexpr (NAME == "YANGPRESSURESBB")
                return StreamerTypeFactory<BounceBackLink<C>, YangPressureLink<C>>{init};
            else if constexpr (NAME == "YANGPRESSUREBFL")
                return StreamerTypeFactory<BouzidiFirdaousLallemandLink<C>, YangPressureLink<C>>{
                    init};
            else if constexpr (NAME == "YANGPRESSUREGZS")
                return StreamerTypeFactory<GuoZhengShiLink<C>, YangPressureLink<C>>{init};
            else if constexpr (NAME == "YANGPRESSUREGZSE")
                return StreamerTypeFactory<GuoZhengShiElasticWallLink<C>, YangPressureLink<C>>{
                    init};
            else if constexpr (NAME == "LADDIOLETSBB")
                return StreamerTypeFactory<BounceBackLink<C>, LaddIoletLink<C>>{init};
            else if constexpr (NAME == "LADDIOLETBFL")
                return StreamerTypeFactory<BouzidiFirdaousLallemandLink<C>, LaddIoletLink<C>>{init};
            else if constexpr (NAME == "LADDIOLETGZS")
                return StreamerTypeFactory<GuoZhengShiLink<C>, LaddIoletLink<C>>{init};
            else if constexpr (NAME == "LADDIOLETGZSE")
                return StreamerTypeFactory<GuoZhengShiElasticWallLink<C>, LaddIoletLink<C>>{init};
            else
                static_assert(NAME == "AUTO", "Invalid wall/iolet boundary choice");
        }
    } // namespace detail

    template <ct_string NAME, typename WS, typename IS>
    using SelectedCornerStreamer =
        decltype(detail::get_corner_streamer<NAME, WS, IS>(std::declval<InitParams &>()));
}
#endif /* HEMELB_LB_STREAMERS_H */
