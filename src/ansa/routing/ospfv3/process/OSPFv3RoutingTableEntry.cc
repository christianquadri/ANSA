#include "ansa/routing/ospfv3/process/OSPFv3RoutingTableEntry.h"

namespace inet{
OSPFv3RoutingTableEntry::OSPFv3RoutingTableEntry(IInterfaceTable *ift, IPv6Address destPrefix, int prefixLength, SourceType sourceType) :
        IPv6Route(destPrefix, prefixLength, sourceType),
        ift(ift),
        area(BACKBONE_AREAID),
        pathType(OSPFv3RoutingTableEntry::INTRAAREA)
{
    this->setPrefixLength(128);
    this->setSourceType(IRoute::OSPF);
}

//OSPFv3RoutingTableEntry::OSPFv3RoutingTableEntry(IInterfaceTable *_ift) : IPv6Route(IPv6Address destPrefix, int prefixLength, int sourceType),
//    ift(_ift),
//    destinationType(OSPFv3RoutingTableEntry::NETWORK_DESTINATION),
//    area(BACKBONE_AREAID),
//    pathType(OSPFv3RoutingTableEntry::INTRAAREA)
//{
//    setNetmask(IPv4Address::ALLONES_ADDRESS);
//    setSourceType(IRoute::OSPF);
//}

OSPFv3RoutingTableEntry::OSPFv3RoutingTableEntry(const OSPFv3RoutingTableEntry& entry, IPv6Address destPrefix, int prefixLength, SourceType sourceType) :
        IPv6Route(destPrefix, prefixLength, sourceType),
    destinationType(entry.destinationType),
    optionalCapabilities(entry.optionalCapabilities),
    area(entry.area),
    pathType(entry.pathType),
    cost(entry.cost),
    type2Cost(entry.type2Cost),
    linkStateOrigin(entry.linkStateOrigin),
    nextHops(entry.nextHops)
{
    this->setDestination(entry.getDestinationAsGeneric());
    this->setPrefixLength(prefixLength);
    this->setInterface(entry.getInterface());
    this->setSourceType(entry.getSourceType());
    this->setMetric(entry.getMetric());
//    this->setDestination(entry.getDestination());
//    setNetmask(entry.getNetmask());
//    setGateway(entry.getGateway());
//    setInterface(entry.getInterface());
//    setSourceType(entry.getSourceType());
//    setMetric(entry.getMetric());
}

void OSPFv3RoutingTableEntry::setPathType(RoutingPathType type)
{
    pathType = type;
    // FIXME: this is a hack. But the correct way to do it is to implement a separate IIPv4RoutingTable module for OSPF...
    if (pathType == OSPFv3RoutingTableEntry::TYPE2_EXTERNAL) {
        setMetric(cost + type2Cost * 1000);
    }
    else {
        setMetric(cost);
    }
}

void OSPFv3RoutingTableEntry::setCost(Metric pathCost)
{
    cost = pathCost;
    // FIXME: this is a hack. But the correct way to do it is to implement a separate IIPv4RoutingTable module for OSPF...
    if (pathType == OSPFv3RoutingTableEntry::TYPE2_EXTERNAL) {
        setMetric(cost + type2Cost * 1000);
    }
    else {
        setMetric(cost);
    }
}

void OSPFv3RoutingTableEntry::setType2Cost(Metric pathCost)
{
    type2Cost = pathCost;
    // FIXME: this is a hack. But the correct way to do it is to implement a separate IIPv4RoutingTable module for OSPF...
    if (pathType == OSPFv3RoutingTableEntry::TYPE2_EXTERNAL) {
        setMetric(cost + type2Cost * 1000);
    }
    else {
        setMetric(cost);
    }
}

void OSPFv3RoutingTableEntry::addNextHop(NextHop hop)
{
    if (nextHops.size() == 0) {
        InterfaceEntry *routingInterface = ift->getInterfaceById(hop.ifIndex);

        setInterface(routingInterface);
        // TODO: this used to be commented out, but it seems we need it
        // otherwise gateways will never be filled in and gateway is needed for broadcast networks
        //setGateway(hop.hopAddress);
    }
    nextHops.push_back(hop);
}

bool OSPFv3RoutingTableEntry::operator==(const OSPFv3RoutingTableEntry& entry) const
{
    unsigned int hopCount = nextHops.size();
    unsigned int i = 0;

    if (hopCount != entry.nextHops.size()) {
        return false;
    }
    for (i = 0; i < hopCount; i++) {
        if ((nextHops[i] != entry.nextHops[i])) {
            return false;
        }
    }

    return (this->destinationType == entry.destinationType) &&
           (this->getDestinationAsGeneric() == entry.getDestinationAsGeneric()) &&
           (this->getPrefixLength() == entry.getPrefixLength()) &&
           (this->optionalCapabilities == entry.optionalCapabilities) &&
           (this->area == entry.area) &&
           (this->pathType == entry.pathType) &&
           (this->cost == entry.cost) &&
           (this->type2Cost == entry.type2Cost) &&
           (this->linkStateOrigin == entry.linkStateOrigin);
}

std::ostream& operator<<(std::ostream& out, const OSPFv3RoutingTableEntry& entry)
{
    out << "Destination: " << entry.getDestinationAsGeneric() << "/" << entry.getPrefixLength() << " (";
    if (entry.getDestinationType() == OSPFv3RoutingTableEntry::NETWORK_DESTINATION) {
        out << "Network";
    }
    else {
        if ((entry.getDestinationType() & OSPFv3RoutingTableEntry::AREA_BORDER_ROUTER_DESTINATION) != 0) {
            out << "AreaBorderRouter";
        }
        if ((entry.getDestinationType() & OSPFv3RoutingTableEntry::AS_BOUNDARY_ROUTER_DESTINATION) != 0) {
            if ((entry.getDestinationType() & OSPFv3RoutingTableEntry::AREA_BORDER_ROUTER_DESTINATION) != 0) {
                out << "+";
            }
            out << "ASBoundaryRouter";
        }
    }
    out << "), Area: "
        << entry.getArea().str(false)
        << ", PathType: ";
    switch (entry.getPathType()) {
        case OSPFv3RoutingTableEntry::INTRAAREA:
            out << "IntraArea";
            break;

        case OSPFv3RoutingTableEntry::INTERAREA:
            out << "InterArea";
            break;

        case OSPFv3RoutingTableEntry::TYPE1_EXTERNAL:
            out << "Type1External";
            break;

        case OSPFv3RoutingTableEntry::TYPE2_EXTERNAL:
            out << "Type2External";
            break;

        default:
            out << "Unknown";
            break;
    }
    out << ", iface: " << entry.getInterface()->getName();
    out << ", Cost: " << entry.getCost()
        << ", Type2Cost: " << entry.getType2Cost()
        //TODO<< ", Origin: [" << entry.getLinkStateOrigin()->getHeader()
        << "], NextHops: ";

    unsigned int hopCount = entry.getNextHopCount();
    for (unsigned int i = 0; i < hopCount; i++) {
        out << entry.getNextHop(i).hopAddress << " ";
    }

    return out;
}
}