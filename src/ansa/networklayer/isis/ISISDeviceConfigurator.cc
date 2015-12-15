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
// 


#include "ISISDeviceConfigurator.h"
#include "IPv6Address.h"
#include "IPv6InterfaceData.h"
#include "IPv4Address.h"

#include <errno.h>


Define_Module(ISISDeviceConfigurator);

using namespace std;

void ISISDeviceConfigurator::initialize(int stage){

    if (stage == 0)
    {

        /* these variables needs to be set only once */
        deviceType = par("deviceType");
        deviceId = par("deviceId");
        configFile = par("configFile");
        /*
         * doesn't need to be performed anywhere else,
         * if it's NULL then behaviour depends on device type
         */
        device = GetDevice(deviceType, deviceId, configFile);
        if (device == NULL)
        {
            ev << "No configuration found for this device (" << deviceType << " id=" << deviceId << ")" << endl;
            return;
        }
    }
    else if (stage == 2)
    {// interfaces and routing table are not ready before stage 2
        // get table of interfaces of this device

        ift = InterfaceTableAccess().get();
        if (ift == NULL)
            throw cRuntimeError("InterfaceTable not found");

        //////////////////////////
        /// IPv4 Configuration ///
        //////////////////////////
        // get routing table of this device
        rt = RoutingTableAccess().getIfExists();
        if (rt != NULL)
        {
            for (int i=0; i<ift->getNumInterfaces(); ++i)
            {
                //ift->getInterface(i)->setMulticast(true);
                rt->configureInterfaceForIPv4(ift->getInterface(i));
            }

            if (device != NULL)
            {
                cXMLElement *iface = GetInterface(NULL, device);
                if (iface == NULL)
                    ev << "No IPv4 interface configuration found for this device (" << deviceType << " id=" << deviceId << ")" << endl;
                else
                    loadInterfaceConfig(iface);

                // configure static routing
                cXMLElement *route = GetStaticRoute(NULL, device);
                if (route == NULL && strcmp(deviceType, "Router") == 0)
                    ev << "No IPv4 static routing configuration found for this device (" << deviceType << " id=" << deviceId << ")" << endl;
                else
                    loadStaticRouting(route);
            }
        }

        //////////////////////////
        /// IPv6 Configuration ///
        //////////////////////////
        // get routing table of this device
        rt6 = RoutingTable6Access().getIfExists();
        if (rt6 != NULL)
        {
            // RFC 4861 specifies that sending RAs should be disabled by default
            for (int i = 0; i < ift->getNumInterfaces(); i++)
                ift->getInterface(i)->ipv6Data()->setAdvSendAdvertisements(false);

            if (device == NULL)
                return;

            // configure interfaces - addressing
            cXMLElement *iface = GetInterface(NULL, device);
            if (iface == NULL)
                ev << "No IPv6 interface configuration found for this device (" << deviceType << " id=" << deviceId << ")" << endl;
            else
                loadInterfaceConfig6(iface);

            // configure static routing
            cXMLElement *route = GetStaticRoute6(NULL, device);
            if (route == NULL && strcmp(deviceType, "Router") == 0)
                ev << "No IPv6 static routing configuration found for this device (" << deviceType << " id=" << deviceId << ")" << endl;
            else
                loadStaticRouting6(route);

            // Adding default route requires routing table lookup to pick the right output
            // interface. This needs to be performed when all IPv6 addresses are already assigned
            // and there are matching records in the routing table.
            cXMLElement *gateway = device->getFirstChildWithTag("DefaultRouter6");
            if (gateway == NULL && strcmp(deviceType, "Host") == 0)
                ev << "No IPv6 default-router configuration found for this device (" << deviceType << " id=" << deviceId << ")" << endl;
            else
                loadDefaultRouter6(gateway);
        }
    }
    else if(stage == 3)
    {
        if (device == NULL)
            return;

        if (isMulticastEnabled(device))
        {
            // get PIM interface table of this device
            pimIft = PimInterfaceTableAccess().get();
            if (pimIft == NULL)
                throw cRuntimeError("PimInterfaces not found");

            // fill pim interfaces table from config file
            cXMLElement *iface = GetInterface(NULL, device);
            if (iface != NULL)
            {
                loadPimInterfaceConfig(iface);

            }
            else
                EV<< "No PIM interface is configured for this device (" << deviceType << " id=" << deviceId << ")" << endl;

        }
        else
        {
            EV<< "Multicast routing is not enable for this device (" << deviceType << " id=" << deviceId << ")" << endl;
        }

    }
    else if(stage == 4)
    {
        if (device == NULL)
            return;

        //////////////////////////
        /// IPv4 Configuration ///
        //////////////////////////
        // Adding default route requires routing table lookup to pick the right output
        // interface. This needs to be performed when all IPv4 addresses are already assigned
        // and there are matching records in the routing table.
        // ANSARoutingTable: dir. conn. routes are added by deviceConfigurator in stage 2
        // "inet" RoutingTable: dir. conn. routes are added by initialite() in the RoutingTable stage 3
        cXMLElement *gateway = device->getFirstChildWithTag("DefaultRouter");
        if (gateway == NULL && strcmp(deviceType, "Host") == 0)
            ev << "No IPv4 default-router configuration found for this device (" << deviceType << " id=" << deviceId << ")" << endl;
        else if (gateway != NULL ) {
            loadDefaultRouter(gateway);
            gateway = gateway->getNextSiblingWithTag("DefaultRouter");
            if (gateway)
                loadDefaultRouter(gateway);
        }


    }
    else if(stage == 10)
    {
        if(device == NULL)
            return;

        cXMLElement *iface = GetInterface(NULL, device);
        addIPv4MulticastGroups(iface);
        addIPv6MulticastGroups(iface);
    }
}


void ISISDeviceConfigurator::handleMessage(cMessage *msg){
   throw cRuntimeError("This module does not receive messages");
   delete msg;
}

void ISISDeviceConfigurator::loadStaticRouting6(cXMLElement *route){

   // for each static route
   while (route != NULL){

      // get network address string with prefix
      cXMLElement *network = route->getFirstChildWithTag("NetworkAddress");
      if (network == NULL){
         throw cRuntimeError("IPv6 network address for static route not set");
      }

      string addrFull = network->getNodeValue();
      IPv6Address addrNetwork;
      int prefixLen;

      // check if it's a valid IPv6 address string with prefix and get prefix
      if (!addrNetwork.tryParseAddrWithPrefix(addrFull.c_str(), prefixLen)){
         throw cRuntimeError("Unable to set IPv6 network address %s for static route", addrFull.c_str());
      }

      addrNetwork = IPv6Address(addrFull.substr(0, addrFull.find_last_of('/')).c_str());


      // get IPv6 next hop address string without prefix
      cXMLElement *nextHop = route->getFirstChildWithTag("NextHopAddress");
      if (nextHop == NULL){
         throw cRuntimeError("IPv6 next hop address for static route not set");
      }

      IPv6Address addrNextHop = IPv6Address(nextHop->getNodeValue());


      // optinal argument - administrative distance is set to 1 if not set
      cXMLElement *distance = route->getFirstChildWithTag("AdministrativeDistance");
      int adminDistance = 1;
      if (distance != NULL){
         if (!Str2Int(&adminDistance, distance->getNodeValue())){
            adminDistance = 0;
         }
      }

      if (adminDistance < 1 || adminDistance > 255){
         throw cRuntimeError("Invalid administrative distance for static route (%d)", adminDistance);
      }


      // current INET routing lookup implementation is not recursive
      // -> nextHop needs to be known network and we have to set output interface manually

      // browse connected routes and find one that matches next hop address
      const IPv6Route *record = rt6->doLongestPrefixMatch(addrNextHop);
      if (record == NULL){
         ev << "No directly connected route for IPv6 next hop address " << addrNextHop << " found" << endl;
      }else{
         // add static route
         rt6->addStaticRoute(addrNetwork, prefixLen, record->getInterfaceId(), addrNextHop, adminDistance);
      }


      // get next static route
      route = GetStaticRoute6(route, NULL);
   }
}

