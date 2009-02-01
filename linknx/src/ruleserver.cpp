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

#include "ruleserver.h"
#include "services.h"
#include "smsgateway.h"
#include "luacondition.h"
#include "ioport.h"

extern "C"
{
#include "common.h"
}


RuleServer* RuleServer::instance_m;

RuleServer::RuleServer()
{}

RuleServer::~RuleServer()
{
    RuleIdMap_t::iterator it;
    for (it = rulesMap_m.begin(); it != rulesMap_m.end(); it++)
        delete (*it).second;
}

RuleServer* RuleServer::instance()
{
    if (instance_m == 0)
        instance_m = new RuleServer();
    return instance_m;
}

void RuleServer::importXml(ticpp::Element* pConfig)
{
    ticpp::Iterator< ticpp::Element > child("rule");
    for ( child = pConfig->FirstChildElement("rule", false); child != child.end(); child++ )
    {
        std::string id = child->GetAttribute("id");
        bool del = child->GetAttribute("delete") == "true";
        RuleIdMap_t::iterator it = rulesMap_m.find(id);
        if (it == rulesMap_m.end())
        {
            if (del)
                throw ticpp::Exception("Rule not found");
            Rule* rule = new Rule();
            rule->importXml(&(*child));
            rulesMap_m.insert(RuleIdPair_t(id, rule));
        }
        else if (del)
        {
            delete it->second;
            rulesMap_m.erase(it);
        }
        else
            it->second->updateXml(&(*child));
    }
}

void RuleServer::exportXml(ticpp::Element* pConfig)
{
    RuleIdMap_t::iterator it;
    for (it = rulesMap_m.begin(); it != rulesMap_m.end(); it++)
    {
        ticpp::Element pElem("rule");
        (*it).second->exportXml(&pElem);
        pConfig->LinkEndChild(&pElem);
    }
}

int RuleServer::parseDuration(const std::string& duration, bool allowNegative)
{
    if (duration == "")
        return 0;
    std::istringstream val(duration);
    std::string unit;
    int num;
    val >> num;

    if (val.fail() || (num < 0 && !allowNegative))
    {
        std::stringstream msg;
        msg << "RuleServer::parseDuration: Bad value: '" << duration << "'" << std::endl;
        throw ticpp::Exception(msg.str());
    }
    val >> unit;
    if (unit == "d")
        num = num * 3600 * 24;
    else if (unit == "h")
        num = num * 3600;
    else if (unit == "m")
        num = num * 60;
    else if (unit != "" && unit != "s")
    {
        std::stringstream msg;
        msg << "RuleServer::parseDuration: Bad unit: '" << unit << "'" << std::endl;
        throw ticpp::Exception(msg.str());
    }
    return num;
}

std::string RuleServer::formatDuration(int duration)
{
    if (duration == 0)
        return "";
    std::stringstream output;
    if (duration % (3600*24) == 0)
        output << (duration / (3600*24)) << 'd';
    else if (duration % 3600 == 0)
        output << (duration / 3600) << 'h';
    else if (duration % 60 == 0)
        output << (duration / 60) << 'm';
    else
        output << duration;
    return output.str();
}

Logger& Rule::logger_m(Logger::getInstance("Rule"));

Rule::Rule() : condition_m(0), prevValue_m(false), isActive_m(false)
{}

Rule::~Rule()
{
    if (condition_m != 0)
        delete condition_m;

    ActionsList_t::iterator it;
    for(it=actionsList_m.begin(); it != actionsList_m.end(); ++it)
        delete (*it);
    for(it=actionsListFalse_m.begin(); it != actionsListFalse_m.end(); ++it)
        delete (*it);
}

void Rule::importXml(ticpp::Element* pConfig)
{
    pConfig->GetAttribute("id", &id_m, false);

    std::string value = pConfig->GetAttribute("active");
    isActive_m = !(value == "off" || value == "false" || value == "no");

    logger_m.infoStream() << "Rule: Configuring " << getID() << " (active=" << isActive_m << ")" << endlog;

    ticpp::Element* pCondition = pConfig->FirstChildElement("condition");
    condition_m = Condition::create(pCondition, this);

    ticpp::Iterator<ticpp::Element> actionListIt("actionlist");
    for ( actionListIt = pConfig->FirstChildElement("actionlist"); actionListIt != actionListIt.end(); actionListIt++ )
    {
        bool isFalse = (*actionListIt).GetAttribute("type") == "on-false";
        logger_m.infoStream() << "ActionList: Configuring " << (isFalse ? "'on-false'" : "" ) << endlog;
        ticpp::Iterator<ticpp::Element> actionIt("action");
        for (actionIt = (*actionListIt).FirstChildElement("action", false); actionIt != actionIt.end(); actionIt++ )
        {
            Action* action = Action::create(&(*actionIt));
            if (isFalse)
                actionsListFalse_m.push_back(action);
            else
                actionsList_m.push_back(action);
        }
    }
    logger_m.infoStream() << "Rule: Configuration done" << endlog;
}

