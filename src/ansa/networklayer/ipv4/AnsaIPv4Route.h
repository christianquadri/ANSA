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
 * @author Veronika Rybova, Tomas Prochazka (mailto:xproch21@stud.fit.vutbr.cz), Jiri Trhlik (mailto:JiriTM@gmail.com)
 * Vladimir Vesely (mailto:ivesely@fit.vutbr.cz)
 * @brief Inherited class from inet::IPv4Route for PIM purposes
 * @detail File implements new functions for inet::IPv4Route which are needed for PIM
 */

#ifndef ANSAIPV4ROUTE_H_
#define ANSAIPV4ROUTE_H_

#include "networklayer/ipv4/IPv4Route.h"

#include "ansa/networklayer/pim/PIMTimer_m.h"
#include "networklayer/common/InterfaceEntry.h"

/**
 * ANSAIPv4 unicast route in IRoutingTable.
 */
class ANSAIPv4Route : public inet::IPv4Route
{
    public:
        /** Should be set if route source is a "routing protocol" **/
        enum RoutingProtocolSource
        {
            pUnknown = 0,
            pIGRP,           //IGRP derived  - I
            pRIP,            //RIP           - R
            pEGP,            //EGP derived   - E
            pBGP,            //BGP derived   - B
            pISISderived,    //IS-IS derived - i
            pISIS,           //IS-IS         - ia
            pOSPF,           //OSPF derived                    - O
            pOSPFinter,      //OSPF inter area route           - IA
            pOSPFext1,       //OSPF external type 1 route      - E1
            pOSPFext2,       //OSPF external type 2 route      - E2
            pOSPFNSSAext1,   //OSPF NSSA external type 1 route - N1
            pOSPFNSSAext2,   //OSPF NSSA external type 2 route - N2
            pEIGRP,          //EIGRP          - D
            pEIGRPext,       //EIGRP external - EX
            pBABEL           //BABEL
        };

        /** Cisco like administrative distances */
        enum RouteAdminDist
        {
            dDirectlyConnected = 0,
            dStatic = 1,
            dEIGRPSummary = 5,
            dBGPExternal = 20,
            dEIGRPInternal = 90,
            dIGRP = 100,
            dOSPF = 110,
            dISIS = 115,
            dRIP = 120,
            dBABEL = 125,
            dEGP = 140,
            dODR = 160,
            dEIGRPExternal = 170,
            dBGPInternal = 200,
            dDHCPlearned = 254,
            dUnknown = 255
        };

        //Some codes are defined in @class inet::IPv4Route!!
        enum {F_ADMINDIST = 10, F_ROUTINGPROTSOURCE = 11}; // field codes for changed()

    protected:
        unsigned int _adminDist;
        /** Should be set if route source is a "routing protocol" **/
        RoutingProtocolSource _routingProtocolSource;

    public:
        ANSAIPv4Route() : inet::IPv4Route() { _adminDist = dUnknown; _routingProtocolSource = pUnknown; }
        virtual ~ANSAIPv4Route() {}
        virtual std::string info() const;
        virtual std::string detailedInfo() const;

        virtual const char *getRouteSrcName() const;
        unsigned int getAdminDist() const  { return _adminDist; }
        RoutingProtocolSource getRoutingProtocolSource() const { return _routingProtocolSource; }

        void setAdminDist(unsigned int adminDist)  { if (adminDist != _adminDist) { _adminDist = adminDist; changed(F_ADMINDIST);} }
        void setRoutingProtocolSource(RoutingProtocolSource routingProtocolSource) { if (routingProtocolSource != _routingProtocolSource) { _routingProtocolSource = routingProtocolSource; changed(F_ROUTINGPROTSOURCE);} }
};


/**
 * ANSAIPv4 multicast route in IRoutingTable.
 */
class INET_API AnsaIPv4MulticastRoute : public inet::IPv4MulticastRoute
{
    public:
        /** Route flags. Added to each route. */
        enum flag
        {
            NO_FLAG,
            D,              /**< Dense */
            S,              /**< Sparse */
            C,              /**< Connected */
            P,              /**< Pruned */
            A,              /**< Source is directly connected */
            F,              /**< Register flag*/
            T               /**< SPT bit*/
        };