void ISISDeviceConfigurator::loadStaticRouting(cXMLElement* route)
{
    AnsaRoutingTable *ANSArt = dynamic_cast<AnsaRoutingTable *>(rt);

    while (route != NULL)
    {
        ANSAIPv4Route *ANSAStaticRoute = new ANSAIPv4Route();

        cXMLElementList ifDetails = route->getChildren();
        for (cXMLElementList::iterator routeElemIt = ifDetails.begin(); routeElemIt != ifDetails.end(); routeElemIt++)
        {
            std::string nodeName = (*routeElemIt)->getTagName();

            if (nodeName=="NetworkAddress")
            {
                ANSAStaticRoute->setDestination(IPv4Address((*routeElemIt)->getNodeValue()));
            }
            else if (nodeName=="NetworkMask")
            {
                ANSAStaticRoute->setNetmask(IPv4Address((*routeElemIt)->getNodeValue()));
            }
            else if (nodeName=="NextHopAddress")
            {
                ANSAStaticRoute->setGateway(IPv4Address((*routeElemIt)->getNodeValue()));
                InterfaceEntry *intf=NULL;
                for (int i=0; i<ift->getNumInterfaces(); i++)
                {
                    intf = ift->getInterface(i);
                    if ( ((intf->ipv4Data()->getIPAddress()).doAnd(intf->ipv4Data()->getNetmask())) ==
                         ((ANSAStaticRoute->getGateway()).doAnd(intf->ipv4Data()->getNetmask())) )
                        break;
                }

                if (intf)
                    ANSAStaticRoute->setInterface(intf);
                else
                    throw cRuntimeError("No exit interface found for the static route %s next hop.", (*routeElemIt)->getNodeValue());

                ANSAStaticRoute->setMetric(1);
            }
            else if (nodeName=="ExitInterface")
            {
                InterfaceEntry *ie=ift->getInterfaceByName((*routeElemIt)->getNodeValue());
                if (!ie)
                    throw cRuntimeError("Interface %s does not exists.", (*routeElemIt)->getNodeValue());

                ANSAStaticRoute->setInterface(ie);
                ANSAStaticRoute->setGateway(IPv4Address::UNSPECIFIED_ADDRESS);
                ANSAStaticRoute->setMetric(0);
            }
            else if (nodeName=="StaticRouteMetric")
            {
                ANSAStaticRoute->setMetric(atoi((*routeElemIt)->getNodeValue()));
            }
        }

        ANSAStaticRoute->setSource(IPv4Route::MANUAL);
        ANSAStaticRoute->setAdminDist(ANSAIPv4Route::dDirectlyConnected);

        //To the ANSA RoutingTable add ANSAIPv4Route, to the inet RoutingTable add IPv4Route
        if (ANSArt != NULL)
        {
            rt->addRoute(ANSAStaticRoute);
        }
        else
        {
            IPv4Route *staticRoute = new IPv4Route();
            staticRoute->setSource(ANSAStaticRoute->getSource());
            staticRoute->setDestination(ANSAStaticRoute->getDestination());
            staticRoute->setNetmask(ANSAStaticRoute->getNetmask());
            staticRoute->setGateway(ANSAStaticRoute->getGateway());
            staticRoute->setInterface(ANSAStaticRoute->getInterface());
            staticRoute->setMetric(ANSAStaticRoute->getMetric());

            rt->addRoute(staticRoute);
            delete ANSAStaticRoute;
        }

        route = GetStaticRoute(route, NULL);
    }
}

void ISISDeviceConfigurator::loadInterfaceConfig6(cXMLElement *iface){

   // for each interface node
   while (iface != NULL){

      // get interface name and find matching interface in interface table
      const char *ifaceName = iface->getAttribute("name");
      InterfaceEntry *ie = ift->getInterfaceByName(ifaceName);
      if (ie == NULL){
         //throw cRuntimeError("No interface called %s on this device", ifaceName);
         EV << "No interface called %s on this device" << ifaceName << endl;
         // get next interface
         iface = GetInterface(iface, NULL);
         continue;
      }

      // for each IPv6 address
      cXMLElement *addr = GetIPv6Address(NULL, iface);
      while (addr != NULL){

         // get address string
         string addrFull = addr->getNodeValue();
         IPv6Address ipv6;
         int prefixLen;

         // check if it's a valid IPv6 address string with prefix and get prefix
         if (!ipv6.tryParseAddrWithPrefix(addrFull.c_str(), prefixLen)){
            throw cRuntimeError("Unable to set IPv6 address %s on interface %s", addrFull.c_str(), ifaceName);
         }

         ipv6 = IPv6Address(addrFull.substr(0, addrFull.find_last_of('/')).c_str());

         // IPv6NeighbourDiscovery doesn't implement DAD for non-link-local addresses
         // -> we have to set the address as non-tentative
         ie->ipv6Data()->assignAddress(ipv6, false, 0, 0);

         //If rt6 is ANSARoutingTable6, than overridden addStaticRoute is called and ANSAIPv6Route is added
         //else inet IPv6Route is added
         // adding directly connected route to the routing table
         rt6->addStaticRoute(ipv6.getPrefix(prefixLen), prefixLen, ie->getInterfaceId(), IPv6Address::UNSPECIFIED_ADDRESS, 0);

         // get next IPv6 address
         addr = GetIPv6Address(addr, NULL);
      }

      setInterfaceParamters(ie); //TODO - verify

      // for each parameter
      for (cXMLElement *param = iface->getFirstChild(); param; param = param->getNextSibling()){

         if(strcmp(param->getTagName(), "NdpAdvSendAdvertisements") == 0){
            bool value = false;
            if (!Str2Bool(&value, param->getNodeValue())){
               throw cRuntimeError("Invalid NdpAdvSendAdvertisements value on interface %s", ie->getName());
            }
            ie->ipv6Data()->setAdvSendAdvertisements(value);
         }

         if(strcmp(param->getTagName(), "NdpMaxRtrAdvInterval") == 0){
            int value = 0;
            if (!Str2Int(&value, param->getNodeValue())){
               throw cRuntimeError("Unable to parse valid NdpMaxRtrAdvInterval %s on interface %s", value, ifaceName);
            }
            if (value < 4 || value > 1800){
               value = 600;
            }
            ie->ipv6Data()->setMaxRtrAdvInterval(value);
         }

         if(strcmp(param->getTagName(), "NdpMinRtrAdvInterval") == 0){
            int value = 0;
            if (!Str2Int(&value, param->getNodeValue())){
               throw cRuntimeError("Unable to parse valid NdpMinRtrAdvInterval %s on interface %s", value, ifaceName);
            }
            if (value < 3 || value > 1350){
               value = 200;
            }
            ie->ipv6Data()->setMinRtrAdvInterval(value);
         }
      }

      // for each IPv6 prefix
      cXMLElement *prefix = GetAdvPrefix(NULL, iface);
      while (prefix != NULL){

         // get address string
         string addrFull = prefix->getNodeValue();
         IPv6InterfaceData::AdvPrefix advPrefix;
         int prefixLen;

         // check if it's a valid IPv6 address string with prefix and get prefix
         if (!advPrefix.prefix.tryParseAddrWithPrefix(addrFull.c_str(), prefixLen)){
            throw cRuntimeError("Unable to parse IPv6 prefix %s on interface %s", addrFull.c_str(), ifaceName);
         }
         advPrefix.prefix =  IPv6Address(addrFull.substr(0, addrFull.find_last_of('/')).c_str());
         advPrefix.prefixLength = prefixLen;

         const char *validLifeTime = prefix->getAttribute("valid");
         const char *preferredLifeTime = prefix->getAttribute("preferred");
         int value;

         value = 2592000;
         if (validLifeTime != NULL){
            if (!Str2Int(&value, validLifeTime)){
               throw cRuntimeError("Unable to parse valid lifetime %s on IPv6 prefix %s on interface %s", validLifeTime, addrFull.c_str(), ifaceName);
            }
            advPrefix.advValidLifetime = value;
         }

         value = 604800;
         if (preferredLifeTime != NULL){
            if (!Str2Int(&value, preferredLifeTime)){
               throw cRuntimeError("Unable to parse preferred lifetime %s on IPv6 prefix %s on interface %s", preferredLifeTime, addrFull.c_str(), ifaceName);
            }
            advPrefix.advPreferredLifetime = value;
         }

         advPrefix.advOnLinkFlag = true;
         advPrefix.advAutonomousFlag = true;

         // adding prefix
         ie->ipv6Data()->addAdvPrefix(advPrefix);

         // get next IPv6 address
         prefix = GetAdvPrefix(prefix, NULL);
      }



      // get next interface
      iface = GetInterface(iface, NULL);
   }
}


void ISISDeviceConfigurator::loadDefaultRouter6(cXMLElement *gateway){

   if (gateway == NULL)
      return;

   // get default-router address string (without prefix)
   IPv6Address nextHop;
   nextHop = IPv6Address(gateway->getNodeValue());

   // browse routing table to find the best route to default-router
   const IPv6Route *route = rt6->doLongestPrefixMatch(nextHop);
   if (route == NULL){
      return;
   }

   // add default static route
   rt6->addStaticRoute(IPv6Address::UNSPECIFIED_ADDRESS, 0, route->getInterfaceId(), nextHop, 1);
}