void Rule::updateXml(ticpp::Element* pConfig)
{
    std::string value = pConfig->GetAttribute("active");
    if (value != "")
        isActive_m = !(value == "off" || value == "false" || value == "no");

    logger_m.infoStream() << "Rule: Reconfiguring " << getID() << " (active=" << isActive_m << ")" << endlog;

    ticpp::Element* pCondition = pConfig->FirstChildElement("condition", false);
    if (pCondition != NULL)
    {
        if (condition_m != 0)
            delete condition_m;
        logger_m.infoStream() << "Rule: Reconfiguring condition " << getID() << endlog;
        condition_m = Condition::create(pCondition, this);
    }

    ticpp::Element* pActionList = pConfig->FirstChildElement("actionlist", false);

    if (pActionList != NULL)
    {
        ActionsList_t::iterator it;
        for(it=actionsList_m.begin(); it != actionsList_m.end(); ++it)
            delete (*it);
        actionsList_m.clear();
        for(it=actionsListFalse_m.begin(); it != actionsListFalse_m.end(); ++it)
            delete (*it);
        actionsListFalse_m.clear();

        ticpp::Iterator<ticpp::Element> actionListIt("actionlist");
        for ( actionListIt = pActionList; actionListIt != actionListIt.end(); actionListIt++ )
        {
            bool isFalse = (*actionListIt).GetAttribute("type") == "on-false";
            logger_m.infoStream() << "ActionList: Reconfiguring " << (isFalse ? "'on-false'" : "" ) << endlog;
            ticpp::Iterator<ticpp::Element> actionIt("action");
            for (actionIt = (*actionListIt).FirstChildElement("action"); actionIt != actionIt.end(); actionIt++ )
            {
                Action* action = Action::create(&(*actionIt));
                if (isFalse)
                    actionsListFalse_m.push_back(action);
                else
                    actionsList_m.push_back(action);
            }
        }
    }
    logger_m.infoStream() << "Rule: Reconfiguration done" << endlog;
}

void Rule::exportXml(ticpp::Element* pConfig)
{
    if (id_m != "")
        pConfig->SetAttribute("id", id_m);
    if (isActive_m == false)
        pConfig->SetAttribute("active", "no");
    if (condition_m)
    {
        ticpp::Element pCond("condition");
        condition_m->exportXml(&pCond);
        pConfig->LinkEndChild(&pCond);
    }

    ActionsList_t::iterator it;
    if (actionsList_m.begin() != actionsList_m.end())
    {
        ticpp::Element pList("actionlist");
        pConfig->LinkEndChild(&pList);

        for(it=actionsList_m.begin(); it != actionsList_m.end(); ++it)
        {
            ticpp::Element pElem("action");
            (*it)->exportXml(&pElem);
            pList.LinkEndChild(&pElem);
        }
    }
    if (actionsListFalse_m.begin() != actionsListFalse_m.end())
    {
        ticpp::Element pList("actionlist");
        pList.SetAttribute("type", "on-false");
        pConfig->LinkEndChild(&pList);

        for(it=actionsListFalse_m.begin(); it != actionsListFalse_m.end(); ++it)
        {
            ticpp::Element pElem("action");
            (*it)->exportXml(&pElem);
            pList.LinkEndChild(&pElem);
        }
    }
}

void Rule::onChange(Object* object)
{
    evaluate();
}

void Rule::evaluate()
{
    if (isActive_m)
    {
        ActionsList_t::iterator it;
        bool curValue = condition_m->evaluate();
        if (curValue && !prevValue_m)
        {
            for(it=actionsList_m.begin(); it != actionsList_m.end(); ++it)
                (*it)->execute();
        }
        else if (!curValue && prevValue_m)
        {
            for(it=actionsListFalse_m.begin(); it != actionsListFalse_m.end(); ++it)
                (*it)->execute();
        }
        prevValue_m = curValue;
    }
}

