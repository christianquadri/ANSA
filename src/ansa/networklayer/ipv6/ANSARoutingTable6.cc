// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this program; if not, see <http://www.gnu.org/licenses/>.
//

/**
 * @file ANSARoutingTable6.cc
 * @date 21.5.2013
 * @author Jiri Trhlik (mailto:jiritm@gmail.com), Vladimir Vesely (mailto:ivesely@fit.vutbr.cz)
 * @brief Extended RoutingTable6
 * @details Adds administrative distance, fixes routing table cache, IPv4-like routes updates
 */

#include <algorithm>

#include "common/INETUtils.h"

#include "ansa/networklayer/ipv6/ANSARoutingTable6.h"

#include "networklayer/ipv6/IPv6InterfaceData.h"
#include "networklayer/common/InterfaceTableAccess.h"

#include "networklayer/ipv6tunneling/IPv6TunnelingAccess.h"

//Define_Module(ANSARoutingTable6);

ANSAIPv6Route::ANSAIPv6Route(inet::IPv6Address destPrefix, int length, RouteSrc src)
    : IPv6Route(destPrefix, length, src)
{
    _adminDist = dUnknown;
    _routingProtocolSource = pUnknown;

    ift = InterfaceTableAccess().get();
    rt = NULL;
}

std::string ANSAIPv6Route::info() const
{
    std::stringstream out;

    out << getRouteSrcName();
    out << " ";
    if (getDestPrefix().isUnspecified())
        out << "::";
    else
        out << getDestPrefix();
    out << "/" << getPrefixLength();
    out << " [" << getAdminDist() << "/" << getMetric() << "]";
    out << " via ";
    if (getNextHop() == inet::IPv6Address::UNSPECIFIED_ADDRESS)
        out << "::";
    else
        out << getNextHop();
    out << ", " << getInterfaceName();
    if (getExpiryTime()>0)
        out << " exp:" << getExpiryTime();

    return out.str();
}

std::string ANSAIPv6Route::detailedInfo() const
{
    return std::string();
}

const char *ANSAIPv6Route::getRouteSrcName() const
{
    switch (getSrc())
    {
        case FROM_RA:
            return "ND";  //Neighbor Discovery

        case OWN_ADV_PREFIX:
            return "C";

        case STATIC:
            if (getNextHop() == inet::IPv6Address::UNSPECIFIED_ADDRESS)
                return "C";
            return "S";

        case ROUTING_PROT:
            switch(getRoutingProtocolSource())
            {
                case pRIP: return "R";
                case pBGP: return "B";
                case pISIS1: return "I1";
                case pISIS2: return "I2";
                case pISISinterarea: return "IA";
                case pISISsum: return "IS";
                case pOSPFintra: return "O";
                case pOSPFinter: return "OI";
                case pOSPFext1: return "OE1";
                case pOSPFext2: return "OE2";
                case pOSPFNSSAext1: return "ON1";
                case pOSPFNSSAext2: return "ON2";
                case pEIGRP: return "D";
                case pEIGRPext: return "EX";
                case pBABEL: return "BA";
                default: return "?";
            }

        default:
            return "?";
    }
}

const char *ANSAIPv6Route::getInterfaceName() const
{
    ASSERT(ift);
    inet::InterfaceEntry *interface = ift->getInterfaceById(_interfaceID);
    return interface ? interface->getName() : "";
}

void ANSAIPv6Route::changed(int fieldCode)
{
    if (rt)
        rt->routeChanged(this, fieldCode);
}

void ANSAIPv6Route::changedSilent(int fieldCode)
{
    if (rt)
        rt->routeChangedSilent(this, fieldCode);
}

ANSARoutingTable6::ANSARoutingTable6()
{
}

ANSARoutingTable6::~ANSARoutingTable6()
{
}