void ISISDeviceConfigurator::loadPimInterfaceConfig(cXMLElement *iface)
{
    // for each interface node
    while (iface != NULL)
    {
        // get PIM node
        cXMLElement* pimNode = iface->getElementByPath("Pim");
        if (pimNode == NULL)
          break;                //FIXME it is break ok?

        // create new PIM interface
        PimInterface newentry;
        InterfaceEntry *interface = ift->getInterfaceByName(iface->getAttribute("name"));
        newentry.setInterfaceID(interface->getInterfaceId());
        newentry.setInterfacePtr(interface);

        // get PIM mode for interface
        cXMLElement* pimMode = pimNode->getElementByPath("Mode");
        if (pimMode == NULL)
          continue;

        const char *mode = pimMode->getNodeValue();
        //EV << "PimSplitter::PIM interface mode = "<< mode << endl;
        if (!strcmp(mode, "dense-mode"))
          newentry.setMode(Dense);
        else if (!strcmp(mode, "sparse-mode"))
          newentry.setMode(Sparse);
        else
          continue;

        // get PIM mode for interface
        cXMLElement* stateRefreshMode = pimNode->getElementByPath("StateRefresh");
        if (stateRefreshMode != NULL)
        {
            EV << "Nasel State Refresh" << endl;
            // will router send state refresh msgs?
            cXMLElement* origMode = stateRefreshMode->getElementByPath("OriginationInterval");
            if (origMode != NULL)
            {
                EV << "Nasel Orig" << endl;
                newentry.setSR(true);
            }
        }

        // register pim multicast address 224.0.0.13 (all PIM routers) on Pim interface
        std::vector<IPv4Address> intMulticastAddresses;

        //FIXME only for PIM-DM testing purposes
        cXMLElement* IPaddress = iface->getElementByPath("IPAddress");                  //Register 226.1.1.1 to R2 router
        std::string intfToRegister = IPaddress->getNodeValue();

        if (intfToRegister == "192.168.12.2" || intfToRegister == "192.168.22.2")
                interface->ipv4Data()->addMulticastListener(IPv4Address("226.1.1.1"));

        interface->ipv4Data()->addMulticastListener(IPv4Address("224.0.0.13"));
        intMulticastAddresses = interface->ipv4Data()->getReportedMulticastGroups();

        for(unsigned int i = 0; i < (intMulticastAddresses.size()); i++)
            EV << intMulticastAddresses[i] << ", ";
        EV << endl;

        newentry.setIntMulticastAddresses(newentry.deleteLocalIPs(intMulticastAddresses));

        // add new entry to pim interfaces table
        pimIft->addInterface(newentry);

        // get next interface
        iface = GetInterface(iface, NULL);
    }
}


void ISISDeviceConfigurator::loadDefaultRouter(cXMLElement *gateway)
{
    if (gateway == NULL)
      return;

    // get default-router address string (without prefix)
    IPv4Address nextHop;
    nextHop = IPv4Address(gateway->getNodeValue());

    // browse routing table to find the best route to default-router
    const IPv4Route *route = rt->findBestMatchingRoute(nextHop);
    if (route == NULL)
      return;

    AnsaRoutingTable *ANSArt = dynamic_cast<AnsaRoutingTable *>(rt);

    //To the ANSA RoutingTable add ANSAIPv4Route, to the inet RoutingTable add IPv4Route
    if (ANSArt != NULL)
    {
        ANSAIPv4Route *defaultRoute = new ANSAIPv4Route();
        defaultRoute->setSource(IPv4Route::MANUAL);
        defaultRoute->setDestination(IPv4Address());
        defaultRoute->setNetmask(IPv4Address());
        defaultRoute->setGateway(nextHop);
        defaultRoute->setInterface(route->getInterface());
        defaultRoute->setMetric(0);
        defaultRoute->setAdminDist(ANSAIPv4Route::dStatic);

        rt->addRoute(defaultRoute);
    }
    else
    {
        IPv4Route *defaultRoute = new IPv4Route();
        defaultRoute->setSource(IPv4Route::MANUAL);
        defaultRoute->setDestination(IPv4Address());
        defaultRoute->setNetmask(IPv4Address());
        defaultRoute->setGateway(nextHop);
        defaultRoute->setInterface(route->getInterface());
        defaultRoute->setMetric(0);

        rt->addRoute(defaultRoute);
    }
}

void ISISDeviceConfigurator::addIPv4MulticastGroups(cXMLElement *iface)
{
    while (iface != NULL)
    {
        // get MCastGroups Node
        cXMLElement* MCastGroupsNode = iface->getElementByPath("MCastGroups");
        if (MCastGroupsNode == NULL)
        {
            break;
        }



        //std::vector<IPv4Address> mcastGroupsList;
        InterfaceEntry *ie = ift->getInterfaceByName(iface->getAttribute("name"));
        bool empty = true;
        for (cXMLElement *mcastNode=MCastGroupsNode->getFirstChild(); mcastNode; mcastNode = mcastNode->getNextSibling())
        {
            const char *mcastAddress = mcastNode->getNodeValue();
            //mcastGroupsList.push_back((IPv4Address)mcastAddress);
            ie->ipv4Data()->joinMulticastGroup((IPv4Address)mcastAddress);
            empty = false;
        }
        if(empty)
            EV << "No Multicast Groups found for this interface " << iface->getAttribute("name") << " on this device: " <<  deviceType << " id=" << deviceId << endl;
        iface = GetInterface(iface, NULL);
    }

}

void ISISDeviceConfigurator::addIPv6MulticastGroups(cXMLElement *iface)
{
    while (iface != NULL)
    {
        // get MCastGroups Node
        cXMLElement* MCastGroupsNode6 = iface->getElementByPath("MCastGroups6");
        if (MCastGroupsNode6 == NULL)
        {
            break;
        }



        //std::vector<IPv4Address> mcastGroupsList;
        InterfaceEntry *ie = ift->getInterfaceByName(iface->getAttribute("name"));
        bool empty = true;
        for (cXMLElement *mcastNode6=MCastGroupsNode6->getFirstChild(); mcastNode6; mcastNode6 = mcastNode6->getNextSibling())
        {
            const char *mcastAddress6 = mcastNode6->getNodeValue();
            //mcastGroupsList.push_back((IPv4Address)mcastAddress);
            ie->ipv6Data()->joinMulticastGroup(IPv6Address(mcastAddress6));      //todo
            empty = false;
        }
        if(empty)
            EV << "No Multicast6 Groups found for this interface " << iface->getAttribute("name") << " on this device: " <<  deviceType << " id=" << deviceId << endl;
        iface = GetInterface(iface, NULL);
    }

}


void ISISDeviceConfigurator::loadInterfaceConfig(cXMLElement* iface)
{
    AnsaRoutingTable *ANSArt = dynamic_cast<AnsaRoutingTable *>(rt);

    while (iface != NULL)
    {
        // get interface name and find matching interface in interface table
        const char *ifaceName = iface->getAttribute("name");
        InterfaceEntry *ie = ift->getInterfaceByName(ifaceName);

        if (ie == NULL) {
           //throw cRuntimeError("No interface called %s on this device", ifaceName);
            EV << "No interface " << ifaceName << "called on this device" << endl;
            iface = GetInterface(iface, NULL);
            continue;
        }

        std::string ifaceType = std::string(ifaceName).substr(0,3);

        //implicitne nastavenia
        if (ifaceType=="eth")
            ie->setBroadcast(true);
        if (ifaceType=="ppp")
            ie->setPointToPoint(true);

        //register multicast groups
        if (strcmp("Router", deviceType) == 0)
        {//TODO: ???
            //ie->ipv4Data()->addMulticastListener(IPv4Address("224.0.0.1"));
            //ie->ipv4Data()->addMulticastListener(IPv4Address("224.0.0.2"));
        }

        ie->ipv4Data()->setMetric(1);
        ie->setMtu(1500);
        setInterfaceParamters(ie);

        int tempNumber;

        cXMLElementList ifDetails = iface->getChildren();
        for (cXMLElementList::iterator ifElemIt = ifDetails.begin(); ifElemIt != ifDetails.end(); ifElemIt++)
        {
            std::string nodeName = (*ifElemIt)->getTagName();

            if (nodeName=="IPAddress")
                ie->ipv4Data()->setIPAddress(IPv4Address((*ifElemIt)->getNodeValue()));

            if (nodeName=="Mask")
                ie->ipv4Data()->setNetmask(IPv4Address((*ifElemIt)->getNodeValue()));

            if (nodeName=="MTU")
                ie->setMtu(atoi((*ifElemIt)->getNodeValue()));

            if (nodeName=="Bandwidth")
            { // Bandwidth in kbps
                if (!Str2Int(&tempNumber, (*ifElemIt)->getNodeValue()))
                    throw cRuntimeError("Bad value for bandwidth on interface %s", ifaceName);
                ie->setBandwidth(tempNumber);
            }

            if (nodeName=="Delay")
            { // Delay in tens of microseconds
                if (!Str2Int(&tempNumber, (*ifElemIt)->getNodeValue()))
                    throw cRuntimeError("Bad value for delay on interface %s", ifaceName);
                ie->setDelay(tempNumber * 10);
            }
        }

        //Add directly connected routes to the ANSA rt version only
        //--- inet routing table adds dir. conn. routes in its own initialize method
        if (ANSArt != NULL)
        {
            ANSAIPv4Route *route = new ANSAIPv4Route();
            route->setSource(IPv4Route::IFACENETMASK);
            route->setDestination(ie->ipv4Data()->getIPAddress().doAnd(ie->ipv4Data()->getNetmask()));
            route->setNetmask(ie->ipv4Data()->getNetmask());
            route->setGateway(IPv4Address());
            route->setMetric(ie->ipv4Data()->getMetric());
            route->setAdminDist(ANSAIPv4Route::dDirectlyConnected);
            route->setInterface(ie);
            if (!(ie->ipv4Data()->getIPAddress().isUnspecified()
                    && ie->ipv4Data()->getNetmask() == IPv4Address::ALLONES_ADDRESS ) )
                ANSArt->addRoute(route);
        }

        iface = GetInterface(iface, NULL);
    }
}


