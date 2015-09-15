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
 * @file AnsaIPv4Route.cc
 * @date 25.1.2013
 * @author Veronika Rybova, Tomas Prochazka (mailto:xproch21@stud.fit.vutbr.cz),
 * Vladimir Vesely (mailto:ivesely@fit.vutbr.cz)
 * @brief Inherited class from inet::IPv4Route for PIM purposes
 * @detail File implements new functions for inet::IPv4Route which are needed for PIM
 */

#include "ansa/networklayer/ipv4/AnsaIPv4Route.h"

std::string ANSAIPv4Route::info() const
{
    std::stringstream out;

    out << getRouteSrcName();
    out << " ";
    if (getDestination().isUnspecified())
        out << "0.0.0.0";
    else
        out << getDestination();

    out << "/";
    if (getNetmask().isUnspecified())
        out << "0";
    else
        out << getNetmask().getNetmaskLength();

    if (getGateway().isUnspecified())
    {
        out << " is directly connected";
    }
    else
    {
        out << " [" << getAdminDist() << "/" << getMetric() << "]";
        out << " via ";
        out << getGateway();
    }

    out << ", " << getInterfaceName();

    return out.str();
}

std::string ANSAIPv4Route::detailedInfo() const
{
    return std::string();
}

const char *ANSAIPv4Route::getRouteSrcName() const
{
    switch (getSourceType())
    {
        case MANUAL:
        case IFACENETMASK:
            if (getGateway().isUnspecified())
                return "C";
            return "S";

        default:
            switch(getRoutingProtocolSource())
            {
                case pIGRP: return "I";
                case pRIP: return "R";
                case pEGP: return "E";
                case pBGP: return "B";
                case pISISderived: return "i";
                case pISIS: return "ia";
                case pOSPF: return "O";
                case pOSPFinter: return "IA";
                case pOSPFext1: return "E1";
                case pOSPFext2: return "E2";
                case pOSPFNSSAext1: return "N1";
                case pOSPFNSSAext2: return "N2";
                case pEIGRP: return "D";
                case pEIGRPext: return "EX";
                case pBABEL: return "BA";
                default: return "?";
            }
    }
}


AnsaIPv4MulticastRoute::AnsaIPv4MulticastRoute()
{
    inInt.intPtr = NULL;
    grt = NULL;
    sat = NULL;
    srt = NULL;
    rst = NULL;
    kat = NULL;
    et = NULL;
    jt = NULL;
    ppt = NULL;

    RP = inet::IPv4Address::UNSPECIFIED_ADDRESS;
    sequencenumber = 0;

    this->setRoutingTable(NULL);
    /**
     * Migration towards ANSAINET2.2
     */
    //this->setParent(NULL);
    this->setSourceType(MANUAL);
    this->setMetric(0);
}

std::string AnsaIPv4MulticastRoute::info() const
{
    std::stringstream out;
    out << "(";
    if (this->getOrigin().isUnspecified()) out << "*  "; else out << this->getOrigin() << ",  ";
    if (this->getMulticastGroup().isUnspecified()) out << "*  "; else out << this->getMulticastGroup() << "),  ";
    if (RP.isUnspecified()) out << "0.0.0.0"<< endl; else out << "RP is " << RP << endl;
    out << "Incoming interface: ";
    if(inInt.intPtr != NULL)
    {
        if (inInt.intPtr) out << inInt.intPtr->getName() << ", ";
        out << "RPF neighbor " << inInt.nextHop << endl;
        out << "Outgoing interface list:" << endl;
    }

    for (InterfaceVector::const_iterator i = outInt.begin(); i < outInt.end(); i++)
    {
        if ((*i).intPtr) out << (*i).intPtr->getName() << ", ";
        if (i->forwarding == Forward) out << "Forward/"; else out << "Pruned/";
        if (i->mode == Densemode) out << "Dense"; else out << "Sparse";
        out << endl;
    }

    return out.str();
}

bool AnsaIPv4MulticastRoute::isFlagSet(flag fl)
{
    for(unsigned int i = 0; i < flags.size(); i++)
    {
        if (flags[i] == fl)
            return true;
    }
    return false;
}

void AnsaIPv4MulticastRoute::addFlag(flag fl)
{
    if (!isFlagSet(fl))
        flags.push_back(fl);
}

void AnsaIPv4MulticastRoute::removeFlag(flag fl)
{
    for(unsigned int i = 0; i < flags.size(); i++)
    {
        if (flags[i] == fl)
        {
            flags.erase(flags.begin() + i);
            return;
        }
    }
}

void AnsaIPv4MulticastRoute::setRegStatus(int intId, RegisterState regState)
{
    unsigned int i;
    for (i = 0; i < outInt.size(); i++)
    {
        if (outInt[i].intId == intId)
        {
            outInt[i].regState = regState;
            break;
        }

    }
}

AnsaIPv4MulticastRoute::RegisterState AnsaIPv4MulticastRoute::getRegStatus(int intId)
{
    unsigned int i;
    for (i = 0; i < outInt.size(); i++)
    {
        if (outInt[i].intId == intId)
            break;
    }
    return outInt[i].regState;
}

int AnsaIPv4MulticastRoute::getOutIdByIntId(int intId)
{
    unsigned int i;
    for (i = 0; i < outInt.size(); i++)
    {
        if (outInt[i].intId == intId)
            break;
    }
    return i;
}

bool AnsaIPv4MulticastRoute::outIntExist(int intId)
{
    unsigned int i;
    for (i = 0; i < outInt.size(); i++)
    {
        if (outInt[i].intId == intId)
            return true;
    }
    return false;
}

bool AnsaIPv4MulticastRoute::isOilistNull()
{
    bool olistNull = true;
    for (unsigned int i = 0; i < outInt.size(); i++)
    {
        if (outInt[i].forwarding == Forward)
        {
            olistNull = false;
            break;
        }
    }
    return olistNull;
}

void AnsaIPv4MulticastRoute::addOutIntFull(inet::InterfaceEntry *intPtr, int intId, intState forwading, intState mode, PIMpt *pruneTimer,
                                                PIMet *expiryTimer, AssertState assert, RegisterState regState, bool show)
{
    outInterface outIntf;

    outIntf.intId = intId;
    outIntf.intPtr = intPtr;
    outIntf.forwarding = forwading;
    outIntf.mode = mode;
    outIntf.pruneTimer = NULL;
    outIntf.pruneTimer = pruneTimer;
    outIntf.expiryTimer = expiryTimer;
    outIntf.regState = regState;
    outIntf.assert = assert;
    outIntf.shRegTun = show;

    this->outInt.push_back(outIntf);
}

void AnsaIPv4MulticastRoute::addFlags(flag fl1, flag fl2, flag fl3,flag fl4)
{
    if (fl1 != NO_FLAG && !isFlagSet(fl1))
        flags.push_back(fl1);
    if (fl2 != NO_FLAG && !isFlagSet(fl2))
        flags.push_back(fl2);
    if (fl3 != NO_FLAG && !isFlagSet(fl3))
        flags.push_back(fl3);
    if (fl4 != NO_FLAG && !isFlagSet(fl4))
        flags.push_back(fl4);
}

void AnsaIPv4MulticastRoute::setAddresses(inet::IPv4Address multOrigin, inet::IPv4Address multGroup, inet::IPv4Address RP)
{
    this->RP = RP;
    this->setMulticastGroup(multGroup);
    this->setOrigin(multOrigin);
}