Logger& Action::logger_m(Logger::getInstance("Action"));

Action* Action::create(const std::string& type)
{
    if (type == "dim-up")
        return new DimUpAction();
    else if (type == "set-value")
        return new SetValueAction();
    else if (type == "copy-value")
        return new CopyValueAction();
    else if (type == "cycle-on-off")
        return new CycleOnOffAction();
    else if (type == "send-sms")
        return new SendSmsAction();
    else if (type == "send-email")
        return new SendEmailAction();
    else if (type == "shell-cmd")
        return new ShellCommandAction();
    else if (type == "tx")
        return new TxAction();
    else
        return 0;
}

Action* Action::create(ticpp::Element* pConfig)
{
    std::string type = pConfig->GetAttribute("type");
    int delay;
    delay = RuleServer::parseDuration(pConfig->GetAttribute("delay"));
    Action* action = Action::create(type);
    if (action == 0)
    {
        std::stringstream msg;
        msg << "Action type not supported: '" << type << "'" << std::endl;
        throw ticpp::Exception(msg.str());
    }
    action->delay_m = delay;
    action->importXml(pConfig);
    return action;
}

void Action::exportXml(ticpp::Element* pConfig)
{
    if (delay_m != 0)
        pConfig->SetAttribute("delay", RuleServer::formatDuration(delay_m));
}

DimUpAction::DimUpAction() : start_m(0), stop_m(255), duration_m(60), object_m(0)
{}

DimUpAction::~DimUpAction()
{}

void DimUpAction::importXml(ticpp::Element* pConfig)
{
    std::string id;
    id = pConfig->GetAttribute("id");
    Object* obj = ObjectController::instance()->getObject(id); 
    object_m = dynamic_cast<U8Object*>(obj); 
    if (!object_m)
    {
        std::stringstream msg;
        msg << "Wrong Object type for DimUpAction: '" << id << "'" << std::endl;
        throw ticpp::Exception(msg.str());
    }
    pConfig->GetAttribute("start", &start_m);
    pConfig->GetAttribute("stop", &stop_m);
    duration_m = RuleServer::parseDuration(pConfig->GetAttribute("duration"));
    logger_m.infoStream() << "DimUpAction: Configured for object " << object_m->getID()
    << " with start=" << start_m
    << "; stop=" << stop_m
    << "; duration=" << duration_m << endlog;
}

void DimUpAction::exportXml(ticpp::Element* pConfig)
{
    pConfig->SetAttribute("type", "dim-up");
    pConfig->SetAttribute("id", object_m->getID());
    pConfig->SetAttribute("start", start_m);
    pConfig->SetAttribute("stop", stop_m);
    pConfig->SetAttribute("duration", RuleServer::formatDuration(duration_m));

    Action::exportXml(pConfig);
}

void DimUpAction::Run (pth_sem_t * stop)
{
    pth_sleep(delay_m);
    if (stop_m > start_m)
    {
        logger_m.infoStream() << "Execute DimUpAction" << endlog;

        unsigned long step = ((duration_m * 1000) / (stop_m - start_m)) * 1000;
        for (int idx=start_m; idx < stop_m; idx++)
        {
            object_m->setIntValue(idx);
            pth_usleep(step);
            if (object_m->getIntValue() < idx)
            {
                logger_m.infoStream() << "Abort DimUpAction" << endlog;
                return;
            }
        }
    }
    else
    {
        logger_m.infoStream() << "Execute DimUpAction (decrease)" << endlog;

        unsigned long step = ((duration_m * 1000) / (start_m - stop_m)) * 1000;
        for (int idx=start_m; idx > stop_m; idx--)
        {
            object_m->setIntValue(idx);
            pth_usleep(step);
            if (object_m->getIntValue() > idx)
            {
                logger_m.infoStream() << "Abort DimUpAction" << endlog;
                return;
            }
        }
    }
}

SetValueAction::SetValueAction() : value_m(0), object_m(0)
{}

SetValueAction::~SetValueAction()
{
    if (value_m)
        delete value_m;
}

