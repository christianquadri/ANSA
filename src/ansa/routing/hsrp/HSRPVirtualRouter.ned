//
// Copyright (C) 2009 - today, Brno University of Technology, Czech Republic
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
// @author Jan Holusa
// @author Vladimir Vesely (ivesely@fit.vutbr.cz)
// @copyright Brno University of Technology (www.fit.vutbr.cz) under GPLv3


package ansa.routing.hsrp;
import inet.applications.contract.IUDPApp;

simple HSRPVirtualRouter like IUDPApp
{
    parameters:
        @display("i=block/routing");

        xml configData = default(xml("<HSRP />"));	///config data given in omnetpp.ini
        string interfaceTableModule;	///interface table
        string arp;				///arp module of this router
        int interface;			///interface on which is HSRP running

        //default version of HSRP is 0
        int version = default(0);	 	 //default version of HSRP is 0
        int hellotime @unit(s) = default(3s);	  	//hellotime of actual device
        int holdtime @unit(s) = default(10s);	  	//holdtime of actual device
        //priority of actual device
        int priority = default(100);	 	 //priority of actual device
        //preemption flag
        bool preempt = default(false);	 //preemption flag
        //group number of actual device 	
        int group = default(0);	    	//group number of actual device 	
        //virtual IP address of actual HSRP device        	
        string virtualIP = default("0.0.0.0");	//virtual IP address of actual HSRP device        	

    gates:
        input udpIn; 	///to HSRP module
        output udpOut;	///from HSRP module
}