        /** States of each outgoing interface. E.g.: Forward/Dense. */
        enum intState
        {
            Densemode = 1,
            Sparsemode = 2,
            Forward,
            Pruned
        };

        /** Assert States of each outgoing interface. */
        enum AssertState
        {
            NoInfo = 0,
            Winner = 1,
            Loser = 2
        };

        /**  Register machine States. */
        enum RegisterState
        {
          NoInfoRS = 0,
          Join = 1,
          Prune = 2,
          JoinPending = 3
        };

        /**
         * @brief Structure of incoming interface.
         * @details E.g.: GigabitEthernet1/4, RPF nbr 10.10.51.145
         */
        struct inInterface
        {
            inet::InterfaceEntry    *intPtr;            /**< Pointer to interface */
            int                     intId;              /**< Interface ID */
            inet::IPv4Address       nextHop;            /**< RF neighbor */
        };

        /**
         * @brief Structure of outgoing interface.
         * @details E.g.: Ethernet0, Forward/Sparse, 5:29:15/0:02:57
         */
        struct outInterface
        {
            inet::InterfaceEntry    *intPtr;            /**< Pointer to interface */
            int                     intId;              /**< Interface ID */
            intState                forwarding;         /**< Forward or Pruned */
            intState                mode;               /**< Dense, Sparse, ... */
            PIMpt                   *pruneTimer;        /**< Pointer to PIM Prune Timer*/
            PIMet                   *expiryTimer;       /**< Pointer to PIM Expiry Timer*/
            AssertState             assert;             /**< Assert state. */
            RegisterState           regState;           /**< Register state. */
            bool                    shRegTun;           /**< Show interface which is also register tunnel interface*/
        };

        /** Vector of outgoing interfaces. */
        typedef std::vector<outInterface> InterfaceVector;


    private:
        inet::IPv4Address           RP;                     /**< Randevous point */
        std::vector<flag>           flags;                  /**< Route flags */
        // timers
        PIMgrt                      *grt;                   /**< Pointer to Graft Retry Timer*/
        PIMsat                      *sat;                   /**< Pointer to Source Active Timer*/
        PIMsrt                      *srt;                   /**< Pointer to State Refresh Timer*/
        PIMkat                      *kat;                   /**< Pointer to Keep Alive timer for PIM-SM*/
        PIMrst                      *rst;                   /**< Pointer to Register-stop timer for PIM-SM*/
        PIMet                       *et;                    /**< Pointer to Expiry timer for PIM-SM*/
        PIMjt                       *jt;                    /**< Pointer to Join timer*/
        PIMppt                      *ppt;                   /**< Pointer to Prune Pending Timer*/
        // interfaces
        inInterface                 inInt;                  /**< Incoming interface */
        InterfaceVector             outInt;                 /**< Outgoing interface */

        //Originated from destination.Ensures loop freeness.
        unsigned int sequencenumber;
        //Time of routing table entry creation
        simtime_t installtime;

    public:
        AnsaIPv4MulticastRoute();                                                 /**< Set all pointers to null */
        virtual ~AnsaIPv4MulticastRoute() {}
        virtual std::string info() const;

    public:
        void setRP(inet::IPv4Address RP)  {this->RP = RP;}                        /**< Set RP IP address */
        void setGrt (PIMgrt *grt)   {this->grt = grt;}                      /**< Set pointer to PimGraftTimer */
        void setSat (PIMsat *sat)   {this->sat = sat;}                      /**< Set pointer to PimSourceActiveTimer */
        void setSrt (PIMsrt *srt)   {this->srt = srt;}                      /**< Set pointer to PimStateRefreshTimer */
        void setKat (PIMkat *kat)   {this->kat = kat;}                      /**< Set pointer to KeepAliveTimer */
        void setRst (PIMrst *rst)   {this->rst = rst;}                      /**< Set pointer to RegisterStopTimer */
        void setEt  (PIMet *et)     {this->et = et;}                        /**< Set pointer to ExpiryTimer */
        void setJt  (PIMjt *jt)     {this->jt = jt;}                        /**< Set pointer to JoinTimer */
        void setPpt  (PIMppt *ppt)  {this->ppt = ppt;}                      /**< Set pointer to PrunePendingTimer */

