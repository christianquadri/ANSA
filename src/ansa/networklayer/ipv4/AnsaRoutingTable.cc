// Copyright (C) 2013 Brno University of Technology (http://nes.fit.vutbr.cz/ansa)
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/.
/**
 * @file AnsaRoutingTable.cc
 * @date 25.1.2013
 * @author Tomas Prochazka (mailto:xproch21@stud.fit.vutbr.cz), Vladimir Vesely (mailto:ivesely@fit.vutbr.cz)
 * @brief Extended RoutingTable with new features for PIM
 */

#include <algorithm>

#include "ansa/networklayer/ipv4/AnsaRoutingTable.h"
#include "networklayer/ipv4/IPv4InterfaceData.h"
#include "ansa/networklayer/common/AnsaInterfaceEntry.h"

//Define_Module(AnsaRoutingTable);

/** Defined in RoutingTable.cc */
std::ostream& operator<<(std::ostream& os, const inet::IPv4Route& e);

/** Printout of structure AnsaIPv4MulticastRoute (one route in table). */
std::ostream& operator<<(std::ostream& os, const AnsaIPv4MulticastRoute& e)
{
    os << e.info();
    return os;
};

/**
 * MULTICAST ROUTING TABLE DESTRUCTOR
 *
 * The method deletes Ansa Routing Table. Delete all entries in table.
 */
AnsaRoutingTable::~AnsaRoutingTable()
{
//    for (unsigned int i=0; i<routes.size(); i++)
//        delete routes[i];
//    for (unsigned int i=0; i<multicastRoutes.size(); i++)
//        delete multicastRoutes[i];
}

inet::IPv4Route *AnsaRoutingTable::findRoute(const inet::IPv4Address& network, const inet::IPv4Address& netmask)
{
    //TODO: assume only ANSAIPv4Route in the routing table?
    //Editted by Michal Ruprich from ANSA due to changes in IPv4RoutingTable
    inet::IPv4Route *route = NULL;
    int count = getNumRoutes();
    //for (RouteVector::const_iterator it = routes.begin(); it != routes.end(); ++it)
    for(int i = 0; i<count; i++)
    {
        inet::IPv4Route *inner_route = getRoute(i);
        if (inner_route->getDestination()==network && inner_route->getNetmask()==netmask) // match
        {
            route = inner_route;
            break;
        }
    }

    return route;
}

inet::IPv4Route *AnsaRoutingTable::findRoute(const inet::IPv4Address& network, const inet::IPv4Address& netmask, const inet::IPv4Address& nexthop)
{
    //Editted by Michal Ruprich from ANSA due to changes in IPv4RoutingTable
    inet::IPv4Route *route = NULL;
    int count = getNumRoutes();
    //for (RouteVector::const_iterator it = routes.begin(); it != routes.end(); ++it)
    for(int i = 0; i<count; i++)
    {
        route = getRoute(i);
        if (route->getDestination()==network && route->getNetmask()==netmask) // match
            return route;
    }

    return NULL;
}

bool AnsaRoutingTable::prepareForAddRoute(inet::IPv4Route *route)
{
    //TODO: assume only ANSAIPv4Route in the routing table?

    inet::IPv4Route *routeInTable = findRoute(route->getDestination(), route->getNetmask());

    if (routeInTable)
    {
        ANSAIPv4Route *ANSARoute = dynamic_cast<ANSAIPv4Route *>(route);
        ANSAIPv4Route *ANSARouteInTable = dynamic_cast<ANSAIPv4Route *>(routeInTable);

        //Assume that inet routes have AD 255
        int newAdminDist = ANSAIPv4Route::dUnknown;
        int oldAdminDist = ANSAIPv4Route::dUnknown;

        if (ANSARoute)
            newAdminDist = ANSARoute->getAdminDist();
        if (ANSARouteInTable)
            oldAdminDist = ANSARouteInTable->getAdminDist();

        if (oldAdminDist > newAdminDist)
        {
            deleteRouteSilent(routeInTable);
        }
        else if(oldAdminDist == newAdminDist)
        {
            if (routeInTable->getMetric() > route->getMetric())
                deleteRouteSilent(routeInTable);
            else
                return false;
        }
        else
        {
            return false;
        }
    }

    return true;
}