inet::IPv6Route *ANSARoutingTable6::findRoute(const inet::IPv6Address& prefix, int prefixLength)
{
    //TODO: assume only ANSAIPv6Route in the routing table?

    inet::IPv6Route *route = NULL;
    for (RouteList::iterator it=routeList.begin(); it!=routeList.end(); it++)
    {
        if ((*it)->getDestPrefix()==prefix && (*it)->getPrefixLength()==prefixLength)
        {
            route = (*it);
            break;
        }
    }

    return route;
}

inet::IPv6Route *ANSARoutingTable6::findRoute(const inet::IPv6Address& prefix, int prefixLength, const inet::IPv6Address& nexthop)
{
    //TODO: assume only ANSAIPv6Route in the routing table?

    inet::IPv6Route *route = NULL;
    for (RouteList::iterator it=routeList.begin(); it!=routeList.end(); it++)
    {
        if ((*it)->getDestPrefix()==prefix && (*it)->getPrefixLength()==prefixLength && (*it)->getNextHop()==nexthop)
        {
            route = (*it);
            break;
        }
    }

    return route;
}

bool ANSARoutingTable6::prepareForAddRoute(inet::IPv6Route *route)
{
    //TODO: assume only ANSAIPv6Route in the routing table?

    inet::IPv6Route *routeInTable = findRoute(route->getDestPrefix(), route->getPrefixLength());

    if (routeInTable)
    {
        ANSAIPv6Route *ANSARoute = dynamic_cast<ANSAIPv6Route *>(route);
        ANSAIPv6Route *ANSARouteInTable = dynamic_cast<ANSAIPv6Route *>(routeInTable);

        //Assume that inet routes have AD 255
        int newAdminDist = ANSAIPv6Route::dUnknown;
        int oldAdminDist = ANSAIPv6Route::dUnknown;

        if (ANSARoute)
            newAdminDist = ANSARoute->getAdminDist();
        if (ANSARouteInTable)
            oldAdminDist = ANSARouteInTable->getAdminDist();

        if (oldAdminDist > newAdminDist)
        {
            removeRouteSilent(routeInTable);
        }
        else if(oldAdminDist == newAdminDist)
        {
            if (routeInTable->getMetric() > route->getMetric())
                removeRouteSilent(routeInTable);
            else
                return false;
        }
        else
        {
            return false;
        }
    }

    /*XXX: this deletes some cache entries we want to keep, but the node MUST update
     the Destination Cache in such a way that all entries will use the latest
     route information.*/
    purgeDestCache();

    return true;
}

void ANSARoutingTable6::addOrUpdateOnLinkPrefix(const inet::IPv6Address& destPrefix, int prefixLength,
        int interfaceId, simtime_t expiryTime)
{
    // see if prefix exists in table
    ANSAIPv6Route *route = NULL;
    for (RouteList::iterator it=routeList.begin(); it!=routeList.end(); it++)
    {
        if ((*it)->getSrc()==inet::IPv6Route::FROM_RA && (*it)->getDestPrefix()==destPrefix && (*it)->getPrefixLength()==prefixLength)
        {
            route = dynamic_cast<ANSAIPv6Route *>(*it);
            break;
        }
    }

    if (route==NULL)
    {
        // create new route object
        ANSAIPv6Route *route = new ANSAIPv6Route(destPrefix, prefixLength, inet::IPv6Route::FROM_RA);
        route->setInterfaceId(interfaceId);
        route->setExpiryTime(expiryTime);
        route->setMetric(0);
        route->setAdminDist(ANSAIPv6Route::dDirectlyConnected);

        // then add it
        addRoute(route);
    }
    else
    {
        // update existing one; notification-wise, we pretend the route got removed then re-added
        nb->fireChangeNotification(inet::NF_IPv6_ROUTE_DELETED, route);
        route->setInterfaceId(interfaceId);
        route->setExpiryTime(expiryTime);
        nb->fireChangeNotification(inet::NF_IPv6_ROUTE_ADDED, route);
    }

    updateDisplayString();
}