void SetValueAction::importXml(ticpp::Element* pConfig)
{
    std::string id;
    id = pConfig->GetAttribute("id");
    object_m = ObjectController::instance()->getObject(id);

    std::string value;
    value = pConfig->GetAttribute("value");

    value_m = object_m->createObjectValue(value);
    logger_m.infoStream() << "SetValueAction: Configured for object " << object_m->getID() << " with value " << value_m->toString() << endlog;
}

void SetValueAction::exportXml(ticpp::Element* pConfig)
{
    pConfig->SetAttribute("type", "set-value");
    pConfig->SetAttribute("id", object_m->getID());
    pConfig->SetAttribute("value", value_m->toString());

    Action::exportXml(pConfig);
}

void SetValueAction::Run (pth_sem_t * stop)
{
    pth_sleep(delay_m);
    logger_m.infoStream() << "Execute SetValueAction with value " << value_m->toString() << endlog;
    if (object_m)
        object_m->setValue(value_m);
}

CopyValueAction::CopyValueAction() : from_m(0), to_m(0)
{}

CopyValueAction::~CopyValueAction()
{}

void CopyValueAction::importXml(ticpp::Element* pConfig)
{
    std::string obj;
    obj = pConfig->GetAttribute("to");
    to_m = ObjectController::instance()->getObject(obj);
    obj = pConfig->GetAttribute("from");
    from_m = ObjectController::instance()->getObject(obj);
    if (from_m->getType() != to_m->getType())
    {
        std::stringstream msg;
        msg << "Incompatible object types for CopyValueAction: from='" << from_m->getID() << "' to='" << to_m->getID() << "'" << std::endl;
        throw ticpp::Exception(msg.str());
    }

    logger_m.infoStream() << "CopyValueAction: Configured to copy value from " << from_m->getID() << " to " << to_m->getID() << endlog;
}

void CopyValueAction::exportXml(ticpp::Element* pConfig)
{
    pConfig->SetAttribute("type", "copy-value");
    pConfig->SetAttribute("from", from_m->getID());
    pConfig->SetAttribute("to", to_m->getID());

    Action::exportXml(pConfig);
}

void CopyValueAction::Run (pth_sem_t * stop)
{
    if (from_m && to_m)
    {
        pth_sleep(delay_m);
        try
        {
            std::string value = from_m->getValue();
            logger_m.infoStream() << "Execute CopyValueAction set " << to_m->getID() << " with value " << value << endlog;
            to_m->setValue(value);
        }
        catch( ticpp::Exception& ex )
        {
            logger_m.warnStream() << "Error in CopyValueAction: " << ex.m_details << endlog;
        }
    }
}

CycleOnOffAction::CycleOnOffAction()
    : object_m(0), count_m(0), delayOn_m(0), delayOff_m(0), stopCondition_m(0), running_m(false)
{}

CycleOnOffAction::~CycleOnOffAction()
{}

void CycleOnOffAction::importXml(ticpp::Element* pConfig)
{
    std::string id;
    id = pConfig->GetAttribute("id");
    Object* obj = ObjectController::instance()->getObject(id); 
    object_m = dynamic_cast<SwitchingObject*>(obj); 
    if (!object_m)
    {
        std::stringstream msg;
        msg << "Wrong Object type for CycleOnOffAction: '" << id << "'" << std::endl;
        throw ticpp::Exception(msg.str());
    }

    delayOn_m = RuleServer::parseDuration(pConfig->GetAttribute("on"));
    delayOff_m = RuleServer::parseDuration(pConfig->GetAttribute("off"));
    pConfig->GetAttribute("count", &count_m);

    ticpp::Iterator< ticpp::Element > child;
    ticpp::Element* pStopCondition = pConfig->FirstChildElement("stopcondition", false);
    if (pStopCondition)
    {
        if (stopCondition_m)
            delete stopCondition_m;
        stopCondition_m = Condition::create(pStopCondition, this);
    }

    logger_m.infoStream() << "CycleOnOffAction: Configured for object " << object_m->getID()
    << " with delay_on=" << delayOn_m
    << "; delay_off=" << delayOff_m
    << "; count=" << count_m << endlog;
}

void CycleOnOffAction::exportXml(ticpp::Element* pConfig)
{
    pConfig->SetAttribute("type", "cycle-on-off");
    pConfig->SetAttribute("id", object_m->getID());
    pConfig->SetAttribute("on", RuleServer::formatDuration(delayOn_m));
    pConfig->SetAttribute("off", RuleServer::formatDuration(delayOff_m));
    pConfig->SetAttribute("count", count_m);

    Action::exportXml(pConfig);

    if (stopCondition_m)
    {
        ticpp::Element pCond("stopcondition");
        stopCondition_m->exportXml(&pCond);
        pConfig->LinkEndChild(&pCond);
    }
}

