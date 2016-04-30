/*
 * GLBPVirtualRouter.cc
 *
 *  Created on: 18.4. 2016
 *      Author: Jan Holusa
 */
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

#include "GLBPVirtualRouter.h"
#include <iostream>
#include <sstream>
#include <iomanip>

#include "GLBPVirtualForwarder.h"
#include "inet/networklayer/ipv4/IPv4InterfaceData.h"

namespace inet {

Define_Module(GLBPVirtualRouter);

GLBPVirtualRouter::GLBPVirtualRouter() {
}

void GLBPVirtualRouter::initialize(int stage)
{

    cSimpleModule::initialize(stage);

    if (stage == INITSTAGE_ROUTING_PROTOCOLS) {
        hostname = par("deviceId").stdstringValue();
        containingModule = getContainingNode(this);

        //setup HSRP parameters
        glbpState = INIT;
        glbpUdpPort = (int)par("glbpUdpPort");
        glbpMulticast = new L3Address(par("glbpMulticastAddress"));
        glbpGroup = (int)par("vrid");
        virtualIP = new IPv4Address(par("virtualIP").stringValue());
        priority = (int)par("priority");
        preempt = (bool)par("preempt");
//        setVirtualMAC(1);

        WATCH(glbpState);

        printf("PARAMS:\ngroup: %d; prio: %d, preempt: %d\n", glbpGroup, (int)priority, preempt);

        //init Timers
        helloTime = par("hellotime");
        holdTime = par("holdtime");
        redirect = par("redirect");
        timeout = par("timeout");
        hellotimer = new cMessage("helloTimer");
        standbytimer = new cMessage("standbyTimer");
        activetimer = new cMessage("activeTimer");
        initmessage = new cMessage("startHSRP");

        //init VF timers
        activetimerVf.reserve(vfMax);
        for (int i = 0; i < vfMax; i++){
            cMessage * m = new cMessage("activeTimerVF",i);
            activetimerVf.push_back(m);
        }
        redirecttimer.reserve(vfMax);
        for (int i = 0; i < vfMax; i++){
            cMessage *m = new cMessage("redirectTimer",i);
            redirecttimer.push_back(m);
        }
        timeouttimer.reserve(vfMax);
        for (int i = 0; i < vfMax; i++){
            cMessage *m = new cMessage("timeoutTimer",i);
            timeouttimer.push_back(m);
        }

        //get neccessary OMNET parameters
        ift = getModuleFromPar<IInterfaceTable>(par("interfaceTableModule"), this);
        arp = getModuleFromPar<ARP>(par("arp"), this);
        ie = dynamic_cast<AnsaInterfaceEntry *>(ift->getInterfaceById((int) par("interface")));

        //max nuber of vf is 4
        vfVector.reserve(vfMax);

        //add VF's
        for (int i=1; i<=vfMax; i++){
            GLBPVirtualForwarder *glbpVF = new GLBPVirtualForwarder();
            //add VF's to vector
            vfVector.push_back(glbpVF);
        }

        //get socket ready
        socket = new UDPSocket();
        socket->setOutputGate(gate("udpOut"));
        socket->setReuseAddress(true);
        socket->bind(glbpUdpPort);

        //subscribe to notifications
//        containingModule->subscribe(NF_INTERFACE_CREATED, this);
//        containingModule->subscribe(NF_INTERFACE_DELETED, this);
        containingModule->subscribe(NF_INTERFACE_STATE_CHANGED, this);

        //start GLBP
        scheduleAt(simTime() , initmessage);
    }

}

void GLBPVirtualRouter::handleMessage(cMessage *msg)
{
    if (msg->isSelfMessage()){
        if (msg == initmessage) {
            initState();
            return;
        }

        //Is it self message for VF part?
        if (isVfSelfMessage(msg)<(vfMax+1)){
            handleVfSelfMessage(msg);
        }

        switch(glbpState){
            case LISTEN:
                //f,g
                if (msg == activetimer){
                    scheduleTimer(activetimer);
                    scheduleTimer(standbytimer);
                    DebugStateMachine(LISTEN, SPEAK);
                    glbpState = SPEAK;
                    scheduleTimer(hellotimer);
                    sendMessage(HELLOm);
                }else if ( msg == standbytimer ){
                    scheduleTimer(standbytimer);
                    DebugStateMachine(LISTEN, SPEAK);
                    glbpState = SPEAK;
                    scheduleTimer(hellotimer);
                    sendMessage(HELLOm);
                }else if (msg == hellotimer){
                    scheduleTimer(hellotimer);
                    if (isVfActive()){
                        sendMessage(COMBINEDm);
                    }else{
                        sendMessage(HELLOm);
                    }
                }
                break;
            case SPEAK:
                if (msg == standbytimer){//f
                    if (standbytimer->isScheduled())
                        cancelEvent(standbytimer);
                    DebugStateMachine(SPEAK, STANDBY);
                    glbpState = STANDBY;
                    sendMessage(HELLOm);
                    scheduleTimer(hellotimer);
                }else
                if (msg == activetimer){
                    if (activetimer->isScheduled())
                        cancelEvent(activetimer);
                    DebugStateMachine(SPEAK, ACTIVE);
                    glbpState = ACTIVE;
                    startVf(vfCount);
                    vfIncrement();
                    //TODO jestlize neni VF tak se stan VF1 a posli
                    sendMessage(COMBINEDm);
                    scheduleTimer(hellotimer);
                }else
                if (msg == hellotimer){
                    if (isVfActive()){
                        sendMessage(COMBINEDm);
                    }else{
                        sendMessage(HELLOm);
                    }
                    scheduleTimer(hellotimer);
                }
                break;
            case STANDBY:
                if (msg == activetimer){//g
                    if (standbytimer->isScheduled())
                        cancelEvent(standbytimer);
                    if (activetimer->isScheduled())
                        cancelEvent(activetimer);
                    DebugStateMachine(STANDBY, ACTIVE);
                    glbpState = ACTIVE;
//                    vf->setEnable();//TODO
                    sendMessage(HELLOm);//TODO
                    scheduleTimer(hellotimer);
                }
                if (msg == hellotimer){
                    if (isVfActive()){
                        sendMessage(COMBINEDm);
                    }else{
                        sendMessage(HELLOm);
                    }
                    scheduleTimer(hellotimer);
                }
                break;
            case ACTIVE:
                if (msg == hellotimer){
                    if (isVfActive()){
                        sendMessage(COMBINEDm);
                    }else{
                        sendMessage(HELLOm);
                    }
                    scheduleTimer(hellotimer);
                }
                if (msg == standbytimer){
                    DebugUnknown(STANDBY);
                }
                break;
        }
    }
    else
    {
        GLBPMessage *GLBPm = dynamic_cast<GLBPMessage *>(msg);
        DebugGetMessage(GLBPm);

        //process VF messge part
        if (dynamic_cast<GLBPRequestResponse *>(GLBPm->findOptionByType(REQRESP,1))){
            handleMessageRequestResponse(GLBPm);
        }

        //VG Process ---> works with Hello part
        //and VF starts
        switch(glbpState){
            case LISTEN:
                handleMessageListen(GLBPm);
                break;
            case SPEAK:
                handleMessageSpeak(GLBPm);
                break;
            case STANDBY:
                handleMessageStandby(GLBPm);
                break;
            case ACTIVE:
                handleMessageActive(GLBPm);
                break;
        }//end switch
        delete GLBPm;
    }//end if self msg
}

/**
 * Msg handlers
 */

void GLBPVirtualRouter::handleMessageRequestResponse(GLBPMessage *msg){
    GLBPRequestResponse *req;
    for (int i=1; i< (int)msg->getOptionArraySize(); i++){
        req = dynamic_cast<GLBPRequestResponse *>(msg->findOptionByType(REQRESP,i));

//        if (req->getForwarder() > vfCount){
//            vfCount = req->getForwarder();
//        }
        if (req->getVfState() == ACTIVE){
            switch (vfVector[req->getForwarder()-1]->getState()){
                case LISTEN:
                    //receive from active high prio
                    if (!isHigherVfPriorityThan(msg, req->getForwarder(), i) ){
                        //VF active timer update
                        scheduleTimer(activetimerVf[req->getForwarder()-1]);
                    }else{
                        //receive from active lower
                        if (activetimerVf[req->getForwarder()-1]->isScheduled())
                            cancelEvent(activetimerVf[req->getForwarder()-1]);
                        vfVector[req->getForwarder()-1]->setState(ACTIVE);
                        EV<<hostname<<" # "<<req->getForwarder()<<" Grp "<<glbpGroup<<" LISTEN"<<" -> "<<"ACTIVE"<<endl;
                        std::cout<<hostname<<" # "<<req->getForwarder()<<" Grp "<<glbpGroup<<" LISTEN"<<" -> "<<"ACTIVE"<<endl;
                                    fflush(stdout);
                        vfVector[req->getForwarder()-1]->setEnable();
                        sendMessage(RESPONSEm, req->getForwarder());
                    }
                    break;
                case ACTIVE:
                    //receive from higher active
                    if (!isHigherVfPriorityThan(msg, req->getForwarder(), i) ){
                        scheduleTimer(activetimerVf[req->getForwarder()-1]);
                        vfVector[req->getForwarder()-1]->setState(LISTEN);
                        vfVector[req->getForwarder()-1]->setDisable();
                        EV<<hostname<<" # "<<req->getForwarder()<<" Grp "<<glbpGroup<<" ACTIVE"<<" -> "<<"LISTEN"<<endl;
                        std::cout<<hostname<<" # "<<req->getForwarder()<<" Grp "<<glbpGroup<<" ACTIVE"<<" -> "<<"LISTEN"<<endl;
                        fflush(stdout);
                        vfVector[req->getForwarder()-1]->setDisable();
                    }
                    break;
            }//end switch
        }
    }//end for
}

void GLBPVirtualRouter::handleMessageListen(GLBPMessage *msg){
    //TODO resign receive
    if (dynamic_cast<GLBPHello *>(msg->findOptionByType(HELLO,0))){
        GLBPHello *hello = check_and_cast<GLBPHello *>(msg->findOptionByType(HELLO,0));
        UDPDataIndication *udpControll = check_and_cast<UDPDataIndication *>(msg->getControlInfo());
        IPv4Address IP = udpControll->getSrcAddr().toIPv4();

        switch(hello->getVgState()){
        case SPEAK:
            if (!isHigherPriorityThan(msg)){
                scheduleTimer(standbytimer);//FIXME TODO mozna tady nema byt
    //        scheduleTimer(activetimer);//FIXME TODO mozna tady nema byt
            }
            break;
        case STANDBY://j
            //lower
            if (isHigherPriorityThan(msg)){
                DebugStateMachine(LISTEN,SPEAK);
                glbpState = SPEAK;
                scheduleTimer(standbytimer);
                sendMessage(HELLOm);
                scheduleTimer(hellotimer);
            }
            break;
        case ACTIVE:
            //lower
            if (isHigherPriorityThan(msg)){//l
                if (preempt){
                    DebugStateMachine(LISTEN, SPEAK);
                    glbpState = SPEAK;
                    scheduleTimer(activetimer);
                    scheduleTimer(standbytimer);
                    sendMessage(HELLOm);
                    scheduleTimer(hellotimer);
                } else{//TODO KDYZ NENI PREEMPT TAK PREPLANUJ TIMER"":::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::
                    //TODO JEstli nemam  VF tak pozadam
                    if (!isVfActive()){
                        sendMessageRequestResponse(REQUESTm, &IP);
                    }
                    scheduleTimer(activetimer);
                }
            }
            else
            {//higher - k
                //TODO Learn params
                //tODO pouze kdyz nemam VF tak pozadam
                if (!isVfActive()){
                    sendMessageRequestResponse(REQUESTm, &IP);
                }
                scheduleTimer(activetimer);
            }
            break;
        }
    }else{
        addOrStartVf(msg);
    }
}

void GLBPVirtualRouter::handleMessageSpeak(GLBPMessage *msg){
    //TODO RESIGN

    if (dynamic_cast<GLBPHello *>(msg->findOptionByType(HELLO,0))){
        GLBPHello *hello = check_and_cast<GLBPHello *>(msg->findOptionByType(HELLO,0));

        UDPDataIndication *udpControll = check_and_cast<UDPDataIndication *>(msg->getControlInfo());
        IPv4Address IP = udpControll->getSrcAddr().toIPv4();

        switch(hello->getVgState()){//TODO msg->getVgState()){
            case SPEAK:
                //h
                if (!isHigherPriorityThan(msg)){
                    scheduleTimer(standbytimer);//TODO mozna i activetimer
                    scheduleTimer(hellotimer);
                    DebugStateMachine(SPEAK, LISTEN);
                    glbpState = LISTEN;

                }
                break;
            case STANDBY:
                //i
                if (!isHigherPriorityThan(msg)){
                    scheduleTimer(standbytimer);
                    scheduleTimer(hellotimer);
                    DebugStateMachine(SPEAK, LISTEN);
                    glbpState = LISTEN;
                }
                else
                {//j
                    if (standbytimer->isScheduled())
                        cancelEvent(standbytimer);
                    DebugStateMachine(SPEAK, STANDBY);
                    glbpState = STANDBY;
                    sendMessage(HELLOm);
                    scheduleTimer(hellotimer);
                }
                break;
            case ACTIVE:
                if (isHigherPriorityThan(msg)){
                    if (preempt){ //l
                        if (activetimer->isScheduled())
                            cancelEvent(activetimer);
                        if (standbytimer->isScheduled())
                            cancelEvent(standbytimer);
                        DebugStateMachine(SPEAK, ACTIVE);
                        glbpState = ACTIVE;
                        sendMessage(HELLOm);
                        scheduleTimer(hellotimer);
                    }else{
                        //TODO jestli nemam VF
                        if (!isVfActive()){
                            sendMessageRequestResponse(REQUESTm, &IP);
                        }
                        scheduleTimer(activetimer);
                    }
                }
                else
                {//k
                    //TODO LEARN PARAMS
                    //TODO jestli nemam VF
                    if (!isVfActive()){
                        sendMessageRequestResponse(REQUESTm, &IP);
                    }
                    scheduleTimer(activetimer);
                }
                break;
        }//switch
    }
    else{
        addOrStartVf(msg);
    }
}

void GLBPVirtualRouter::handleMessageStandby(GLBPMessage *msg){
    //TODO RESIGN RECEIVE
    if (dynamic_cast<GLBPHello *>(msg->findOptionByType(HELLO,0))){
        GLBPHello *hello = check_and_cast<GLBPHello *>(msg->findOptionByType(HELLO,0));

        UDPDataIndication *udpControll = check_and_cast<UDPDataIndication *>(msg->getControlInfo());
        IPv4Address IP = udpControll->getSrcAddr().toIPv4();

        switch(hello->getVgState()){// TODOmsg->getVgState()){
            case SPEAK:
                if (!isHigherPriorityThan(msg)){
                    scheduleTimer(standbytimer);
                    scheduleTimer(hellotimer);
                    DebugStateMachine(STANDBY, LISTEN);
                    glbpState = LISTEN;
                }
                break;
            case ACTIVE:
                //l
                if (!isHigherPriorityThan(msg)){
                    //TODO Learn params
                    if (!isVfActive()){
                        sendMessageRequestResponse(REQUESTm, &IP);
                    }
                    scheduleTimer(activetimer);
                }
                else
                {//k
                    if (preempt){
                        if (activetimer->isScheduled())
                            cancelEvent(activetimer);
                        if (standbytimer->isScheduled())
                            cancelEvent(standbytimer);
                        DebugStateMachine(STANDBY, ACTIVE);
                        glbpState = ACTIVE;
    //                    vf->setEnable();//TODO
                        sendMessage(HELLOm);
                        scheduleTimer(hellotimer);
                    }else{
                        if (!isVfActive()){
                            sendMessageRequestResponse(REQUESTm, &IP);
                        }
                        scheduleTimer(activetimer);
                    }
                }
                break;
        }//end switch
    }else{
        addOrStartVf(msg);
    }
}

void GLBPVirtualRouter::handleMessageActive(GLBPMessage *msg)
{
    //Got Hello or ReqResp?
    if (dynamic_cast<GLBPHello *>(msg->findOptionByType(HELLO,0))){
        GLBPHello *hello = check_and_cast<GLBPHello *>(msg->findOptionByType(HELLO,0));

        switch(hello->getVgState()){//msg->getVgState()){
            case STANDBY:
                scheduleTimer(standbytimer);
                break;
            case ACTIVE:
                if (!isHigherPriorityThan(msg)){//k
                    scheduleTimer(activetimer);
                    scheduleTimer(standbytimer);
                    DebugStateMachine(ACTIVE, SPEAK);
    //                vf->setDisable();//TODO
                    glbpState = SPEAK;
                    sendMessage(HELLOm);
                    scheduleTimer(hellotimer);
                }
                break;
        }
    }else{//ReqResp
        GLBPRequestResponse *req = check_and_cast<GLBPRequestResponse *>(msg->findOptionByType(REQRESP,0));

        //its request
        if (req->getForwarder() == UNKNOWN){
            //get IP requester IP
            UDPDataIndication *udpControll = check_and_cast<UDPDataIndication *>(msg->getControlInfo());
            IPv4Address IP = udpControll->getSrcAddr().toIPv4();

            //send Response
            sendMessageRequestResponse(RESPONSEm, &IP);
        }
        else
        {//its advertisment
            addVf(req->getForwarder(), &(msg->getOwnerId()));
        }

    }
}

void GLBPVirtualRouter::addOrStartVf(GLBPMessage *msg){
    GLBPRequestResponse *glbpM = check_and_cast<GLBPRequestResponse *>(msg->findOptionByType(REQRESP,0));

    if (glbpM->getForwarder() > vfCount)
        vfCount = glbpM->getForwarder();

    if (glbpM->getVfState() == UNKNOWN){
        //LEARN VF params and start Vf as a primary
        if (glbpM->getForwarder() <= vfMax){
            startVf(glbpM->getForwarder());
        }
    }else if(vfVector[glbpM->getForwarder()-1]->getState() == DISABLED){
        //add VF as a secondary
        addVf(glbpM->getForwarder(), &(msg->getOwnerId()));
    }
}

void GLBPVirtualRouter::receiveSignal(cComponent *source, simsignal_t signalID, cObject *obj DETAILS_ARG)
{
    Enter_Method_Silent("GLBPVirtualRouter::receiveChangeNotification(%s)", notificationCategoryName(signalID));

     const AnsaInterfaceEntry *ief;
     const InterfaceEntryChangeDetails *change;

     if (signalID == NF_INTERFACE_STATE_CHANGED) {

         change = check_and_cast<const InterfaceEntryChangeDetails *>(obj);
         ief = check_and_cast<const AnsaInterfaceEntry *>(change->getInterfaceEntry());

         //Is it my interface?
         if (ief->getInterfaceId() == ie->getInterfaceId()){

             //is it change to UP or to DOWN?
             if (ie->isUp()){
                 //do Enable VF
                 EV<<hostname<<" # "<<ie->getName()<<" Interface up."<<endl;
                 interfaceUp();
             }else{
                 //do disable VF
                 EV<<hostname<<" # "<<ie->getName()<<" Interface down."<<endl;
                 interfaceDown();
             }

         }
     }
     else
         throw cRuntimeError("Unexpected signal: %s", getSignalName(signalID));
 }

void GLBPVirtualRouter::initState(){
//    vf->setDisable();//TODO
    // Check virtualIP
    if (strcmp(virtualIP->str(false).c_str(), "0.0.0.0") != 0)
    { //virtual ip is already set
        DebugStateMachine(INIT, LISTEN);
        listenState();
    }
    else
    { //virtual ip is not set
        DebugStateMachine(INIT, DISABLED);
        glbpState = DISABLED;
//        disabledState();
    }
}

void GLBPVirtualRouter::listenState(){
    glbpState = LISTEN;

//    if ()
    scheduleTimer(hellotimer);
    scheduleTimer(activetimer);
    scheduleTimer(standbytimer);
}

/**
 * VF State machine
 */
void GLBPVirtualRouter::startVf(int n)
{
    //TODO check if vfCount==vfMax neni!!

    //setup parameters
    vfVector[n-1]->addIPAddress(*virtualIP);
    vfVector[n-1]->setMACAddress(*setVirtualMAC(n));
    vfVector[n-1]->setPriority(167);
    vfVector[n-1]->setState(LISTEN);
    vfVector[n-1]->setWeight(weight);
    vfVector[n-1]->setForwarder(n);
    MACAddress *myMac = new MACAddress(ie->getMacAddress());
    vfVector[n-1]->setPrimary(myMac);

    //add vf to interface
    ie->addVirtualForwarder(vfVector[n-1]);

    //start timer
    scheduleTimer(activetimerVf[n-1]);

    std::cout<<hostname<<" # "<<n<<" Grp "<<glbpGroup<<"STARTUJEM -> "<<"LISTEN"<<endl;
    fflush(stdout);
}

void GLBPVirtualRouter::addVf(int n, MACAddress *macOfPrimary)
{
    //If it is not already added
    if (vfVector[n-1]->getForwarder() == 0){
        //setup parameters
        vfVector[n-1]->addIPAddress(*virtualIP);
        vfVector[n-1]->setMACAddress(*setVirtualMAC(n));
        vfVector[n-1]->setPriority(135);
        vfVector[n-1]->setState(LISTEN);
        vfVector[n-1]->setWeight(weight);
        vfVector[n-1]->setForwarder(n);
        vfVector[n-1]->setPrimary(macOfPrimary);

        //add vf to interface
        ie->addVirtualForwarder(vfVector[n-1]);

        //start timer
        scheduleTimer(activetimerVf[n-1]);

        std::cout<<hostname<<" # "<<n<<" Grp "<<glbpGroup<<" INIT"<<" -> "<<"LISTEN"<<endl;
    fflush(stdout);
    }
}

void GLBPVirtualRouter::sendMessage(GLBPMessageType type, int forwarder){
    //Toto je zprava oznamujici ze jsem active

    //TODO zprava odpovedi na VF request
    DebugSendMessage(type);
    GLBPMessage *packet = new GLBPMessage();

    packet->setName("GLBPRequest/Response");
    packet->setByteLength(GLBP_HEADER_BYTES + GLBP_HELLO_BYTES);

    //set glbp HELLO TLV
    GLBPRequestResponse *helloPacket = generateReqRespTLV(forwarder);
    packet->addOption(helloPacket);

    //set glbp packet header
    packet->setGroup(glbpGroup);
    packet->setOwnerId(ie->getMacAddress());

    UDPSocket::SendOptions options;
    options.outInterfaceId = ie->getInterfaceId();
    options.srcAddr = ie->ipv4Data()->getIPAddress();

    socket->setTimeToLive(1);
    socket->sendTo(packet, *glbpMulticast, glbpUdpPort, &options);
}

void GLBPVirtualRouter::sendMessageRequestResponse(GLBPMessageType type, IPv4Address *IP)
{
    DebugSendMessage(type);
    GLBPMessage *packet = new GLBPMessage();
    packet->setName("GLBPRequest/Response");

    GLBPRequestResponse *reqrespPacket = generateReqRespTLV(0);

    switch (type){
        case REQUESTm:
            packet->addOption(reqrespPacket);
            packet->setByteLength(GLBP_HEADER_BYTES + GLBP_REQRESP_BYTES);

            packet->setName("GLBPRequest");
            break;
        case RESPONSEm:
            reqrespPacket->setForwarder(vfCount);
            reqrespPacket->setMacAddress(*setVirtualMAC(vfCount));

            packet->setName("GLBPResponse");
            packet->addOption(reqrespPacket);
            packet->setByteLength(GLBP_HEADER_BYTES + GLBP_REQRESP_BYTES);
            //TODO start dalsi VF? Do backup state...?
            vfIncrement();
            break;
    }
    //set glbp packet header
    packet->setGroup(glbpGroup);
    packet->setOwnerId(ie->getMacAddress());

    UDPSocket::SendOptions options;
    options.outInterfaceId = ie->getInterfaceId();
    options.srcAddr = ie->ipv4Data()->getIPAddress();

    socket->setTimeToLive(1);
    socket->sendTo(packet, *IP, glbpUdpPort, &options);
}

void GLBPVirtualRouter::vfIncrement(){
    if (vfCount <= vfMax)
        vfCount++;
}

void GLBPVirtualRouter::sendMessage(GLBPMessageType type)
{
    DebugSendMessage(type);
    GLBPMessage *packet = new GLBPMessage();
    switch (type){//TODO
        case HELLOm:
        {
            packet->setName("GLBPHello");
            packet->setByteLength(GLBP_HEADER_BYTES + GLBP_REQRESP_BYTES);

            //set glbp HELLO TLV
            GLBPHello *helloPacket = generateHelloTLV();
            packet->addOption(helloPacket);
            break;
        }
        case COMBINEDm:
        {
            packet->setName("GLBPHello,Req/Resp");
            //set glbp HELLO TLV
            GLBPHello *helloPacket = generateHelloTLV();
            packet->addOption(helloPacket);

            //add active || listen primary forwarders to GLBP Hello, Req/resp
            for (int i = 0; i < (int) vfVector.size(); i++){
                if ((vfVector[i]->getPriority() == 167) || (vfVector[i]->getState() == ACTIVE)){
                    //set glbp REQ/RESP TLV TODO
                    GLBPRequestResponse *reqrespPacket = generateReqRespTLV(i+1);
                    packet->addOption(reqrespPacket);
                }
            }
            packet->setByteLength(GLBP_HEADER_BYTES + GLBP_HELLO_BYTES + GLBP_REQRESP_BYTES*(packet->getOptionArraySize()-1));
            break;
        }
        case COUPm:
            break;
    }


    //set glbp packet header
    packet->setGroup(glbpGroup);
    packet->setOwnerId(ie->getMacAddress());

    UDPSocket::SendOptions options;
    options.outInterfaceId = ie->getInterfaceId();
    options.srcAddr = ie->ipv4Data()->getIPAddress();

    socket->setTimeToLive(1);
    socket->sendTo(packet, *glbpMulticast, glbpUdpPort, &options);
}

//GLBPMessage *GLBPVirtualRouter::generateMessage()
//{ //todo udelat obecne ne jen pro hello
//    GLBPMessage *msg = new GLBPMessage("GLBPMessage");
//
//    //set glbp packet header
//    msg->setGroup(glbpGroup);
//    msg->setOwnerId(ie->getMacAddress());

//    //set glbp HELLO TLV
//    GLBPHello *helloPacket = generateHelloTLV();
//    msg->addOption(helloPacket);

//    GLBPRequestResponse *reqrespPacket = generateReqRespTLV(1);
//    msg->addOption(reqrespPacket);

//    return msg;
//}

GLBPHello *GLBPVirtualRouter::generateHelloTLV(){
    GLBPHello *packet = new GLBPHello();

    //setup Hello TLV
    packet->setVgState(glbpState);
    packet->setPriority(priority);
    packet->setHelloint(helloTime);
    packet->setHoldint(holdTime);
    packet->setRedirect(redirect);
    packet->setTimeout(timeout);
    //TODO: IPv6
    packet->setAddressType(IPv4);
    packet->setAddress(*virtualIP);

    return packet;
}

GLBPRequestResponse *GLBPVirtualRouter::generateReqRespTLV(int forwarder)
{
    //TODO Pozor! Na 1 IF muze byt forwarder (napr cislo 1) z vice skupin
    GLBPRequestResponse *packet = new GLBPRequestResponse();

    if (forwarder > 0){
        //setup RequestResponse TLV
        packet->setForwarder(forwarder);
        packet->setVfState(vfVector[forwarder-1]->getState());
        packet->setPriority(vfVector[forwarder-1]->getPriority());
        packet->setWeight(vfVector[forwarder-1]->getWeight());
        packet->setMacAddress(vfVector[forwarder-1]->getMacAddress());
    }else{
        //VF request
        MACAddress *unknownMAC = new MACAddress("00-00-00-00-00-00");

        packet->setForwarder(forwarder);
        packet->setVfState(0);
        packet->setPriority(0);
        packet->setWeight(0);
        packet->setMacAddress(*unknownMAC);
    }
    return packet;
}

void GLBPVirtualRouter::scheduleTimer(cMessage *msg)
{
    float jitter = 0;
    if (par("jittered").boolValue()){
        jitter = (rand() % 100)*0.01; //TODO rand is not optimal
    }

    if (msg->isScheduled()){
        cancelEvent(msg);
    }
    if (msg == activetimer)
        scheduleAt(simTime() + holdTime+jitter, activetimer);
    if (msg == standbytimer)
        scheduleAt(simTime() + holdTime+jitter, standbytimer);
    if (msg == hellotimer)
        scheduleAt(simTime() + helloTime+jitter, hellotimer);

    for (int i = 0; i < (int) activetimerVf.size(); i++){
        if (msg == activetimerVf[i])
            scheduleAt(simTime() + holdTime+jitter, activetimerVf[i]);
    }
}

MACAddress *GLBPVirtualRouter::setVirtualMAC(int n)
{
    MACAddress * mac;
    //"00-07-b4-xx-xx-yy"
    //x's are 6bits of 0 followed by 10bits of group ID
    //y's reprezent number of virtual forwarder

    //set first two bits of group ID
    int minus = 0;
    if (glbpGroup < 256){
        mac = new MACAddress("00-07-b4-00-00-00");
    }
    else if ( glbpGroup < 512 ){ // glbpGroup >= 256 &&
        mac = new MACAddress("00-07-b4-01-00-00");
        minus = 256;
    }else if ( glbpGroup < 768 ){ // glbpGroup >= 512 &&
        mac = new MACAddress("00-07-b4-02-00-00");
        minus = 512;
    }else{ //horni bity oba 11
        mac = new MACAddress("00-07-b4-03-00-00");
        minus = 768;
    }

    //set another 8 bits of gid
    mac->setAddressByte(4, glbpGroup-minus);

    //set virtual forwarder number
    mac->setAddressByte(5, n);
//    EV_DEBUG<<"routerID:"<<par("deviceId").str()<<"vMAC:"<<virtualMAC->str()<<"\n";
    return mac;
}

bool GLBPVirtualRouter::isHigherPriorityThan(GLBPMessage *GLBPm){
    UDPDataIndication *udpInfo = check_and_cast<UDPDataIndication *>(GLBPm->getControlInfo());
    GLBPHello *hello = check_and_cast<GLBPHello *>(GLBPm->findOptionByType(HELLO,0));

    if (hello->getPriority() < priority){//TODO
        return true;
    }else if (hello->getPriority() > priority){//TODO
        return false;
    }else{// ==
//        std::cout<<"ie IP:"<<ie->ipv4Data()->getIPAddress().str(false)<<"recv mess IP:"<<ci->getSrcAddr().str()<<std::endl;
//        fflush(stdout);
        if (ie->ipv4Data()->getIPAddress() > udpInfo->getSrcAddr().toIPv4() ){
            return true;
        }else{
            return false;
        }
    }
}

bool GLBPVirtualRouter::isHigherVfPriorityThan(GLBPMessage *GLBPm, int forwarder, int pos){
    UDPDataIndication *udpInfo = check_and_cast<UDPDataIndication *>(GLBPm->getControlInfo());
    GLBPRequestResponse *req = check_and_cast<GLBPRequestResponse *>(GLBPm->findOptionByType(REQRESP,pos));

    if (req->getPriority() < vfVector[forwarder-1]->getPriority()){//TODO
        return true;
    }else if (req->getPriority() > vfVector[forwarder-1]->getPriority()){//TODO
        return false;
    }else{// ==
        if (ie->ipv4Data()->getIPAddress() > udpInfo->getSrcAddr().toIPv4() ){
            return true;
        }else{
            return false;
        }
    }
}

/**
 * VF Functions
 */

void GLBPVirtualRouter::handleVfSelfMessage(cMessage *msg){
    int forwarder = isVfSelfMessage(msg);

    if (msg == activetimerVf[forwarder-1]){
        if (vfVector[forwarder-1]->getState() == LISTEN){
            if (activetimerVf[forwarder-1]->isScheduled())
                cancelEvent(activetimerVf[forwarder-1]);
            vfVector[forwarder-1]->setState(ACTIVE);
            EV<<hostname<<" # "<<forwarder<<" Grp "<<glbpGroup<<" LISTEN"<<" -> "<<"ACTIVE"<<endl;
            std::cout<<hostname<<" # "<<forwarder<<" Grp "<<glbpGroup<<" LISTEN"<<" -> "<<"ACTIVE"<<endl;
            fflush(stdout);
            sendMessage(RESPONSEm, forwarder);
            vfVector[forwarder-1]->setEnable();
            scheduleTimer(redirecttimer[forwarder-1]);
            scheduleTimer(timeouttimer[forwarder-1]);
        }
    }else if(msg == redirecttimer[forwarder-1]){
        //TODO
    }else{//==>timeouttimer
        //TODO
    }
}

int GLBPVirtualRouter::isVfSelfMessage(cMessage *msg){
    for (int i = 0; i < (int) activetimerVf.size(); i++){
        if (msg == activetimerVf[i])
            return i+1;
    }
    for (int i = 0; i < (int) redirecttimer.size(); i++){
            if (msg == redirecttimer[i])
                return i+1;
    }
    for (int i = 0; i < (int) timeouttimer.size(); i++){
            if (msg == timeouttimer[i])
                return i+1;
    }
    return vfMax+1;
}

bool GLBPVirtualRouter::isVfActive(){
    for (int i=0; i<vfMax; i++){
        if (vfVector[i]->getPrimary() != nullptr){
            if (vfVector[i]->getPriority() == 167){
                return true;
            }
        }
    }
    return false;
}

/**
 * Debug Messages
 */

void GLBPVirtualRouter::DebugStateMachine(int from, int to){
    std::string fromText, toText;
    fromText = intToGlbpState(from);
    toText = intToGlbpState(to);
    EV<<hostname<<" # "<<ie->getName()<<" Grp "<<glbpGroup<<" "<<fromText<<" -> "<<toText<<endl;
}

std::string GLBPVirtualRouter::intToGlbpState(int state){
    std::string retval;
    switch(state){
        case INIT:
            retval = "Init"; break;
        case LISTEN:
            retval = "Listen"; break;
        case SPEAK:
            retval = "Speak"; break;
        case STANDBY:
            retval = "Standby"; break;
        case ACTIVE:
            retval = "Active"; break;
        case DISABLED:
            retval = "Disabled"; break;
    }
    return retval;
}

void GLBPVirtualRouter::DebugSendMessage(GLBPMessageType t){
    std::string type = intToMessageType(t);
    std::string state = intToGlbpState(glbpState);

    EV<<hostname<<" # "<<ie->getName()<<" Grp "<<glbpGroup<<" "<<type<<" out "<< ie->ipv4Data()->getIPAddress().str(false) <<" "<<state << " pri "<<priority<<" vIP "<<virtualIP->str(false)<<endl;
}

void GLBPVirtualRouter::DebugGetMessage(GLBPMessage *msg){
    UDPDataIndication *udpInfo = check_and_cast<UDPDataIndication *>(msg->getControlInfo());
    std::string ipFrom =  udpInfo->getSrcAddr().toIPv4().str(false);

    for (int i=0; i < (int)msg->getOptionArraySize(); i++){
        if (dynamic_cast<GLBPHello *>(msg->findOptionByType(HELLO, i))){
            GLBPHello *glbpM = check_and_cast<GLBPHello *>(msg->findOptionByType(HELLO, i));
            std::string type = intToMessageType(HELLOm);
            std::string state = intToGlbpState(glbpM->getVgState());
            EV<<hostname<<" # "<<ie->getName()<<" Grp "<<(int)msg->getGroup()<<" "<<type<<" in "<< ipFrom <<" "<<state << " pri "<<(int)glbpM->getPriority()<<" vIP "<<glbpM->getAddress().str(false)<<endl;
        }else{
            GLBPRequestResponse *glbpM = check_and_cast<GLBPRequestResponse *>(msg->findOptionByType(REQRESP, i));
            std::string type = intToMessageType(REQUESTm);//tODO
            std::string state = intToGlbpState(glbpM->getVfState());
            EV<<hostname<<" # "<<ie->getName()<<" Grp "<<(int)msg->getGroup()<<" "<<type<<" in "<< ipFrom <<" "<<state << " pri "<<(int)glbpM->getPriority()<<" vMAC "<<glbpM->getMacAddress().str()<<endl;
        }
    }
}

void GLBPVirtualRouter::DebugUnknown(int who){
    std::string type = intToGlbpState(who);
    EV_DEBUG<<hostname<<" # "<<ie->getName()<<" "<<glbpGroup<<" "<<type<<" router is unknown"<<endl;//TODO was, <ip stareho>
}

std::string GLBPVirtualRouter::intToMessageType(GLBPMessageType msg){
    std::string retval;
    switch(msg){
        case HELLOm:
            retval = "Hello"; break;
        case RESPONSEm:
        case REQUESTm:
            retval = "Request/Response"; break;
        case COMBINEDm:
            retval = "Hello, Req/Resp"; break;
        case COUPm:
            retval = "Coup"; break;
    }
    return retval;
}

void GLBPVirtualRouter::interfaceDown(){
    switch(glbpState){
        case ACTIVE:
            //TODO SEND RESIGN
        default:
            if (hellotimer->isScheduled())
                cancelEvent(standbytimer);
            if (standbytimer->isScheduled())
                cancelEvent(standbytimer);
            if (hellotimer->isScheduled())
                cancelEvent(standbytimer);
            break;
    }//switch

    DebugStateMachine(glbpState, INIT);
    glbpState = INIT;
//    vf->setDisable();//tODO

    stopVf();
}

void GLBPVirtualRouter::stopVf(){
    for (int i = 0; i < vfMax; i++){
        if (activetimerVf[i]->isScheduled())
            cancelEvent(activetimerVf[i]);
        if (redirecttimer[i]->isScheduled())
            cancelEvent(redirecttimer[i]);
        if (timeouttimer[i]->isScheduled())
            cancelEvent(timeouttimer[i]);
    }

    for (int i=0; i<vfCount; i++){
        vfVector[i]->setState(INIT);
        if (!vfVector[i]->isDisable())
            vfVector[i]->setDisable();
        EV<<hostname<<" # "<<i+1<<" Grp "<<glbpGroup<<" NECO"<<" -> "<<"INIT"<<endl;
        std::cout<<hostname<<" # "<<i+1<<" Grp "<<glbpGroup<<" NECO"<<" -> "<<"INIT"<<endl;
        fflush(stdout);
    }
}

void GLBPVirtualRouter::interfaceUp(){
    initState();
    //TODO prepsat do funkce a osetrit ze nahodou dalsi vf nebyli zatraceni (coz by mohl obstarat teoreticky nejaky timer)
    for (int i = 0; i < vfCount; i++){
        //jestli existoval
        if(vfVector[i]->getForwarder() != 0){
            //tak ho obnov TODO:mozna rovnu do ACTIVE ty s prioritou 167
            vfVector[i]->setState(LISTEN);
            scheduleTimer(activetimerVf[i]);
        }
    }
}


GLBPVirtualRouter::~GLBPVirtualRouter() {
    cancelAndDelete(hellotimer);
    cancelAndDelete(standbytimer);
    cancelAndDelete(activetimer);
    cancelAndDelete(initmessage);
    for (int i = 0; i < vfMax; i++){
        cancelAndDelete(activetimerVf[i]);
        cancelAndDelete(redirecttimer[i]);
        cancelAndDelete(timeouttimer[i]);
    }
    containingModule->unsubscribe(NF_INTERFACE_STATE_CHANGED, this);
}

} /* namespace inet */