bool AnsaRoutingTable::prepareForAddRouteAndNotify(inet::IPv4Route *route)
{
    //TODO: assume only ANSAIPv4Route in the routing table?

    inet::IPv4Route *routeInTable = findRoute(route->getDestination(), route->getNetmask());

    if (routeInTable)
    {
        ANSAIPv4Route *ANSARoute = dynamic_cast<ANSAIPv4Route *>(route);
        ANSAIPv4Route *ANSARouteInTable = dynamic_cast<ANSAIPv4Route *>(routeInTable);

        //Assume that inet routes have AD 255
        int newAdminDist = ANSAIPv4Route::dUnknown;
        int oldAdminDist = ANSAIPv4Route::dUnknown;

        if (ANSARoute)
            newAdminDist = ANSARoute->getAdminDist();
        if (ANSARouteInTable)
            oldAdminDist = ANSARouteInTable->getAdminDist();

        if (oldAdminDist > newAdminDist)
        {
            deleteRoute(routeInTable);
        }
        else if(oldAdminDist == newAdminDist)
        {
            if (routeInTable->getMetric() > route->getMetric())
                deleteRoute(routeInTable);
            else
                return false;
        }
        else
        {
            return false;
        }
    }

    return true;
}

bool AnsaRoutingTable::deleteRouteSilent(inet::IPv4Route *entry)
{
    Enter_Method("deleteRouteSilent(...)");

    entry = internalRemoveRoute(entry);

    if (entry != NULL)
    {
        invalidateCache();
        updateDisplayString();
        ASSERT(entry->getRoutingTable() == this); // still filled in, for the listeners' benefit
        delete entry;
    }
    return entry != NULL;
}

void AnsaRoutingTable::updateNetmaskRoutes()
{
    //Editted by Michal Ruprich from ANSA due to changes in IPv4RoutingTable
    //TODO - check while running
    // first, delete all routes with src=IFACENETMASK
    for (unsigned int k=0; k<(unsigned int)getNumRoutes(); k++)
    {
        //if (routes[k]->getSource()==inet::IPv4Route::IFACENETMASK)
        inet::IPv4Route* route = getRoute(k);
        if(route->getSourceType()==inet::IPv4Route::IFACENETMASK)
        {
            //std::vector<inet::IPv4Route *>::iterator it = routes.begin()+(k--);  // '--' is necessary because indices shift down
            //inet::IPv4Route *route = *it;
            //routes.erase(it);
            deleteRoute(route);
            ASSERT(route->getRoutingTable() == this); // still filled in, for the listeners' benefit
            nb->fireChangeNotification(inet::NF_IPv4_ROUTE_DELETED, route);
            //delete route;
        }
    }

    // then re-add them, according to actual interface configuration
    // TODO: say there's a node somewhere in the network that belongs to the interface's subnet
    // TODO: and it is not on the same link, and the gateway does not use proxy ARP, how will packets reach that node?
    // Update - process only interfaces in up state
    for (int i=0; i<ift->getNumInterfaces(); i++)
    {
        inet::InterfaceEntry *ie = ift->getInterface(i);
        if (ie->isUp() && ie->ipv4Data()->getNetmask()!=inet::IPv4Address::ALLONES_ADDRESS)
        {
            ANSAIPv4Route *route = new ANSAIPv4Route();
            route->setSourceType(inet::IPv4Route::IFACENETMASK);
            route->setDestination(ie->ipv4Data()->getIPAddress().doAnd(ie->ipv4Data()->getNetmask()));
            route->setNetmask(ie->ipv4Data()->getNetmask());
            route->setGateway(inet::IPv4Address());
            route->setMetric(ie->ipv4Data()->getMetric());
            route->setAdminDist(ANSAIPv4Route::dDirectlyConnected);
            route->setInterface(ie);
            route->setRoutingTable(this);
            //RouteVector::iterator pos = upper_bound(routes.begin(), routes.end(), route, routeLessThan);
            //routes.insert(pos, route);
            internalAddRoute(route);
            nb->fireChangeNotification(inet::NF_IPv4_ROUTE_ADDED, route);
        }
    }

    invalidateCache();
    updateDisplayString();
}