void CycleOnOffAction::onChange(Object* object)
{
    if (stopCondition_m && running_m && stopCondition_m->evaluate())
        running_m = false;
}

void CycleOnOffAction::Run (pth_sem_t * stop)
{
    if (!object_m)
        return;
    running_m = true;
    pth_sleep(delay_m);
    logger_m.infoStream() << "Execute CycleOnOffAction" << endlog;
    for (int i=0; i<count_m; i++)
    {
        if (!running_m)
            break;
        object_m->setBoolValue(true);
        pth_sleep(delayOn_m);
        if (!running_m)
            break;
        object_m->setBoolValue(false);
        pth_sleep(delayOff_m);
    }
    if (running_m)
        running_m = false;
    else
        logger_m.infoStream() << "CycleOnOffAction stopped by condition" << endlog;
}

SendSmsAction::SendSmsAction()
{}

SendSmsAction::~SendSmsAction()
{}

void SendSmsAction::importXml(ticpp::Element* pConfig)
{
    id_m = pConfig->GetAttribute("id");
    value_m = pConfig->GetAttribute("value");

    logger_m.infoStream() << "SendSmsAction: Configured for id " << id_m << " with value " << value_m << endlog;
}

void SendSmsAction::exportXml(ticpp::Element* pConfig)
{
    pConfig->SetAttribute("type", "send-sms");
    pConfig->SetAttribute("id", id_m);
    pConfig->SetAttribute("value", value_m);

    Action::exportXml(pConfig);
}

void SendSmsAction::Run (pth_sem_t * stop)
{
    pth_sleep(delay_m);
    logger_m.infoStream() << "Execute SendSmsAction with value " << value_m << endlog;

    Services::instance()->getSmsGateway()->sendSms(id_m, value_m);
}

SendEmailAction::SendEmailAction()
{}

SendEmailAction::~SendEmailAction()
{}

void SendEmailAction::importXml(ticpp::Element* pConfig)
{
    to_m = pConfig->GetAttribute("to");
    subject_m = pConfig->GetAttribute("subject");
    text_m = pConfig->GetText();

    logger_m.infoStream() << "SendEmailAction: Configured to=" << to_m << " subject=" << subject_m << endlog;
}

void SendEmailAction::exportXml(ticpp::Element* pConfig)
{
    pConfig->SetAttribute("type", "send-email");
    pConfig->SetAttribute("to", to_m);
    pConfig->SetAttribute("subject", subject_m);
    if (text_m != "")
        pConfig->SetText(text_m);

    Action::exportXml(pConfig);
}

void SendEmailAction::Run (pth_sem_t * stop)
{
    pth_sleep(delay_m);
    logger_m.infoStream() << "Execute SendEmailAction: to=" << to_m << " subject=" << subject_m << endlog;

    Services::instance()->getEmailGateway()->sendEmail(to_m, subject_m, text_m);
}

ShellCommandAction::ShellCommandAction()
{}

ShellCommandAction::~ShellCommandAction()
{}

void ShellCommandAction::importXml(ticpp::Element* pConfig)
{
    cmd_m = pConfig->GetAttribute("cmd");

    logger_m.infoStream() << "ShellCommandAction: Configured" << endlog;
}

void ShellCommandAction::exportXml(ticpp::Element* pConfig)
{
    pConfig->SetAttribute("type", "shell-cmd");
    pConfig->SetAttribute("cmd", cmd_m);

    Action::exportXml(pConfig);
}

void ShellCommandAction::Run (pth_sem_t * stop)
{
    pth_sleep(delay_m);
    logger_m.infoStream() << "Execute ShellCommandAction: " << cmd_m << endlog;

    int ret = pth_system(cmd_m.c_str());
    if (ret != 0)
        logger_m.infoStream() << "Execute ShellCommandAction: returned " << ret << endlog;
}


Logger& Condition::logger_m(Logger::getInstance("Condition"));

