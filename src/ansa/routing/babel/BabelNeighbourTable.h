// The MIT License (MIT)
//
// Copyright (c) 2016 Brno University of Technology
//
//@author Vladimir Vesely (iveselyATfitDOTvutbrDOTcz)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
/**
* @file BabelNeighbourTable.h
* @author Vit Rek (mailto:xrekvi00@stud.fit.vutbr.cz)
* @brief Babel Neighbour Table header file
* @detail Represents data structure of Babel routing neighbour
*/

#ifndef __ANSA_BABELNEIGHBOURTABLE_H_
#define __ANSA_BABELNEIGHBOURTABLE_H_

#include "inet/common/INETDefs.h"

#include "ansa/routing/babel/BabelDef.h"
#include "ansa/routing/babel/BabelInterfaceTable.h"

namespace inet {
class INET_API BabelNeighbour : public cObject
{
  protected:
    BabelInterface *interface;
    L3Address address;
    uint16_t history;
    uint16_t cost;
    uint16_t txcost;
    uint16_t expHSeqno;
    uint16_t neighHelloInterval;
    uint16_t neighIhuInterval;
    Babel::BabelTimer *neighHelloTimer;
    Babel::BabelTimer *neighIhuTimer;


  public:
    BabelNeighbour(BabelInterface *iface, const L3Address& addr,
            Babel::BabelTimer *nht, Babel::BabelTimer *nit):
        interface(iface),
        address(addr),
        history(0),
        cost(0xFFFF),
        txcost(0xFFFF),
        expHSeqno(0),
        neighHelloInterval(0),
        neighIhuInterval(0),
        neighHelloTimer(nht),
        neighIhuTimer(nit)
        {
            ASSERT(iface != NULL);
            ASSERT(nht != NULL);
            ASSERT(nit != NULL);

            neighHelloTimer->setContextPointer(this);
            neighIhuTimer->setContextPointer(this);
        }

    BabelNeighbour():
        interface(NULL),
        history(0),
        cost(0xFFFF),
        txcost(0xFFFF),
        expHSeqno(0),
        neighHelloInterval(0),
        neighIhuInterval(0),
        neighHelloTimer(NULL),
        neighIhuTimer(NULL)
        {}
    virtual ~BabelNeighbour();
    virtual std::string str() const;
    virtual std::string detailedInfo() const {return str();}
    friend std::ostream& operator<<(std::ostream& os, const BabelNeighbour& bn);

    BabelInterface *getInterface() const {return interface;}
    void setInterface(BabelInterface * iface) {interface = iface;}

    const L3Address& getAddress() const {return address;}
    void setAddress(const L3Address& addr) {address = addr;}

    uint16_t getHistory() const {return history;}
    void setHistory(uint16_t h) {history = h;}

    uint16_t getCost() const {return cost;}
    void setCost(uint16_t c) {cost = c;}

    uint16_t getTxcost() const {return txcost;}
    void setTxcost(uint16_t t) {txcost = t;}

    uint16_t getExpHSeqno() const {return expHSeqno;}
    void setExpHSeqno(uint16_t ehsn) {expHSeqno = ehsn;}

    uint16_t getNeighHelloInterval() const {return neighHelloInterval;}
    void setNeighHelloInterval(uint16_t nhi) {neighHelloInterval = nhi;}

    uint16_t getNeighIhuInterval() const {return neighIhuInterval;}
    void setNeighIhuInterval(uint16_t nii) {neighIhuInterval = nii;}

    void noteReceive() {history = (history >> 1) | 0x8000;}
    void noteLoss() {history = history >> 1;}

    bool recomputeCost();
    uint16_t computeRxcost() const;

    void resetNHTimer();
    void resetNITimer();
    void resetNHTimer(double delay);
    void resetNITimer(double delay);
    void deleteNHTimer();
    void deleteNITimer();
};




class BabelNeighbourTable
{
  protected:
    std::vector<BabelNeighbour *> neighbours;

  public:
    BabelNeighbourTable()
        {}
    virtual ~BabelNeighbourTable();

    std::vector<BabelNeighbour *>& getNeighbours() {return neighbours;}
    int getNumOfNeighOnIface(BabelInterface *iface);

    BabelNeighbour *findNeighbour(BabelInterface *iface, const L3Address& addr);
    BabelNeighbour *addNeighbour(BabelNeighbour *neigh);
    void removeNeighbour(BabelNeighbour *neigh);
    void removeNeighboursOnIface(BabelInterface *iface);
};
}
#endif