/**
 * GET NUMBER OF ROUTES
 *
 * Returns number of entries in multicast routing table.
 */
int AnsaRoutingTable::getNumRoutes() const
{
    return multicastRoutes.size();
}

/**
 * GET ROUTE
 *
 * Returns the k-th route. The returned route cannot be modified;
 * you must delete and re-add it instead. This rule is emphasized
 * by returning a const pointer.
 */
AnsaIPv4MulticastRoute *AnsaRoutingTable::getMulticastRoute(int k) const
{
    if (k < (int)multicastRoutes.size())
        return multicastRoutes[k];

    return NULL;
}

/**
 * UPDATE DISPLAY STRING
 *
 * Update string under multicast table icon - number of multicast routes.
 */
void AnsaRoutingTable::updateDisplayString()
{
    if (!ev.isGUI())
        return;

    char buf[80];
    //sprintf(buf, "%d routes\n%d multicast routes", (int)routes.size(), (int)multicastRoutes.size());
    sprintf(buf, "%d routes\n%d multicast routes", getNumRoutes(), getNumMulticastRoutes());

    getDisplayString().setTagArg("t", 0, buf);
}



/**
 * INITIALIZE
 *
 * The method initializes Multicast Routing Table module. It get access to all needed objects.
 *
 * @param stage Stage of initialization.
 */
void AnsaRoutingTable::initialize(int stage)
{
    if (stage==0)
    {
        // get a pointer to inet::IInterfaceTable
        ift = InterfaceTableAccess().get();

        nb = NotificationBoardAccess().get();

        //IPForward = par("IPForward").boolValue();
        forwarding = par("IPForward").boolValue();
        multicastForward = par("forwardMulticast").boolValue();

        /*
        nb->subscribe(this, inet::NF_INTERFACE_CREATED);
        nb->subscribe(this, inet::NF_INTERFACE_DELETED);
        nb->subscribe(this, inet::NF_INTERFACE_STATE_CHANGED);
        nb->subscribe(this, inet::NF_INTERFACE_CONFIG_CHANGED);
        nb->subscribe(this, inet::NF_INTERFACE_IPv4CONFIG_CHANGED);
        */

        // watch multicast table
        WATCH_VECTOR(showMRoute);
        //WATCH_PTRVECTOR(routes);
        WATCH_PTRVECTOR(multicastRoutes);
        WATCH(forwarding);
        WATCH(routerId);
    }
    else if (stage==3)
    {
        // routerID selection must be after stage==2 when network autoconfiguration
        // assigns interface addresses
        configureRouterId();

        // We don't want to call this method:
        // 1. adds inet::IPv4Route instead of ANSAIPv4Route (method is not overridden)
        // 2. directly connected routes are added in the deviceConfigurator
        //updateNetmaskRoutes();

        //printRoutingTable();
    }
}