Condition* Condition::create(const std::string& type, ChangeListener* cl)
{
    if (type == "and")
        return new AndCondition(cl);
    else if (type == "or")
        return new OrCondition(cl);
    else if (type == "not")
        return new NotCondition(cl);
    else if (type == "object")
        return new ObjectCondition(cl);
    else if (type == "timer")
        return new TimerCondition(cl);
    else if (type == "object-src")
        return new ObjectSourceCondition(cl);
    else if (type == "time-counter")
        return new TimeCounterCondition(cl);
#ifdef HAVE_LUA
    else if (type == "lua")
        return new LuaCondition(cl);
#endif
    else
        return 0;
}

Condition* Condition::create(ticpp::Element* pConfig, ChangeListener* cl)
{
    std::string type;
    type = pConfig->GetAttribute("type");
    Condition* condition = Condition::create(type, cl);
    if (condition == 0)
    {
        std::stringstream msg;
        msg << "Condition type not supported: '" << type << "'";
        throw ticpp::Exception(msg.str());
    }
    condition->importXml(pConfig);
    return condition;
}

AndCondition::AndCondition(ChangeListener* cl) : cl_m(cl)
{}

AndCondition::~AndCondition()
{
    ConditionsList_t::iterator it;
    for(it=conditionsList_m.begin(); it != conditionsList_m.end(); ++it)
        delete (*it);
}

bool AndCondition::evaluate()
{
    ConditionsList_t::iterator it;
    for(it=conditionsList_m.begin(); it != conditionsList_m.end(); ++it)
        if (!(*it)->evaluate())
            return false;
    return true;
}

void AndCondition::importXml(ticpp::Element* pConfig)
{
    ticpp::Iterator< ticpp::Element > child("condition");
    for ( child = pConfig->FirstChildElement("condition"); child != child.end(); child++ )
    {
        Condition* condition = Condition::create(&(*child), cl_m);
        conditionsList_m.push_back(condition);
    }
}

void AndCondition::exportXml(ticpp::Element* pConfig)
{
    pConfig->SetAttribute("type", "and");
    ConditionsList_t::iterator it;
    for (it = conditionsList_m.begin(); it != conditionsList_m.end(); it++)
    {
        ticpp::Element pElem("condition");
        (*it)->exportXml(&pElem);
        pConfig->LinkEndChild(&pElem);
    }
}

OrCondition::OrCondition(ChangeListener* cl) : cl_m(cl)
{}

OrCondition::~OrCondition()
{
    ConditionsList_t::iterator it;
    for(it=conditionsList_m.begin(); it != conditionsList_m.end(); ++it)
        delete (*it);
}

bool OrCondition::evaluate()
{
    ConditionsList_t::iterator it;
    for(it=conditionsList_m.begin(); it != conditionsList_m.end(); ++it)
        if ((*it)->evaluate())
            return true;
    return false;
}

void OrCondition::importXml(ticpp::Element* pConfig)
{
    ticpp::Iterator< ticpp::Element > child("condition");
    for ( child = pConfig->FirstChildElement("condition"); child != child.end(); child++ )
    {
        Condition* condition = Condition::create(&(*child), cl_m);
        conditionsList_m.push_back(condition);
    }
}

void OrCondition::exportXml(ticpp::Element* pConfig)
{
    pConfig->SetAttribute("type", "or");
    ConditionsList_t::iterator it;
    for (it = conditionsList_m.begin(); it != conditionsList_m.end(); it++)
    {
        ticpp::Element pElem("condition");
        (*it)->exportXml(&pElem);
        pConfig->LinkEndChild(&pElem);
    }
}

NotCondition::NotCondition(ChangeListener* cl) : cl_m(cl), condition_m(0)
{}

NotCondition::~NotCondition()
{
    if (condition_m)
        delete condition_m;
}

bool NotCondition::evaluate()
{
    return !condition_m->evaluate();
}

void NotCondition::importXml(ticpp::Element* pConfig)
{
    condition_m = Condition::create(pConfig->FirstChildElement("condition"), cl_m);
}

void NotCondition::exportXml(ticpp::Element* pConfig)
{
    pConfig->SetAttribute("type", "not");
    if (condition_m)
    {
        ticpp::Element pElem("condition");
        condition_m->exportXml(&pElem);
        pConfig->LinkEndChild(&pElem);
    }
}

ObjectCondition::ObjectCondition(ChangeListener* cl) : value_m(0), cl_m(cl), trigger_m(false), op_m(eq)
{}