void ISISDeviceConfigurator::setInterfaceParamters(InterfaceEntry *interface)
{
    int gateId = interface->getNodeOutputGateId();
    cModule *host = findContainingNode(interface->getInterfaceModule());
    cGate *outGw;
    cChannel *link;

    if (host != NULL && gateId != -1)
    { // Get link type
        outGw = host->gate(gateId);
        if ((link = outGw->getChannel()) == NULL)
            return;

        const char *linkType = link->getChannelType()->getName();
        cPar bwPar = link->par("datarate");

        // Bandwidth in kbps
        interface->setBandwidth(bwPar.doubleValue() / 1000);
        interface->setDelay(getDefaultDelay(linkType));
    }
}


double ISISDeviceConfigurator::getDefaultDelay(const char *linkType)
{
    if (!strcmp(linkType, "Eth10M"))
        return 1000;
    if (!strcmp(linkType, "Eth100M"))
        return 100;
    if (!strcmp(linkType, "Eth1G"))
        return 10;
    if (!strcmp(linkType, "Eth10G"))
        return 10;
    if (!strcmp(linkType, "Eth40G"))
        return 10;
    if (!strcmp(linkType, "Eth100G"))
        return 10;

    return 1000;    // ethernet 10M
}



cXMLElement * ISISDeviceConfigurator::GetDevice(const char *deviceType, const char *deviceId, const char *configFile){

   // get access to the XML file (if exists)
   cXMLElement *config = ev.getXMLDocument(configFile);
   if (config == NULL)
      return NULL;


   string type = deviceType;
   string id = deviceId;
   if (type.empty() || id.empty())
      return NULL;

   // create string that describes device node, such as <Router id="10.0.0.1">
   string deviceNodePath = type;
   deviceNodePath += "[@id='";
   deviceNodePath += id;
   deviceNodePath += "']";

   // get access to the device node
   cXMLElement *device = config->getElementByPath(deviceNodePath.c_str());

   return device;
}


cXMLElement * ISISDeviceConfigurator::GetInterface(cXMLElement *iface, cXMLElement *device){

   // initial call of the method - find <Interfaces> and get first "Interface" node
   if (device != NULL){

      cXMLElement *ifaces = device->getFirstChildWithTag("Interfaces");
      if (ifaces == NULL)
         return NULL;

      iface = ifaces->getFirstChildWithTag("Interface");

   // repeated call - get another "Interface" sibling node
   }else if (iface != NULL){
      iface = iface->getNextSiblingWithTag("Interface");
   }else{
      iface = NULL;
   }

   return iface;
}

cXMLElement * ISISDeviceConfigurator::GetIPv6Address(cXMLElement *addr, cXMLElement *iface){

   // initial call of the method - get first "IPv6Address" child node
   if (iface != NULL){
      addr = iface->getFirstChildWithTag("IPv6Address");

   // repeated call - get another "IPv6Address" sibling node
   }else if (addr != NULL){
      addr = addr->getNextSiblingWithTag("IPv6Address");
   }else{
      addr = NULL;
   }

   return addr;
}

cXMLElement * ISISDeviceConfigurator::GetAdvPrefix(cXMLElement *prefix, cXMLElement *iface){

   // initial call of the method - get first "NdpAdvPrefix" child node
   if (iface != NULL){
      prefix = iface->getFirstChildWithTag("NdpAdvPrefix");

   // repeated call - get another "NdpAdvPrefix" sibling node
   }else if (prefix != NULL){
      prefix = prefix->getNextSiblingWithTag("NdpAdvPrefix");
   }else{
      prefix = NULL;
   }

   return prefix;
}

cXMLElement * ISISDeviceConfigurator::GetStaticRoute6(cXMLElement *route, cXMLElement *device){

   // initial call of the method - find <Routing> -> <Static>
   // and then get first "Route" child node
   if (device != NULL){

      cXMLElement *routing = device->getFirstChildWithTag("Routing6");
      if (routing == NULL)
         return NULL;

      cXMLElement *staticRouting = routing->getFirstChildWithTag("Static");
      if (staticRouting == NULL)
         return NULL;

      route = staticRouting->getFirstChildWithTag("Route");

   // repeated call - get another "Route" sibling node
   }else if (route != NULL){
      route = route->getNextSiblingWithTag("Route");
   }else{
      route = NULL;
   }

   return route;
}

cXMLElement * ISISDeviceConfigurator::GetStaticRoute(cXMLElement *route, cXMLElement *device){

   // initial call of the method - find <Routing6> -> <Static>
   // and then get first "Route" child node
   if (device != NULL){

      cXMLElement *routing = device->getFirstChildWithTag("Routing");
      if (routing == NULL)
         return NULL;

      cXMLElement *staticRouting = routing->getFirstChildWithTag("Static");
      if (staticRouting == NULL)
         return NULL;

      route = staticRouting->getFirstChildWithTag("Route");

   // repeated call - get another "Route" sibling node
   }else if (route != NULL){
      route = route->getNextSiblingWithTag("Route");
   }else{
      route = NULL;
   }

   return route;
}



/*
 * A utility method for proper str -> int conversion with error checking.
 */
bool ISISDeviceConfigurator::Str2Int(int *retValue, const char *str){

   if (retValue == NULL || str == NULL){
      return false;
   }

   char *tail = NULL;
   long value = 0;
   errno = 0;

   value = strtol(str, &tail, 0);

   if (*tail != '\0' || errno == ERANGE || errno == EINVAL || value < INT_MIN || value > INT_MAX){
      return false;
   }

   *retValue = (int) value;
   return true;
}

bool ISISDeviceConfigurator::Str2Bool(bool *ret, const char *str){

   if (  (strcmp(str, "yes") == 0)
      || (strcmp(str, "enabled") == 0)
      || (strcmp(str, "on") == 0)
      || (strcmp(str, "true") == 0)){

      *ret = true;
      return true;
   }

   if (  (strcmp(str, "no") == 0)
      || (strcmp(str, "disabled") == 0)
      || (strcmp(str, "off") == 0)
      || (strcmp(str, "false") == 0)){

      *ret = false;
      return true;
   }

   int value;
   if (Str2Int(&value, str)){
      if (value == 1){
         *ret = true;
         return true;
      }

      if (value == 0){
         *ret = false;
         return true;
      }
   }

   return false;
}


bool ISISDeviceConfigurator::isMulticastEnabled(cXMLElement *device)
{
    // Routing element
    cXMLElement* routingNode = device->getElementByPath("Routing");
    if (routingNode == NULL)
         return false;

    // Multicast element
    cXMLElement* multicastNode = routingNode->getElementByPath("Multicast");
    if (multicastNode == NULL)
       return false;


    // Multicast has to be enabled
    const char* enableAtt = multicastNode->getAttribute("enable");
    if (strcmp(enableAtt, "1"))
        return false;

    return true;
}