void AnsaRoutingTable::receiveChangeNotification(int category, const cObject *details)
{
    inet::InterfaceEntry *entry = NULL;

    if (simulation.getContextType()==CTX_INITIALIZE)
        return;  // ignore notifications during initialize

    Enter_Method_Silent();
    inet::printNotificationBanner(category, details);

    if (category==inet::NF_INTERFACE_CREATED)
    {
        // add netmask route for the new interface
        updateNetmaskRoutes();
    }
    else if (category==inet::NF_INTERFACE_DELETED)
    {
        // remove all routes that point to that interface
        entry = const_cast<inet::InterfaceEntry*>(check_and_cast<const inet::InterfaceEntry*>(details));
        deleteInterfaceRoutes(entry);
    }
    else if (category==inet::NF_INTERFACE_STATE_CHANGED)
    {
        entry = const_cast<inet::InterfaceEntry*>(check_and_cast<const inet::InterfaceEntry*>(details));

        if (entry->isUp())
        { // an interface goes up
            insertConnectedRoute(entry);
        }
        else if ((entry->getState()==inet::InterfaceEntry::DOWN) || !entry->hasCarrier())
        { // an interface goes down
            deleteInterfaceRoutes(entry);
        }
    }
    else if (category==inet::NF_INTERFACE_CONFIG_CHANGED)
    {
        invalidateCache();
    }
    else if (category==inet::NF_INTERFACE_IPv4CONFIG_CHANGED)
    {
        // if anything IPv4-related changes in the interfaces, interface netmask
        // based routes have to be re-built.
        updateNetmaskRoutes();
    }
}

void AnsaRoutingTable::deleteInterfaceRoutes(inet::InterfaceEntry *entry)
{
    bool changed = false;

    // delete unicast routes using this interface
    //for (RouteVector::iterator it = routes.begin(); it != routes.end(); )
    inet::IPv4Route* route = nullptr;
    for(int i=0; i<getNumRoutes();)
    {
        if(route == nullptr)
            route = getRoute(i);

        ANSAIPv4Route *ansaRoute = dynamic_cast<ANSAIPv4Route *>(route);
        ANSAIPv4Route::RoutingProtocolSource rtSrc = ANSAIPv4Route::pUnknown;
        if (ansaRoute != NULL)
            rtSrc = ansaRoute->getRoutingProtocolSource();

        // Do not delete EIGRP routes
        if (route->getInterface() == entry &&
                (ansaRoute == NULL || (rtSrc != ANSAIPv4Route::pEIGRP && rtSrc != ANSAIPv4Route::pEIGRPext)))
        {

            route = internalRemoveRoute(route);
            ASSERT(route->getRoutingTable() == this); // still filled in, for the listeners' benefit
            nb->fireChangeNotification(inet::NF_IPv4_ROUTE_DELETED, route);
            delete route;
            changed = true;
        }
        else
            ++i;
    }

    // delete or update multicast routes:
    //   1. delete routes has entry as input interface
    //   2. remove entry from output interface list
    for (routeVector::iterator it = multicastRoutes.begin(); it != multicastRoutes.end(); )
    {
        inet::IPv4MulticastRoute *route = *it;
        if (route->getInInterface() && route->getInInterface()->getInterface() == entry)
        {
            it = multicastRoutes.erase(it);
            ASSERT(route->getRoutingTable() == this); // still filled in, for the listeners' benefit
            nb->fireChangeNotification(inet::NF_IPv4_MROUTE_DELETED, route);
            delete route;
            changed = true;
        }
        else
        {
            bool removed = route->removeOutInterface(entry);
            if (removed)
            {
                nb->fireChangeNotification(inet::NF_IPv4_MROUTE_CHANGED, route);
                changed = true;
            }
            ++it;
        }
    }

    if (changed)
    {
        invalidateCache();
        updateDisplayString();
    }
}

void AnsaRoutingTable::insertConnectedRoute(inet::InterfaceEntry *ie)
{
    ANSAIPv4Route *route = new ANSAIPv4Route();
    route->setSourceType(inet::IPv4Route::IFACENETMASK);
    route->setDestination(ie->ipv4Data()->getIPAddress().doAnd(ie->ipv4Data()->getNetmask()));
    route->setNetmask(ie->ipv4Data()->getNetmask());
    route->setGateway(inet::IPv4Address());
    route->setMetric(ie->ipv4Data()->getMetric());
    route->setAdminDist(ANSAIPv4Route::dDirectlyConnected);
    route->setInterface(ie);
    route->setRoutingTable(this);

    prepareForAddRouteAndNotify(route);
    addRoute(route);
}