ObjectCondition::~ObjectCondition()
{
    if (value_m)
        delete value_m;
}

bool ObjectCondition::evaluate()
{
    // if no value is defined, condition is always true
    bool val = (value_m == 0);
    if (!val)
    {
        int res = object_m->compare(value_m);
        val = ((op_m & eq) && (res == 0)) || ((op_m & lt) && (res == -1)) || ((op_m & gt) && (res == 1));
    }
    logger_m.infoStream() << "ObjectCondition (id='" << object_m->getID()
    << "') evaluated as '" << val
    << "'" << endlog;
    return val;
}

void ObjectCondition::importXml(ticpp::Element* pConfig)
{
    std::string trigger;
    trigger = pConfig->GetAttribute("trigger");
    std::string id;
    id = pConfig->GetAttribute("id");
    object_m = ObjectController::instance()->getObject(id);

    if (trigger == "true")
    {
        trigger_m = true;
        object_m->addChangeListener(cl_m);
    }

    std::string value;
    value = pConfig->GetAttribute("value");
    if (value != "")
    {
        value_m = object_m->createObjectValue(value);
        logger_m.infoStream() << "ObjectCondition: configured value_m='" << value_m->toString() << "'" << endlog;
    }
    else
    {
        logger_m.infoStream() << "ObjectCondition: configured, no value specified" << endlog;
    }

    std::string op;
    op = pConfig->GetAttribute("op");
    if (op == "" || op == "eq")
        op_m = eq;
    else if (op == "lt")
        op_m = lt;
    else if (op == "gt")
        op_m = gt;
    else if (op == "ne")
        op_m = lt | gt;
    else if (op == "lte")
        op_m = gt | eq;
    else if (op == "gte")
        op_m = gt | eq;
    else
    {
        std::stringstream msg;
        msg << "ObjectCondition: operation not supported: '" << op << "'";
        throw ticpp::Exception(msg.str());
    }
}

void ObjectCondition::exportXml(ticpp::Element* pConfig)
{
    pConfig->SetAttribute("type", "object");
    pConfig->SetAttribute("id", object_m->getID());
    if (op_m != eq)
    {
        std::string op;
        if (op_m == lt)
            op = "lt";
        else if (op_m == gt)
            op = "gt";
        else if (op_m == (lt | eq))
            op = "lte";
        else if (op_m == (gt | eq))
            op = "gte";
        else
            op = "ne";
        pConfig->SetAttribute("op", op);
    }
    pConfig->SetAttribute("value", value_m->toString());
    if (trigger_m)
        pConfig->SetAttribute("trigger", "true");
}

ObjectSourceCondition::ObjectSourceCondition(ChangeListener* cl) : ObjectCondition(cl), src_m(0)
{}

ObjectSourceCondition::~ObjectSourceCondition()
{}

bool ObjectSourceCondition::evaluate()
{
    bool val = (src_m == object_m->getLastTx()) && ObjectCondition::evaluate();
    logger_m.infoStream() << "ObjectSourceCondition (id='" << object_m->getID()
    << "') evaluated as '" << val
    << "'" << endlog;
    return val;
}

void ObjectSourceCondition::importXml(ticpp::Element* pConfig)
{
    std::string src;
    src = pConfig->GetAttribute("src");

    src_m = readaddr(src.c_str());
    ObjectCondition::importXml(pConfig);
}

void ObjectSourceCondition::exportXml(ticpp::Element* pConfig)
{
    ObjectCondition::exportXml(pConfig);
    pConfig->SetAttribute("type", "object-src");
    pConfig->SetAttribute("src", writeaddr(src_m));
}


TimerCondition::TimerCondition(ChangeListener* cl)
        : PeriodicTask(cl), trigger_m(false)
{}

TimerCondition::~TimerCondition()
{}

bool TimerCondition::evaluate()
{
    Condition::logger_m.infoStream() << "TimerCondition evaluated as '" << value_m << "'" << endlog;
    return value_m;
}