void ANSARoutingTable6::addOrUpdateOwnAdvPrefix(const inet::IPv6Address& destPrefix, int prefixLength,
        int interfaceId, simtime_t expiryTime)
{
    // FIXME this is very similar to the one above -- refactor!!

    // see if prefix exists in table
    ANSAIPv6Route *route = NULL;
    for (RouteList::iterator it=routeList.begin(); it!=routeList.end(); it++)
    {
        if ((*it)->getSrc()==inet::IPv6Route::OWN_ADV_PREFIX && (*it)->getDestPrefix()==destPrefix && (*it)->getPrefixLength()==prefixLength)
        {
            route = dynamic_cast<ANSAIPv6Route *>(*it);
            break;
        }
    }

    if (route==NULL)
    {
        // create new route object
        ANSAIPv6Route *route = new ANSAIPv6Route(destPrefix, prefixLength, inet::IPv6Route::OWN_ADV_PREFIX);
        route->setInterfaceId(interfaceId);
        route->setExpiryTime(expiryTime);
        route->setMetric(0);
        route->setAdminDist(ANSAIPv6Route::dDirectlyConnected);

        // then add it
        addRoute(route);
    }
    else
    {
        // update existing one; notification-wise, we pretend the route got removed then re-added
        nb->fireChangeNotification(inet::NF_IPv6_ROUTE_DELETED, route);
        route->setInterfaceId(interfaceId);
        route->setExpiryTime(expiryTime);
        nb->fireChangeNotification(inet::NF_IPv6_ROUTE_ADDED, route);
    }

    updateDisplayString();
}

void ANSARoutingTable6::addStaticRoute(const inet::IPv6Address& destPrefix, int prefixLength,
                    unsigned int interfaceId, const inet::IPv6Address& nextHop,
                    int metric)
{
    // create route object
    ANSAIPv6Route *route = new ANSAIPv6Route(destPrefix, prefixLength, inet::IPv6Route::STATIC);
    route->setInterfaceId(interfaceId);
    route->setNextHop(nextHop);
    if (metric==0)
        metric = 10; // TBD should be filled from interface metric
    route->setMetric(metric);
    route->setAdminDist(ANSAIPv6Route::dStatic);

    // then add it
    addRoute(route);
}

void ANSARoutingTable6::addDefaultRoute(const inet::IPv6Address& nextHop, unsigned int ifID,
        simtime_t routerLifetime)
{
    // create route object
    ANSAIPv6Route *route = new ANSAIPv6Route(inet::IPv6Address(), 0, inet::IPv6Route::FROM_RA);
    route->setInterfaceId(ifID);
    route->setNextHop(nextHop);
    route->setMetric(10); //FIXME:should be filled from interface metric
    route->setAdminDist(ANSAIPv6Route::dStatic);

#ifdef WITH_xMIPv6
    route->setExpiryTime(routerLifetime); // lifetime useful after transitioning to new AR // 27.07.08 - CB
#endif /* WITH_xMIPv6 */

    // then add it
    addRoute(route);
}

void ANSARoutingTable6::addRoutingProtocolRoute(ANSAIPv6Route *route)
{
    ASSERT(route->getSourceType()==inet::IPv6Route::ROUTING_PROT);
    addRoute(route);
}

void ANSARoutingTable6::addRoute(ANSAIPv6Route *route)
{
    /*XXX: this deletes some cache entries we want to keep, but the node MUST update
     the Destination Cache in such a way that the latest route information are used.*/
    purgeDestCache();

    route->setRoutingTable(this);
    this->internalAddRoute(route);
    //routeList.push_back(route);

    // we keep entries sorted by prefix length in routeList, so that we can
    // stop at the first match when doing the longest prefix matching
    //std::sort(routeList.sbegin(), routeList.end(), routeLessThan);

    updateDisplayString();

    nb->fireChangeNotification(inet::NF_IPv6_ROUTE_ADDED, route);
}