void ISISDeviceConfigurator::loadISISConfig(ISIS *isisModule, ISIS::ISIS_MODE isisMode){

    /* init module pointers based on isisMode */

    if(isisMode == ISIS::L2_ISIS_MODE){
        //TRILL
        isisModule->setTrill(TRILLAccess().get());


        //RBridgeSplitter


    }else if(isisMode == ISIS::L3_ISIS_MODE){
        //IPv4
        //TODO C2

        //IPv6
        //TODO C2

    }else{
        throw cRuntimeError("Unsupported IS-IS mode");
    }

    //CLNSTable must be present in both modes
    isisModule->setClnsTable(CLNSTableAccess().get());

    //InterfaceTable
    isisModule->setIft(InterfaceTableAccess().get());

    //NotificationBoard
    isisModule->setNb(NotificationBoardAccess().get());
    isisModule->subscribeNb();

    /* end of module's pointers init */



    if(device == NULL){
        if(isisMode == ISIS::L3_ISIS_MODE){
            /* In L3 mode we need configuration (at least NET) */
            throw cRuntimeError("No configuration found for this device");
        }else{
            /* For L2 mode we load defaults ...
             * ... repeat after me zero-configuration. */
            this->loadISISCoreDefaultConfig(isisModule);
            this->loadISISInterfacesConfig(isisModule);
        }
        return;
    }

    cXMLElement *isisRouting = GetIsisRouting(device);
    if (isisRouting == NULL)
    {
        if(isisMode == ISIS::L3_ISIS_MODE){
            throw cRuntimeError("Can't find ISISRouting in config file");
        }
    }


    if (isisRouting != NULL)
    {
        //NET
        //TODO: multiple NETs for migrating purposes (merging, splitting areas)
        const char *netAddr = this->getISISNETAddress(isisRouting);
        if (netAddr != NULL)
        {
            isisModule->setNetAddr(netAddr);
        }
        else if (isisMode == ISIS::L2_ISIS_MODE)
        {
            isisModule->generateNetAddr();
        }
        else
        {
            throw cRuntimeError("Net address wasn't specified in IS-IS routing.");
        }

        //IS type {L1(L2_ISIS_MODE) | L2 | L1L2 default for L3_ISIS_MODE}
        if (isisMode == ISIS::L2_ISIS_MODE)
        {
            isisModule->setIsType(L1_TYPE);
        }
        else
        {
            isisModule->setIsType(this->getISISISType(isisRouting));
        }

        //L1 Hello interval
        isisModule->setL1HelloInterval(this->getISISL1HelloInterval(isisRouting));

        //L1 Hello multiplier
        isisModule->setL1HelloMultiplier(this->getISISL1HelloMultiplier(isisRouting));

        //L2 Hello interval
        isisModule->setL2HelloInterval(this->getISISL2HelloInterval(isisRouting));

        //L2 Hello multiplier
        isisModule->setL2HelloMultiplier(this->getISISL2HelloMultiplier(isisRouting));

        //LSP interval
        isisModule->setLspInterval(this->getISISLSPInterval(isisRouting));

        //LSP refresh interval
        isisModule->setLspRefreshInterval(this->getISISLSPRefreshInterval(isisRouting));

        //LSP max lifetime
        isisModule->setLspMaxLifetime(this->getISISLSPMaxLifetime(isisRouting));

        //L1 LSP generating interval
        isisModule->setL1LspGenInterval(this->getISISL1LSPGenInterval(isisRouting));

        //L2 LSP generating interval
        isisModule->setL2LspGenInterval(this->getISISL2LSPGenInterval(isisRouting));

        //L1 LSP send interval
        isisModule->setL1LspSendInterval(this->getISISL1LSPSendInterval(isisRouting));

        //L2 LSP send interval
        isisModule->setL2LspSendInterval(this->getISISL2LSPSendInterval(isisRouting));

        //L1 LSP initial waiting period
        isisModule->setL1LspInitWait(this->getISISL1LSPInitWait(isisRouting));

        //L2 LSP initial waiting period
        isisModule->setL2LspInitWait(this->getISISL2LSPInitWait(isisRouting));

        //L1 CSNP interval
        isisModule->setL1CSNPInterval(this->getISISL1CSNPInterval(isisRouting));

        //L2 CSNP interval
        isisModule->setL2CSNPInterval(this->getISISL2CSNPInterval(isisRouting));

        //L1 PSNP interval
        isisModule->setL1PSNPInterval(this->getISISL1PSNPInterval(isisRouting));

        //L2 PSNP interval
        isisModule->setL2PSNPInterval(this->getISISL2PSNPInterval(isisRouting));

        //L1 SPF Full interval
        isisModule->setL1SpfFullInterval(this->getISISL1SPFFullInterval(isisRouting));

        //L2 SPF Full interval
        isisModule->setL2SpfFullInterval(this->getISISL2SPFFullInterval(isisRouting));

        /* End of core module properties */
    }
    else
    {
        this->loadISISCoreDefaultConfig(isisModule);

    }
    /* Load configuration for interfaces */

    this->loadISISInterfacesConfig(isisModule);
    /* End of load configuration for interfaces */

}
void ISISDeviceConfigurator::loadISISInterfacesConfig(ISIS *isisModule){

    cXMLElement *interfaces = NULL;
    if (device != NULL)
    {
        interfaces = device->getFirstChildWithTag("Interfaces");
        if (interfaces == NULL)
        {
            EV
                    << "deviceId " << deviceId << ": <Interfaces></Interfaces> tag is missing in configuration file: \""
                            << configFile << "\"\n";
//        return;
        }
    }
    // add all interfaces to ISISIft vector containing additional information
//    InterfaceEntry *entryIFT = new InterfaceEntry(this); //TODO added "this" -> experimental
    for (int i = 0; i < ift->getNumInterfaces(); i++)
    {
        InterfaceEntry *ie = ift->getInterface(i);
        if (interfaces == NULL)
        {
            this->loadISISInterfaceDefaultConfig(isisModule, ie);
        }
        else
        {
            this->loadISISInterfaceConfig(isisModule, ie,
                    interfaces->getFirstChildWithAttribute("Interface", "name", ie->getName()));

        }
    }
}


void ISISDeviceConfigurator::loadISISCoreDefaultConfig(ISIS *isisModule){
    //NET

          isisModule->generateNetAddr();


      //IS type {L1(L2_ISIS_MODE) | L2 | L1L2 default for L3_ISIS_MODE}

      isisModule->setIsType(L1_TYPE);

      //set Attach flag
      isisModule->setAtt(false);


      //L1 Hello interval
      isisModule->setL1HelloInterval(ISIS_HELLO_INTERVAL);

      //L1 Hello multiplier
      isisModule->setL1HelloMultiplier(ISIS_HELLO_MULTIPLIER);

      //L2 Hello interval
      isisModule->setL2HelloInterval(ISIS_HELLO_INTERVAL);

      //L2 Hello multiplier
      isisModule->setL2HelloMultiplier(ISIS_HELLO_MULTIPLIER);

      //LSP interval
      isisModule->setLspInterval(ISIS_LSP_INTERVAL);

      //LSP refresh interval
      isisModule->setLspRefreshInterval(ISIS_LSP_REFRESH_INTERVAL);

      //LSP max lifetime
      isisModule->setLspMaxLifetime(ISIS_LSP_MAX_LIFETIME);

      //L1 LSP generating interval
      isisModule->setL1LspGenInterval(ISIS_LSP_GEN_INTERVAL);

      //L2 LSP generating interval
      isisModule->setL2LspGenInterval(ISIS_LSP_GEN_INTERVAL);

      //L1 LSP send interval
      isisModule->setL1LspSendInterval(ISIS_LSP_SEND_INTERVAL);

      //L2 LSP send interval
      isisModule->setL2LspSendInterval(ISIS_LSP_SEND_INTERVAL);

      //L1 LSP initial waiting period
      isisModule->setL1LspInitWait(ISIS_LSP_INIT_WAIT);

      //L2 LSP initial waiting period
      isisModule->setL2LspInitWait(ISIS_LSP_INIT_WAIT);

      //L1 CSNP interval
      isisModule->setL1CSNPInterval(ISIS_CSNP_INTERVAL);

      //L2 CSNP interval
      isisModule->setL2CSNPInterval(ISIS_CSNP_INTERVAL);

      //L1 PSNP interval
      isisModule->setL1PSNPInterval(ISIS_PSNP_INTERVAL);

      //L2 PSNP interval
      isisModule->setL2PSNPInterval(ISIS_PSNP_INTERVAL);

      //L1 SPF Full interval
      isisModule->setL1SpfFullInterval(ISIS_SPF_FULL_INTERVAL);

      //L2 SPF Full interval
      isisModule->setL2SpfFullInterval(ISIS_SPF_FULL_INTERVAL);
}