void TimerCondition::importXml(ticpp::Element* pConfig)
{
    std::string trigger;
    trigger = pConfig->GetAttribute("trigger");

    if (trigger == "true")
        trigger_m = true;
    else
        cl_m = 0;

    ticpp::Element* at = pConfig->FirstChildElement("at", false);
    ticpp::Element* every = pConfig->FirstChildElement("every", false);
    if (at && every)
        throw ticpp::Exception("Timer can't define <at> and <every> elements simultaneously");
    if (at)
    {
        if (at_m)
            delete at_m;
        at_m = TimeSpec::create(at, this);
    }
    else if (every)
        after_m = RuleServer::parseDuration(every->GetText());
    else
        throw ticpp::Exception("Timer must define <at> or <every> elements");

    ticpp::Element* during = pConfig->FirstChildElement("during", false);
    ticpp::Element* until = pConfig->FirstChildElement("until", false);
    if (during && until)
        throw ticpp::Exception("Timer can't define <until> and <during> elements simultaneously");
    if (during)
    {
        during_m = RuleServer::parseDuration(during->GetText());
        if (every && after_m > during_m)
            after_m -= during_m;
        else if (every)
            throw ticpp::Exception("Parameter <every> must be greater than <during>");
    }
    else if (until)
    {
        if (until_m)
            delete until_m;
        until_m = TimeSpec::create(until, this);
//        until_m.importXml(until);
        during_m = -1;
    }
    else
        during_m = 0;

    reschedule(0);
}

void TimerCondition::exportXml(ticpp::Element* pConfig)
{
    pConfig->SetAttribute("type", "timer");
    if (trigger_m)
        pConfig->SetAttribute("trigger", "true");

    if (after_m == -1)
    {
        ticpp::Element pAt("at");
        at_m->exportXml(&pAt);
        pConfig->LinkEndChild(&pAt);
    }
    else
    {
        ticpp::Element pEvery("every");
        int every = after_m;
        if (during_m > 0)
            every += during_m;
        pEvery.SetText(RuleServer::formatDuration(every));
        pConfig->LinkEndChild(&pEvery);
    }

    if (during_m == -1)
    {
        ticpp::Element pUntil("until");
        until_m->exportXml(&pUntil);
        pConfig->LinkEndChild(&pUntil);
    }
    else if (during_m != 0)
    {
        ticpp::Element pDuring("during");
        pDuring.SetText(RuleServer::formatDuration(during_m));
        pConfig->LinkEndChild(&pDuring);
    }
}

TimeCounterCondition::TimeCounterCondition(ChangeListener* cl) : cl_m(cl), condition_m(0), threshold_m(0), resetDelay_m(0), lastVal_m(false), lastTime_m(0), counter_m(0)
{}

TimeCounterCondition::~TimeCounterCondition()
{
    if (condition_m)
        delete condition_m;
}

bool TimeCounterCondition::evaluate()
{
    time_t now = time(0);
    bool val = condition_m->evaluate(); 
    if (lastVal_m)
    {
        counter_m += now - lastTime_m;
        Condition::logger_m.infoStream() << "TimeCounterCondition: counter is now  '" << counter_m << "'" << endlog;
    }
    if (val)
    {
        lastTime_m = now;
        lastVal_m = true;
        execTime_m = now + (threshold_m - counter_m) + 1;
        reschedule(0);
    }
    else if (lastVal_m)
    {
        lastTime_m = now;
        lastVal_m = false;
        execTime_m = now + resetDelay_m + 1;
        reschedule(0);
    }

    if (lastVal_m == false && lastTime_m > 0 && (now-lastTime_m) > resetDelay_m)
    {
        counter_m = 0;
        lastTime_m = 0;
    }
    return (counter_m >= threshold_m);
}

void TimeCounterCondition::onTimer(time_t time)
{
    cl_m->onChange(0);
}

void TimeCounterCondition::importXml(ticpp::Element* pConfig)
{
    threshold_m = RuleServer::parseDuration(pConfig->GetAttribute("threshold"));
    resetDelay_m = RuleServer::parseDuration(pConfig->GetAttribute("reset-delay"));
    condition_m = Condition::create(pConfig->FirstChildElement("condition"), cl_m);
}

void TimeCounterCondition::exportXml(ticpp::Element* pConfig)
{
    pConfig->SetAttribute("type", "time-counter");
    if (threshold_m != 0)
        pConfig->SetAttribute("threshold", RuleServer::formatDuration(threshold_m));
    if (resetDelay_m != 0)
        pConfig->SetAttribute("reset-delay", RuleServer::formatDuration(resetDelay_m));

    if (condition_m)
    {
        ticpp::Element pElem("condition");
        condition_m->exportXml(&pElem);
        pConfig->LinkEndChild(&pElem);
    }
}

