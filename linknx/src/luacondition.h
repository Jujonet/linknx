/*
    LinKNX KNX home automation platform
    Copyright (C) 2007 Jean-François Meessen <linknx@ouaye.net>
 
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
 
    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef LUACONDITION_H
#define LUACONDITION_H

#ifdef HAVE_LUA

#include <string>
#include "config.h"
#include "ruleserver.h"
#include "ticpp.h"

class LuaCondition : public Condition
{
public:
    LuaCondition(ChangeListener* cl);
    virtual ~LuaCondition();

    virtual bool evaluate();
    virtual void importXml(ticpp::Element* pConfig);
    virtual void exportXml(ticpp::Element* pConfig);

private:
//    Condition* condition_m;
    ChangeListener* cl_m;
    std::string code_m;
};

#endif // HAVE_LUA

#endif