void ISISDeviceConfigurator::loadISISInterfaceDefaultConfig(ISIS *isisModule, InterfaceEntry *ie){

    ISISInterfaceData *d = new ISISInterfaceData();
        ISISinterface newIftEntry;
        newIftEntry.intID = ie->getInterfaceId();
        d->setIfaceId(ie->getInterfaceId());

        newIftEntry.gateIndex = ie->getNetworkLayerGateIndex();
        d->setGateIndex(ie->getNetworkLayerGateIndex());
        EV <<"deviceId: " << this->deviceId << "ISIS: adding interface, gateIndex: " <<newIftEntry.gateIndex <<endl;

        //set interface priority
        newIftEntry.priority = ISIS_DIS_PRIORITY;  //default value
        d->setPriority(ISIS_DIS_PRIORITY);

        /* Interface is NOT enabled by default. If ANY IS-IS related property is configured on interface then it's enabled. */
        newIftEntry.ISISenabled = false;
        d->setIsisEnabled(false);
        if(isisModule->getMode() == ISIS::L2_ISIS_MODE){
            newIftEntry.ISISenabled = true;
            d->setIsisEnabled(true);
        }

        //set network type (point-to-point vs. broadcast)
        newIftEntry.network = true; //default value means broadcast TODO check with TRILL default values
        d->setNetwork(ISIS_NETWORK_BROADCAST);

        //set interface metric

        newIftEntry.metric = ISIS_METRIC;    //default value
        d->setMetric(ISIS_METRIC);

        //set interface type according to global router configuration
        newIftEntry.circuitType = isisModule->getIsType();
        d->setCircuitType((ISISCircuitType)isisModule->getIsType()); //TODO B1


        //set L1 hello interval in seconds
        newIftEntry.L1HelloInterval = isisModule->getL1HelloInterval();
        d->setL1HelloInterval(isisModule->getL1HelloInterval());

        //set L1 hello multiplier
        newIftEntry.L1HelloMultiplier = isisModule->getL1HelloMultiplier();
        d->setL1HelloMultiplier(isisModule->getL1HelloMultiplier());


        //set L2 hello interval in seconds
        newIftEntry.L2HelloInterval = isisModule->getL2HelloInterval();
        d->setL2HelloInterval(isisModule->getL2HelloInterval());

        //set L2 hello multiplier
        newIftEntry.L2HelloMultiplier = isisModule->getL2HelloMultiplier();
        d->setL2HelloMultiplier(isisModule->getL2HelloMultiplier());

        //set lspInterval
        newIftEntry.lspInterval = isisModule->getLspInterval();
        d->setLspInterval(isisModule->getLspInterval());

        //set L1CsnpInterval
        newIftEntry.L1CsnpInterval = isisModule->getL1CsnpInterval();
        d->setL1CsnpInterval(isisModule->getL1CsnpInterval());

        //set L2CsnpInterval
        newIftEntry.L2CsnpInterval = isisModule->getL2CsnpInterval();
        d->setL2CsnpInterval(isisModule->getL2CsnpInterval());

        //set L1PsnpInterval
        newIftEntry.L1PsnpInterval = isisModule->getL1PsnpInterval();
        d->setL1PsnpInterval(isisModule->getL1PsnpInterval());

        //set L2PsnpInterval
        newIftEntry.L2PsnpInterval = isisModule->getL2PsnpInterval();
        d->setL2PsnpInterval(isisModule->getL2PsnpInterval());

        // priority is not needed for point-to-point, but it won't hurt
        // set priority of current DIS = me at start
        newIftEntry.L1DISpriority = newIftEntry.priority;
        d->setL1DisPriority(d->getPriority());
        newIftEntry.L2DISpriority = newIftEntry.priority;
        d->setL2DisPriority(d->getPriority());

        //set initial designated IS as himself

        memcpy(newIftEntry.L1DIS,isisModule->getSysId(), ISIS_SYSTEM_ID);
        //set LAN identifier; -99 is because, OMNeT starts numbering interfaces from 100 -> interfaceID 100 means LAN ID 0; and we want to start numbering from 1
        //newIftEntry.L1DIS[6] = ie->getInterfaceId() - 99;
        newIftEntry.L1DIS[ISIS_SYSTEM_ID] = isisModule->getISISIftSize() + 1;

        d->setL1Dis(newIftEntry.L1DIS);
        //do the same for L2 DIS

        memcpy(newIftEntry.L2DIS,isisModule->getSysId(), ISIS_SYSTEM_ID);
        //newIftEntry.L2DIS[6] = ie->getInterfaceId() - 99;
        newIftEntry.L2DIS[ISIS_SYSTEM_ID] = isisModule->getISISIftSize() + 1;
        d->setL2Dis(newIftEntry.L2DIS);

        /* By this time the trillData should be initialized.
         * So set the intial appointedForwaders to itself for configured VLAN(s).
         * TODO B5 add RFC reference and do some magic with vlanId, desiredVlanId, enabledVlans, ... */
        if(isisModule->getMode() == ISIS::L2_ISIS_MODE){
            ie->trillData()->addAppointedForwarder( ie->trillData()->getVlanId(), isisModule->getNickname());
        }
        newIftEntry.passive = false;
        d->setPassive(false);
        newIftEntry.entry = ie;

    //    this->ISISIft.push_back(newIftEntry);
        isisModule->appendISISInterface(newIftEntry);
        ie->setISISInterfaceData(d);
}