//void ANSARoutingTable6::removeRoute(inet::IPv6Route *route)
//{
//    RouteList::iterator it = std::find(routeList.begin(), routeList.end(), route);
//    ASSERT(it!=routeList.end());
//
//    nb->fireChangeNotification(inet::NF_IPv6_ROUTE_DELETED, route); // rather: going to be deleted
//
//    routeList.erase(it);
//
//    /*XXX: this deletes some cache entries we want to keep, but the node MUST update
//     the Destination Cache in such a way that all entries using the next-hop from
//     the deleted route perform next-hop determination again rather than continue
//     sending traffic using that deleted route next-hop.*/
//    purgeDestCache();
//
//    delete route;
//
//    updateDisplayString();
//}

void ANSARoutingTable6::removeRouteSilent(inet::IPv6Route *route)
{
    RouteList::iterator it = std::find(routeList.begin(), routeList.end(), route);
    ASSERT(it!=routeList.end());

    routeList.erase(it);

    /*XXX: this deletes some cache entries we want to keep, but the node MUST update
     the Destination Cache in such a way that all entries using the next-hop from
     the deleted route perform next-hop determination again rather than continue
     sending traffic using that deleted route next-hop.*/
    purgeDestCache();

    delete route;

    updateDisplayString();
}

void ANSARoutingTable6::routeChanged(ANSAIPv6Route *entry, int fieldCode)
{
    ASSERT(entry != NULL);

    routeChangedSilent(entry, fieldCode);

    nb->fireChangeNotification(inet::NF_IPv6_ROUTE_CHANGED, entry); // TODO include fieldCode in the notification
}

void ANSARoutingTable6::routeChangedSilent(ANSAIPv6Route *entry, int fieldCode)
{
    ASSERT(entry != NULL);

    /*if (fieldCode==ANSAIPv6Route::F_NEXTHOP || fieldCode==ANSAIPv6Route::F_METRIC || fieldCode==ANSAIPv6Route::F_IFACE
            || fieldCode==ANSAIPv6Route::F_ADMINDIST || fieldCode==ANSAIPv6Route::F_ROUTINGPROTSOURCE
            )*/

    /*XXX: this deletes some cache entries we want to keep, but the node MUST update
     the Destination Cache in such a way that all entries will use the latest
     route information.*/
    if (fieldCode==ANSAIPv6Route::F_NEXTHOP || fieldCode==ANSAIPv6Route::F_IFACE)
        purgeDestCache();

    updateDisplayString();
}

void ANSARoutingTable6::receiveChangeNotification(int category, const cObject *details)
{
    if (simulation.getContextType()==CTX_INITIALIZE)
        return;  // ignore notifications during initialize

    Enter_Method_Silent();
    inet::printNotificationBanner(category, details);

    if (category==inet::NF_INTERFACE_CREATED)
    {
        //TODO something like this:
        //InterfaceEntry *ie = check_and_cast<inet::InterfaceEntry*>(details);
        //configureInterfaceForIPv6(ie);
    }
    else if (category==inet::NF_INTERFACE_DELETED)
    {
        //TODO remove all routes that point to that interface (?)
        inet::InterfaceEntry *interfaceEntry = check_and_cast<inet::InterfaceEntry*>(details);
        int interfaceEntryId = interfaceEntry->getInterfaceId();
        purgeDestCacheForInterfaceID(interfaceEntryId);
    }
    else if (category==inet::NF_INTERFACE_STATE_CHANGED)
    {
        inet::InterfaceEntry *interfaceEntry = check_and_cast<inet::InterfaceEntry*>(details);
        int interfaceEntryId = interfaceEntry->getInterfaceId();

        // an interface went down
        if (interfaceEntry->getState()==inet::InterfaceEntry::DOWN)
        {
            purgeDestCacheForInterfaceID(interfaceEntryId);
        }
    }
    else if (category==inet::NF_INTERFACE_CONFIG_CHANGED)
    {
        //TODO invalidate routing cache (?)
    }
    else if (category==inet::NF_INTERFACE_IPv6CONFIG_CHANGED)
    {
        //TODO
    }
}