        void setFlags(std::vector<flag> flags)  {this->flags = flags;}      /**< Set vector of flags (flag) */
        bool isFlagSet(flag fl);                                            /**< Returns if flag is set to entry or not*/
        void addFlag(flag fl);                                              /**< Add flag to ineterface */
        void addFlags(flag fl1, flag fl2, flag fl3,flag fl4);               /**< Add flags to ineterface */
        void removeFlag(flag fl);                                           /**< Remove flag from ineterface */

        void setInInt(inet::InterfaceEntry *interfacePtr, int intId, inet::IPv4Address nextHop) {this->inInt.intPtr = interfacePtr; this->inInt.intId = intId; this->inInt.nextHop = nextHop;}    /**< Set information about incoming interface*/
        void setInInt(inInterface inInt) {this->inInt = inInt;}                                                         /**< Set incoming interface*/

        void setOutInt(InterfaceVector outInt) {EV << "MulticastIPRoute: New OutInt" << endl; this->outInt = outInt;}   /**< Set list of outgoing interfaces*/
        void addOutInt(outInterface outInt) {this->outInt.push_back(outInt);}                                           /**< Add interface to list of outgoing interfaces*/
        void addOutIntFull(inet::InterfaceEntry *intPtr, int intId, intState forwading, intState mode,
                            PIMpt *pruneTimer, PIMet *expiryTimer, AssertState assert, RegisterState regState, bool show);

        void setAddresses(inet::IPv4Address multOrigin, inet::IPv4Address multGroup, inet::IPv4Address RP);

        void setRegStatus(int intId, RegisterState regState);                                                           /**< set register status to given interface*/
        RegisterState getRegStatus(int intId);

        bool isRpf(int intId){if (intId == inInt.intId) return true; else return false;}                                /**< Returns if given interface is RPF or not*/
        bool isOilistNull();                                                                                            /**< Returns true if list of outgoing interfaces is empty, otherwise false*/

        inet::IPv4Address   getRP() const {return RP;}                            /**< Get RP IP address */
        PIMgrt*     getGrt() const {return grt;}                            /**< Get pointer to PimGraftTimer */
        PIMsat*     getSat() const {return sat;}                            /**< Get pointer to PimSourceActiveTimer */
        PIMsrt*     getSrt() const {return srt;}                            /**< Get pointer to PimStateRefreshTimer */
        PIMkat*     getKat() const {return kat;}                            /**< Get pointer to KeepAliveTimer */
        PIMrst*     getRst() const {return rst;}                            /**< Get pointer to RegisterStopTimer */
        PIMet*      getEt()  const {return et;}                             /**< Get pointer to ExpiryTimer */
        PIMjt*      getJt()  const {return jt;}                             /**< Get pointer to JoinTimer */
        PIMppt*     getPpt()  const {return ppt;}                           /**< Get pointer to PrunePendingTimer */
        std::vector<flag> getFlags() const {return flags;}                  /**< Get list of route flags */

        // get incoming interface
        inInterface     getInInt() const {return inInt;}                    /**< Get incoming interface*/
        inet::InterfaceEntry* getInIntPtr() const {return inInt.intPtr;}          /**< Get pointer to incoming interface*/
        int             getInIntId() const {return inInt.intId;}            /**< Get ID of incoming interface*/
        inet::IPv4Address       getInIntNextHop() const {return inInt.nextHop;}     /**< Get IP address of next hop for incoming interface*/

        // get outgoing interface
        InterfaceVector getOutInt() const {return outInt;}                  /**< Get list of outgoing interfaces*/
        int             getOutIdByIntId(int intId);                         /**< Get sequence number of outgoing interface with given interface ID*/
        bool            outIntExist(int intId);                             /**< Return true if interface intId exist, otherwise return false*/

        simtime_t getInstallTime() const {return installtime;}
        void setInstallTime(simtime_t time) {installtime = time;}
        void setSequencenumber(int i){sequencenumber =i;}
        unsigned int getSequencenumber() const {return sequencenumber;}
};

#endif /* ANSAIPV4ROUTE_H_ */
