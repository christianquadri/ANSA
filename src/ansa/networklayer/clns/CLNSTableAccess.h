// Copyright (C) 2013 Brno University of Technology (http://nes.fit.vutbr.cz/ansa)
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
/**
 * Copyright (C) 2012 - 2013 Brno University of Technology (http://nes.fit.vutbr.cz/ansa)
 *
 * @file TRILLAccess.h
 * @author Marcel Marek (mailto:xscrew02@gmail.com), Vladimir Vesely (mailto:ivesely@fit.vutbr.cz)
 * @date Mar 9, 2013
 * @brief Provides access to CLNSTable module.
 * @detail Provides access to CLNSTable module.
 * @todo C
 */

#ifndef CLNSTABLEACCESS_H_
#define CLNSTABLEACCESS_H_

//#include "common/INETDefs.h"

#include "common/ModuleAccess.h"
#include "ansa/networklayer/clns/CLNSTable.h"

class CLNSTableAccess: public inet::ModuleAccess<CLNSTable>
{
    public:
        CLNSTableAccess() :
            inet::ModuleAccess<CLNSTable>("clnsTable") {}

};

#endif /* CLNSTABLEACCESS_H_ */