/**
 * ADD ROUTE
 *
 * Function check new multicast table entry and then add new entry to multicast table.
 *
 * @param entry New entry about new multicast group.
 * @see MulticastIPRoute
 * @see updateDisplayString()
 * @see generateShowIPMroute()
 */
void AnsaRoutingTable::addMulticastRoute(const AnsaIPv4MulticastRoute *entry)
{
    Enter_Method("addMulticastRoute(...)");

    // check for null multicast group address
    if (entry->getMulticastGroup().isUnspecified())
        error("addMulticastRoute(): multicast group address cannot be NULL");

    // check that group address is multicast address
    if (!entry->getMulticastGroup().isMulticast())
        error("addMulticastRoute(): group address is not multicast address");

    // check for source or RP address
    if (entry->getOrigin().isUnspecified() && entry->getRP().isUnspecified())
        error("addMulticastRoute(): source or RP address has to be specified");

    // check that the incoming interface exists
    //FIXME for PIM-SM is needed unspecified next hop (0.0.0.0)
    //if (!entry->getInIntPtr() || entry->getInIntNextHop().isUnspecified())
        //error("addMulticastRoute(): incoming interface has to be specified");
    //if (!entry->getInIntPtr())
        //error("addMulticastRoute(): incoming interface has to be specified");

    // add to tables
    multicastRoutes.push_back(const_cast<AnsaIPv4MulticastRoute*>(entry));

    updateDisplayString();
    generateShowIPMroute();
}



/**
 * DELETE ROUTE
 *
 * Function check new multicast table entry and then add new entry to multicast table.
 *
 * @param entry Multicast entry which should be deleted from multicast table.
 * @return False if entry was not found in table. True if entry was deleted.
 * @see MulticastIPRoute
 * @see updateDisplayString()
 * @see generateShowIPMroute()
 */
bool AnsaRoutingTable::deleteMulticastRoute(const AnsaIPv4MulticastRoute *entry)
{
    Enter_Method("deleteMulticastRoute(...)");

    // find entry in routing table
    std::vector<AnsaIPv4MulticastRoute*>::iterator i;
    for (i=multicastRoutes.begin(); i!=multicastRoutes.end(); ++i)
    {
        if ((*i) == entry)
            break;
    }

    // if entry was found, it can be deleted
    if (i!=multicastRoutes.end())
    {
        // first delete all timers assigned to route
        if (entry->getSrt() != NULL)
        {
            cancelEvent(entry->getSrt());
            delete entry->getSrt();
        }
        if (entry->getGrt() != NULL)
        {
            cancelEvent(entry->getGrt());
            delete entry->getGrt();
        }
        if (entry->getSat())
        {
            cancelEvent(entry->getSat());
            delete entry->getSat();
        }
        if (entry->getKat())
        {
            cancelEvent(entry->getKat());
            delete entry->getKat();
        }
        if (entry->getEt())
        {
            cancelEvent(entry->getEt());
            delete entry->getEt();
        }
        if (entry->getJt())
        {
            cancelEvent(entry->getJt());
            delete entry->getJt();
        }
        if (entry->getPpt())
        {
            cancelEvent(entry->getPpt());
            delete entry->getPpt();
        }
        // delete timers from outgoing interfaces
        AnsaIPv4MulticastRoute::InterfaceVector outInt = entry->getOutInt();
        for (unsigned int j = 0;j < outInt.size(); j++)
        {
            if (outInt[j].pruneTimer != NULL)
            {
                cancelEvent(outInt[j].pruneTimer);
                delete outInt[j].pruneTimer;
            }
        }

        // delete route
        multicastRoutes.erase(i);
        delete entry;
        updateDisplayString();
        generateShowIPMroute();
        return true;
    }
    return false;
}

/**
 * GET ROUTE FOR
 *
 * The method returns one route from multicast routing table for given group and source IP addresses.
 *
 * @param group IP address of multicast group.
 * @param source IP address of multicast source.
 * @return Pointer to found multicast route.
 * @see getRoute()
 */