void ISISDeviceConfigurator::loadISISInterfaceConfig(ISIS *isisModule, InterfaceEntry *entry, cXMLElement *intElement){


    if(intElement == NULL){

        this->loadISISInterfaceDefaultConfig(isisModule, entry);
        return;
    }
    ISISinterface newIftEntry;
    newIftEntry.intID = entry->getInterfaceId();

    newIftEntry.gateIndex = entry->getNetworkLayerGateIndex();
    EV <<"deviceId: " << this->deviceId << "ISIS: adding interface, gateIndex: " <<newIftEntry.gateIndex <<endl;

    //set interface priority
    newIftEntry.priority = ISIS_DIS_PRIORITY;  //default value

    /* Interface is NOT enabled by default. If ANY IS-IS related property is configured on interface then it's enabled. */
    newIftEntry.ISISenabled = false;
    if(isisModule->getMode() == ISIS::L2_ISIS_MODE){
        newIftEntry.ISISenabled = true;
    }

    cXMLElement *priority = intElement->getFirstChildWithTag("ISIS-Priority");
    if (priority != NULL && priority->getNodeValue() != NULL)
    {
        newIftEntry.priority = (unsigned char) atoi(priority->getNodeValue());
        newIftEntry.ISISenabled = true;
    }


    //set network type (point-to-point vs. broadcast)

    newIftEntry.network = true; //default value

    cXMLElement *network = intElement->getFirstChildWithTag("ISIS-Network");
    if (network != NULL && network->getNodeValue() != NULL)
    {
        if (!strcmp(network->getNodeValue(), "point-to-point"))
        {
            newIftEntry.network = false;
            EV << "Interface network type is point-to-point " << network->getNodeValue() << endl;
        }
        else if (!strcmp(network->getNodeValue(), "broadcast"))
        {
            EV << "Interface network type is broadcast " << network->getNodeValue() << endl;
        }
        else
        {
            EV << "ERORR: Unrecognized interface's network type: " << network->getNodeValue() << endl;

        }
        newIftEntry.ISISenabled = true;

    }



    //set interface metric

    newIftEntry.metric = ISIS_METRIC;    //default value

        cXMLElement *metric = intElement->getFirstChildWithTag("ISIS-Metric");
        if(metric != NULL && metric->getNodeValue() != NULL)
        {
            newIftEntry.metric = (unsigned char) atoi(metric->getNodeValue());
            newIftEntry.ISISenabled = true;
        }




    //set interface type according to global router configuration
    switch(isisModule->getIsType())
    {
        case(L1_TYPE):
                 newIftEntry.circuitType = L1_TYPE;
                 break;
        case(L2_TYPE):
                 newIftEntry.circuitType = L2_TYPE;
                 break;
        //if router is type is equal L1L2, then interface configuration sets the type
        default: {

            newIftEntry.circuitType = L1L2_TYPE;

            cXMLElement *circuitType = intElement->getFirstChildWithTag("ISIS-Circuit-Type");
            if (circuitType != NULL && circuitType->getNodeValue() != NULL)
            {
                if (strcmp(circuitType->getNodeValue(), "L2") == 0){
                    newIftEntry.circuitType = L2_TYPE;
                }
                else
                {
                    if (strcmp(circuitType->getNodeValue(), "L1") == 0)
                        newIftEntry.circuitType = L1_TYPE;
                }
                newIftEntry.ISISenabled = true;
            }
            else
            {
                newIftEntry.circuitType = L1L2_TYPE;
            }

            break;
        }
    }

    //set L1 hello interval in seconds
    cXMLElement *L1HelloInt = intElement->getFirstChildWithTag(
            "ISIS-L1-Hello-Interval");
    if (L1HelloInt == NULL || L1HelloInt->getNodeValue() == NULL) {
        newIftEntry.L1HelloInterval = isisModule->getL1HelloInterval();
    } else {
        newIftEntry.L1HelloInterval = atoi(L1HelloInt->getNodeValue());
    }

    //set L1 hello multiplier
    cXMLElement *L1HelloMult = intElement->getFirstChildWithTag(
            "ISIS-L1-Hello-Multiplier");
    if (L1HelloMult == NULL || L1HelloMult->getNodeValue() == NULL) {
        newIftEntry.L1HelloMultiplier = isisModule->getL1HelloMultiplier();
    } else {
        newIftEntry.L1HelloMultiplier = atoi(L1HelloMult->getNodeValue());
    }

    //set L2 hello interval in seconds
    cXMLElement *L2HelloInt = intElement->getFirstChildWithTag(
            "ISIS-L2-Hello-Interval");
    if (L2HelloInt == NULL || L2HelloInt->getNodeValue() == NULL) {
        newIftEntry.L2HelloInterval = isisModule->getL2HelloInterval();
    } else {
        newIftEntry.L2HelloInterval = atoi(L2HelloInt->getNodeValue());
    }

    //set L2 hello multiplier
    cXMLElement *L2HelloMult = intElement->getFirstChildWithTag(
            "ISIS-L2-Hello-Multiplier");
    if (L2HelloMult == NULL || L2HelloMult->getNodeValue() == NULL) {
        newIftEntry.L2HelloMultiplier = isisModule->getL2HelloMultiplier();
    } else {
        newIftEntry.L2HelloMultiplier = atoi(L2HelloMult->getNodeValue());
    }

    //set lspInterval
    cXMLElement *cxlspInt = intElement->getFirstChildWithTag("ISIS-LSP-Interval");
    if (cxlspInt == NULL || cxlspInt->getNodeValue() == NULL)
    {
//        newIftEntry.lspInterval = ISIS_LSP_INTERVAL;
        newIftEntry.lspInterval = isisModule->getLspInterval();
    }
    else
    {
        newIftEntry.lspInterval = atoi(cxlspInt->getNodeValue());
    }

    //set L1CsnpInterval
    cXMLElement *cxL1CsnpInt = intElement->getFirstChildWithTag("ISIS-L1-CSNP-Interval");
    if (cxL1CsnpInt == NULL || cxL1CsnpInt->getNodeValue() == NULL)
    {
//        newIftEntry.L1CsnpInterval = ISIS_CSNP_INTERVAL;
                newIftEntry.L1CsnpInterval = isisModule->getL1CsnpInterval();
    }
    else
    {
        newIftEntry.L1CsnpInterval = atoi(cxL1CsnpInt->getNodeValue());
    }

    //set L2CsnpInterval
    cXMLElement *cxL2CsnpInt = intElement->getFirstChildWithTag("ISIS-L2-CSNP-Interval");
    if (cxL2CsnpInt == NULL || cxL2CsnpInt->getNodeValue() == NULL)
    {
//        newIftEntry.L2CsnpInterval = ISIS_CSNP_INTERVAL;
        newIftEntry.L2CsnpInterval = isisModule->getL2CsnpInterval();
    }
    else
    {
        newIftEntry.L2CsnpInterval = atoi(cxL2CsnpInt->getNodeValue());
    }

    //set L1PsnpInterval
    cXMLElement *cxL1PsnpInt = intElement->getFirstChildWithTag("ISIS-L1-PSNP-Interval");
    if (cxL1PsnpInt == NULL || cxL1PsnpInt->getNodeValue() == NULL)
    {
//        newIftEntry.L1PsnpInterval = ISIS_PSNP_INTERVAL;
        newIftEntry.L1PsnpInterval = isisModule->getL1PsnpInterval();
    }
    else
    {
        newIftEntry.L1PsnpInterval = atoi(cxL1PsnpInt->getNodeValue());
    }

    //set L2PsnpInterval
    cXMLElement *cxL2PsnpInt = intElement->getFirstChildWithTag("ISIS-L2-PSNP-Interval");
    if (cxL2PsnpInt == NULL || cxL2PsnpInt->getNodeValue() == NULL)
    {
//        newIftEntry.L2PsnpInterval = ISIS_PSNP_INTERVAL;
        newIftEntry.L2PsnpInterval = isisModule->getL2PsnpInterval();
    }
    else
    {
        newIftEntry.L2PsnpInterval = atoi(cxL2PsnpInt->getNodeValue());
    }


    // priority is not needed for point-to-point, but it won't hurt
    // set priority of current DIS = me at start
    newIftEntry.L1DISpriority = newIftEntry.priority;
    newIftEntry.L2DISpriority = newIftEntry.priority;

    //set initial designated IS as himself
//    this->copyArrayContent((unsigned char*)this->sysId, newIftEntry.L1DIS, ISIS_SYSTEM_ID, 0, 0);
    memcpy(newIftEntry.L1DIS,isisModule->getSysId(), ISIS_SYSTEM_ID);
    //set LAN identifier; -99 is because, OMNeT starts numbering interfaces from 100 -> interfaceID 100 means LAN ID 0; and we want to start numbering from 1
    //newIftEntry.L1DIS[6] = entry->getInterfaceId() - 99;
    newIftEntry.L1DIS[ISIS_SYSTEM_ID] = newIftEntry.gateIndex + 1;
    //do the same for L2 DIS
//    memcpy((unsigned char*)isisModule->getSy, newIftEntry.L2DIS, ISIS_SYSTEM_ID);
    memcpy(newIftEntry.L2DIS,isisModule->getSysId(), ISIS_SYSTEM_ID);
    //newIftEntry.L2DIS[6] = entry->getInterfaceId() - 99;
    newIftEntry.L2DIS[ISIS_SYSTEM_ID] = newIftEntry.gateIndex + 1;

    newIftEntry.passive = false;
    newIftEntry.entry = entry;

//    this->ISISIft.push_back(newIftEntry);
    isisModule->appendISISInterface(newIftEntry);
}




const char *ISISDeviceConfigurator::getISISNETAddress(cXMLElement *isisRouting)
{
    //TODO: multiple NETs for migrating purposes (merging, splitting areas)
    cXMLElement *net = isisRouting->getFirstChildWithTag("NET");
    if (net == NULL)
    {
//            EV << "deviceId " << deviceId << ": Net address wasn't specified in IS-IS routing\n";
//            throw cRuntimeError("Net address wasn't specified in IS-IS routing");
        return NULL;
    }
    return net->getNodeValue();
}

short int ISISDeviceConfigurator::getISISISType(cXMLElement *isisRouting){
    //set router IS type {L1(L2_M | L2 | L1L2 (default)}
    cXMLElement *routertype = isisRouting->getFirstChildWithTag("IS-Type");
    if (routertype == NULL) {
        return L1L2_TYPE;
    } else {
        const char* routerTypeValue = routertype->getNodeValue();
        if (routerTypeValue == NULL) {
            return L1L2_TYPE;
        } else {
            if (strcmp(routerTypeValue, "level-1") == 0) {
                return L1_TYPE;
            } else {
                if (strcmp(routerTypeValue, "level-2") == 0) {
                    return L2_TYPE;
                } else {
                    return L1L2_TYPE;
                }
            }
        }
    }
}

int ISISDeviceConfigurator::getISISL1HelloInterval(cXMLElement *isisRouting){
    //get L1 hello interval in seconds
    cXMLElement *L1HelloInt = isisRouting->getFirstChildWithTag(
            "L1-Hello-Interval");
    if (L1HelloInt == NULL || L1HelloInt->getNodeValue() == NULL) {
        return ISIS_HELLO_INTERVAL;
    } else {
        return atoi(L1HelloInt->getNodeValue());
    }
}

int ISISDeviceConfigurator::getISISL1HelloMultiplier(cXMLElement *isisRouting){
    //get L1 hello multiplier
    cXMLElement *L1HelloMult = isisRouting->getFirstChildWithTag(
            "L1-Hello-Multiplier");
    if (L1HelloMult == NULL || L1HelloMult->getNodeValue() == NULL) {
        return ISIS_HELLO_MULTIPLIER;
    } else {
        return atoi(L1HelloMult->getNodeValue());
    }
}

int ISISDeviceConfigurator::getISISL2HelloInterval(cXMLElement *isisRouting){
    //get L2 hello interval in seconds
    cXMLElement *L2HelloInt = isisRouting->getFirstChildWithTag(
            "L2-Hello-Interval");
    if (L2HelloInt == NULL || L2HelloInt->getNodeValue() == NULL) {
        return ISIS_HELLO_INTERVAL;
    } else {
        return atoi(L2HelloInt->getNodeValue());
    }
}

int ISISDeviceConfigurator::getISISL2HelloMultiplier(cXMLElement *isisRouting){
    //get L2 hello multiplier
    cXMLElement *L2HelloMult = isisRouting->getFirstChildWithTag(
            "L2-Hello-Multiplier");
    if (L2HelloMult == NULL || L2HelloMult->getNodeValue() == NULL) {
        return ISIS_HELLO_MULTIPLIER;
    } else {
        return atoi(L2HelloMult->getNodeValue());
    }
}

int ISISDeviceConfigurator::getISISLSPInterval(cXMLElement *isisRouting){
    //set lspInterval
    cXMLElement *cxlspInt = isisRouting->getFirstChildWithTag("LSP-Interval");
    if (cxlspInt == NULL || cxlspInt->getNodeValue() == NULL)
    {
        return ISIS_LSP_INTERVAL;
    }
    else
    {
        return atoi(cxlspInt->getNodeValue());
    }
}

int ISISDeviceConfigurator::getISISLSPRefreshInterval(cXMLElement *isisRouting){
    //get lspRefreshInterval
    cXMLElement *cxlspRefInt = isisRouting->getFirstChildWithTag("LSP-Refresh-Interval");
    if (cxlspRefInt == NULL || cxlspRefInt->getNodeValue() == NULL)
    {
        return ISIS_LSP_REFRESH_INTERVAL;
    }
    else
    {
        return atoi(cxlspRefInt->getNodeValue());
    }
}

int ISISDeviceConfigurator::getISISLSPMaxLifetime(cXMLElement *isisRouting)
{
    //get lspMaxLifetime
    cXMLElement *cxlspMaxLife = isisRouting->getFirstChildWithTag("LSP-Max-Lifetime");
    if (cxlspMaxLife == NULL || cxlspMaxLife->getNodeValue() == NULL)
    {
        return ISIS_LSP_MAX_LIFETIME;
    }
    else
    {
        return atoi(cxlspMaxLife->getNodeValue());
    }

}

int ISISDeviceConfigurator::getISISL1LSPGenInterval(cXMLElement *isisRouting)
{
    //set L1LspGenInterval (CISCO's
    cXMLElement *cxL1lspGenInt = isisRouting->getFirstChildWithTag("L1-LSP-Gen-Interval");
    if (cxL1lspGenInt == NULL || cxL1lspGenInt->getNodeValue() == NULL)
    {
        return ISIS_LSP_GEN_INTERVAL;
    }
    else
    {
        return atoi(cxL1lspGenInt->getNodeValue());
    }
}

int ISISDeviceConfigurator::getISISL2LSPGenInterval(cXMLElement *isisRouting)
{
    //get L2LspGenInterval (CISCO's
    cXMLElement *cxL2lspGenInt = isisRouting->getFirstChildWithTag("L2-LSP-Gen-Interval");
    if (cxL2lspGenInt == NULL || cxL2lspGenInt->getNodeValue() == NULL)
    {
        return ISIS_LSP_GEN_INTERVAL;
    }
    else
    {
        return atoi(cxL2lspGenInt->getNodeValue());
    }
}

int ISISDeviceConfigurator::getISISL1LSPSendInterval(cXMLElement *isisRouting){
    //get L1LspSendInterval
    cXMLElement *cxL1lspSendInt = isisRouting->getFirstChildWithTag("L1-LSP-Send-Interval");
    if (cxL1lspSendInt == NULL || cxL1lspSendInt->getNodeValue() == NULL)
    {
        return ISIS_LSP_SEND_INTERVAL;
    }
    else
    {
        return atoi(cxL1lspSendInt->getNodeValue());
    }
}

int ISISDeviceConfigurator::getISISL2LSPSendInterval(cXMLElement *isisRouting){
    //get L2LspSendInterval
    cXMLElement *cxL2lspSendInt = isisRouting->getFirstChildWithTag("L2-LSP-Send-Interval");
    if (cxL2lspSendInt == NULL || cxL2lspSendInt->getNodeValue() == NULL)
    {
        return ISIS_LSP_SEND_INTERVAL;
    }
    else
    {
        return atoi(cxL2lspSendInt->getNodeValue());
    }
}

int ISISDeviceConfigurator::getISISL1LSPInitWait(cXMLElement *isisRouting)
{
    //get L1LspInitWait
    cXMLElement *cxL1lspInitWait = isisRouting->getFirstChildWithTag("L1-LSP-Init-Wait");
    if (cxL1lspInitWait == NULL || cxL1lspInitWait->getNodeValue() == NULL)
    {
        return ISIS_LSP_INIT_WAIT;
    }
    else
    {
        return atoi(cxL1lspInitWait->getNodeValue());
    }
}


int ISISDeviceConfigurator::getISISL2LSPInitWait(cXMLElement *isisRouting)
{
    //get L2LspInitWait
    cXMLElement *cxL2lspInitWait = isisRouting->getFirstChildWithTag("L2-LSP-Init-Wait");
    if (cxL2lspInitWait == NULL || cxL2lspInitWait->getNodeValue() == NULL)
    {
        return ISIS_LSP_INIT_WAIT;
    }
    else
    {
        return atoi(cxL2lspInitWait->getNodeValue());
    }
}

int ISISDeviceConfigurator::getISISL1CSNPInterval(cXMLElement *isisRouting){
    //get L1CsnpInterval
    cXMLElement *cxL1CsnpInt = isisRouting->getFirstChildWithTag("L1-CSNP-Interval");
    if (cxL1CsnpInt == NULL || cxL1CsnpInt->getNodeValue() == NULL)
    {
        return ISIS_CSNP_INTERVAL;
    }
    else
    {
        return atoi(cxL1CsnpInt->getNodeValue());
    }

}

int ISISDeviceConfigurator::getISISL2CSNPInterval(cXMLElement *isisRouting){
    //get L2CsnpInterval
    cXMLElement *cxL2CsnpInt = isisRouting->getFirstChildWithTag("L2-CSNP-Interval");
    if (cxL2CsnpInt == NULL || cxL2CsnpInt->getNodeValue() == NULL)
    {
        return ISIS_CSNP_INTERVAL;
    }
    else
    {
        return atoi(cxL2CsnpInt->getNodeValue());
    }

}

int ISISDeviceConfigurator::getISISL1PSNPInterval(cXMLElement *isisRouting){
    //get L1PSNPInterval
    cXMLElement *cxL1PSNPInt = isisRouting->getFirstChildWithTag("L1-PSNP-Interval");
    if (cxL1PSNPInt == NULL || cxL1PSNPInt->getNodeValue() == NULL)
    {
        return ISIS_PSNP_INTERVAL;
    }
    else
    {
        return atoi(cxL1PSNPInt->getNodeValue());
    }

}

int ISISDeviceConfigurator::getISISL2PSNPInterval(cXMLElement *isisRouting){
    //get L2PSNPInterval
    cXMLElement *cxL2PSNPInt = isisRouting->getFirstChildWithTag("L2-PSNP-Interval");
    if (cxL2PSNPInt == NULL || cxL2PSNPInt->getNodeValue() == NULL)
    {
        return ISIS_PSNP_INTERVAL;
    }
    else
    {
        return atoi(cxL2PSNPInt->getNodeValue());
    }

}

int ISISDeviceConfigurator::getISISL1SPFFullInterval(cXMLElement *isisRouting){
    //get L1SPFFullInterval
    cXMLElement *cxL1SPFFullInt = isisRouting->getFirstChildWithTag("L1-SPF-Full-Interval");
    if (cxL1SPFFullInt == NULL || cxL1SPFFullInt->getNodeValue() == NULL)
    {
        return ISIS_SPF_FULL_INTERVAL;
    }
    else
    {
        return atoi(cxL1SPFFullInt->getNodeValue());
    }

}

int ISISDeviceConfigurator::getISISL2SPFFullInterval(cXMLElement *isisRouting){
    //get L2SPFFullInterval
    cXMLElement *cxL2SPFFullInt = isisRouting->getFirstChildWithTag("L2-SPF-Full-Interval");
    if (cxL2SPFFullInt == NULL || cxL2SPFFullInt->getNodeValue() == NULL)
    {
        return ISIS_SPF_FULL_INTERVAL;
    }
    else
    {
        return atoi(cxL2SPFFullInt->getNodeValue());
    }

}


/* Check if IS-IS is enabled in XML config.
 * Return NULL if not presented, otherwise return main IS-IS element
 */
cXMLElement * ISISDeviceConfigurator::GetIsisRouting(cXMLElement * device)
{
    if(device == NULL)
        return NULL;

    cXMLElement *routing = device->getFirstChildWithTag("Routing");
    if(routing == NULL)
        return  NULL;

    cXMLElement * isis = routing->getFirstChildWithTag("ISIS");
    return isis;
}