AnsaIPv4MulticastRoute *AnsaRoutingTable::getRouteFor(inet::IPv4Address group, inet::IPv4Address source)
{
    Enter_Method("getMulticastRoutesFor(%x, %x)", group.getInt(), source.getInt()); // note: str().c_str() too slow here here
    EV << "MulticastRoutingTable::getRouteFor - group = " << group << ", source = " << source << endl;

    // search in multicast routing table
    AnsaIPv4MulticastRoute *route = NULL;

    int n = multicastRoutes.size();
    int i;
    // go through all multicast routes
    for (i = 0; i < n; i++)
    {
        route = getMulticastRoute(i);
        if (route->getMulticastGroup().getInt() == group.getInt() && route->getOrigin().getInt() == source.getInt())
            break;
    }

    if (i == n)
        return NULL;
    return route;
}

/**
 * GET ROUTE FOR
 *
 * The method returns all routes from multicast routing table for given multicast group.
 *
 * @param group IP address of multicast group.
 * @return Vecotr of pointers to routes in multicast table.
 * @see getRoute()
 */
std::vector<AnsaIPv4MulticastRoute*> AnsaRoutingTable::getRouteFor(inet::IPv4Address group)
{
    Enter_Method("getMulticastRoutesFor(%x)", group.getInt()); // note: str().c_str() too slow here here
    EV << "MulticastRoutingTable::getRouteFor - address = " << group << endl;
    std::vector<AnsaIPv4MulticastRoute*> routes;

    // search in multicast table
    int n = multicastRoutes.size();
    for (int i = 0; i < n; i++)
    {
        AnsaIPv4MulticastRoute *route = getMulticastRoute(i);
        if (route->getMulticastGroup().getInt() == group.getInt())
            routes.push_back(route);
    }

    return routes;
}

/**
 * GENERATE SHOW IP MROUTE
 *
 * This method should be called after each change of multicast routing table. It is output which
 * represents state of the table. Format is same as format on Cisco routers.
 */
void AnsaRoutingTable::generateShowIPMroute()
{
    EV << "MulticastRoutingTable::generateShowIPRoute()" << endl;
    showMRoute.clear();

    int n = getNumRoutes();
    const AnsaIPv4MulticastRoute* ipr;

    for (int i=0; i<n; i++)
    {
        ipr = getMulticastRoute(i);
        std::stringstream os;
        os << "(";
        if (ipr->getOrigin().isUnspecified()) os << "*, "; else os << ipr->getOrigin() << ",  ";
        os << ipr->getMulticastGroup() << "), ";
        if (ipr->getOrigin() == inet::IPv4Address::UNSPECIFIED_ADDRESS)
            if (!ipr->getRP().isUnspecified()) os << "RP is " << ipr->getRP()<< ", ";
        os << "flags: ";
        std::vector<AnsaIPv4MulticastRoute::flag> flags = ipr->getFlags();
        for (unsigned int j = 0; j < flags.size(); j++)
        {
            switch(flags[j])
            {
                case AnsaIPv4MulticastRoute::D:
                    os << "D";
                    break;
                case AnsaIPv4MulticastRoute::S:
                    os << "S";
                    break;
                case AnsaIPv4MulticastRoute::C:
                    os << "C";
                    break;
                case AnsaIPv4MulticastRoute::P:
                    os << "P";
                    break;
                case AnsaIPv4MulticastRoute::A:
                    os << "A";
                    break;
                case AnsaIPv4MulticastRoute::F:
                    os << "F";
                    break;
                case AnsaIPv4MulticastRoute::T:
                    os << "T";
                    break;
                case AnsaIPv4MulticastRoute::NO_FLAG:
                    break;
            }
        }
        os << endl;

        os << "Incoming interface: ";
        if (ipr->getInIntPtr()) os << ipr->getInIntPtr()->getName() << ", ";
        else os << "Null, ";
        if (ipr->getInIntNextHop().isUnspecified()) os << "RPF neighbor 0.0.0.0" << endl;
        else os << "RPF neighbor " << ipr->getInIntNextHop() << endl;
        os << "Outgoing interface list:" << endl;

        AnsaIPv4MulticastRoute::InterfaceVector all = ipr->getOutInt();
        if (all.size() == 0)
            os << "Null" << endl;
        else
            for (unsigned int k = 0; k < all.size(); k++)
            {
                if ((all[k].mode == AnsaIPv4MulticastRoute::Sparsemode && all[k].shRegTun == true)
                        || all[k].mode ==AnsaIPv4MulticastRoute:: Densemode)
                {
                    os << all[k].intPtr->getName() << ", ";
                    if (all[k].forwarding == AnsaIPv4MulticastRoute::Forward) os << "Forward/"; else os << "Pruned/";
                    if (all[k].mode == AnsaIPv4MulticastRoute::Densemode) os << "Dense"; else os << "Sparse";
                    os << endl;
                }
                else
                    os << "Null" << endl;
            }
        showMRoute.push_back(os.str());
    }
    std::stringstream out;
}

/**
 * GET ROUTES FOR SOURCES
 *
 * The method returns all routes from multicast routing table for given source.
 *
 * @param source IP address of multicast source.
 * @return Vector of found multicast routes.
 * @see getNetwork()
 */
std::vector<AnsaIPv4MulticastRoute*> AnsaRoutingTable::getRoutesForSource(inet::IPv4Address source)
{
    Enter_Method("getRoutesForSource(%x)", source.getInt()); // note: str().c_str() too slow here here
    EV << "MulticastRoutingTable::getRoutesForSource - source = " << source << endl;
    std::vector<AnsaIPv4MulticastRoute*> routes;

    // search in multicast table
    int n = multicastRoutes.size();
    int i;
    for (i = 0; i < n; i++)
    {
        //FIXME works only for classfull adresses (function getNetwork) !!!!
        AnsaIPv4MulticastRoute *route = getMulticastRoute(i);
        if (route->getOrigin().getNetwork().getInt() == source.getInt())
            routes.push_back(route);
    }
    return routes;
}

bool AnsaRoutingTable::isLocalAddress(const inet::IPv4Address& dest) const
{
    Enter_Method("isLocalAddress A (%u.%u.%u.%u)", dest.getDByte(0), dest.getDByte(1), dest.getDByte(2), dest.getDByte(3)); // note: str().c_str() too slow here

    if (localAddresses.empty())
    {
        // collect interface addresses if not yet done
        for (int i=0; i<ift->getNumInterfaces(); i++)
        {
            inet::IPv4Address interfaceAddr = ift->getInterface(i)->ipv4Data()->getIPAddress();
            localAddresses.insert(interfaceAddr);
        }
    }

    for (int i=0; i<ift->getNumInterfaces(); i++)
    {
        AnsaInterfaceEntry* ieVF = dynamic_cast<AnsaInterfaceEntry *>(ift->getInterface(i));
        if (ieVF && ieVF->hasIPAddress(dest))
            return true;
    }

    AddressSet::iterator it = localAddresses.find(dest);
    return it!=localAddresses.end();
}

inet::InterfaceEntry *AnsaRoutingTable::getInterfaceByAddress(const inet::IPv4Address& addr) const
{
    Enter_Method("getInterfaceByAddress(%u.%u.%u.%u)", addr.getDByte(0), addr.getDByte(1), addr.getDByte(2), addr.getDByte(3)); // note: str().c_str() too slow here

    if (addr.isUnspecified())
        return NULL;
    for (int i=0; i<ift->getNumInterfaces(); ++i)
    {
        inet::InterfaceEntry* ie = ift->getInterface(i);
        if (ie->ipv4Data()->getIPAddress()==addr)
            return ie;

        if (!ie->isLoopback() && dynamic_cast<AnsaInterfaceEntry *>(ie))
        {
            AnsaInterfaceEntry* ieVF = dynamic_cast<AnsaInterfaceEntry *>(ie);
            if (ieVF->hasIPAddress(addr))
                return ie;

        }
    }
    return NULL;
}

