/* Copyright (c) 2009
 *
 * Modular Systems Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *   3. Neither the name Modular Systems Ltd nor the names of its contributors
 *      may be used to endorse or promote products derived from this
 *      software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY MODULAR SYSTEMS LTD AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL MODULAR SYSTEMS OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Modified, debugged, optimized and improved by Henri Beauchamp Feb 2010.
 * HG - now less string operations and case insensitivity, you memory wasting mouse
 */

#include "llviewerprecompiledheaders.h"

#include "lluuid.h"
#include "lluictrlfactory.h"
#include "llscrolllistctrl.h"

#include "llagent.h"
#include "lltracker.h"
#include "llviewerobjectlist.h"
#include "llviewercontrol.h"
#include "jcfloaterareasearch.h"

JCFloaterAreaSearch* JCFloaterAreaSearch::sInstance = NULL;
LLViewerRegion* JCFloaterAreaSearch::sLastRegion = NULL;
S32 JCFloaterAreaSearch::sRequested = 0;
std::map<LLUUID, AObjectDetails> JCFloaterAreaSearch::sObjectDetails;
std::string JCFloaterAreaSearch::sSearchedName;
std::string JCFloaterAreaSearch::sSearchedDesc;
std::string JCFloaterAreaSearch::sSearchedOwner;
std::string JCFloaterAreaSearch::sSearchedGroup;
bool JCFloaterAreaSearch::sSearchingName;
bool JCFloaterAreaSearch::sSearchingDesc;
bool JCFloaterAreaSearch::sSearchingOwner;
bool JCFloaterAreaSearch::sSearchingGroup;

const F32 min_refresh_interval = 0.25f;	// Minimum interval between list refreshes in seconds.

JCFloaterAreaSearch::JCFloaterAreaSearch() :  
LLFloater(),
mCounterText(0),
mResultList(0)
{
	llassert_always(sInstance == NULL);
	sInstance = this;
	mLastUpdateTimer.reset();
}

JCFloaterAreaSearch::~JCFloaterAreaSearch()
{
	sInstance = NULL;
}

void JCFloaterAreaSearch::close(bool app_closing)
{
	if (app_closing)
		LLFloater::close(app_closing);
	else if(sInstance)
		sInstance->setVisible(FALSE);
}

BOOL JCFloaterAreaSearch::postBuild()
{
	mResultList = getChild<LLScrollListCtrl>("result_list");
	mResultList->setCallbackUserData(this);
	mResultList->setDoubleClickCallback(onDoubleClick);
	mResultList->sortByColumn("Name", TRUE);

	mCounterText = getChild<LLTextBox>("counter");

	childSetAction("Refresh", search, this);
	childSetAction("Stop", cancel, this);

	childSetKeystrokeCallback("Name query chunk", onCommitLine, 0);
	childSetKeystrokeCallback("Description query chunk", onCommitLine, 0);
	childSetKeystrokeCallback("Owner query chunk", onCommitLine, 0);
	childSetKeystrokeCallback("Group query chunk", onCommitLine, 0);

	return TRUE;
}

// static
void JCFloaterAreaSearch::checkRegion()
{
	// Check if we changed region, and if we did, clear the object details cache.
	LLViewerRegion* region = gAgent.getRegion();
	if (region != sLastRegion)
	{
		sLastRegion = region;
		sRequested = 0;
		sObjectDetails.clear();
		if (sInstance)
		{
			sInstance->mResultList->deleteAllItems();
			sInstance->mCounterText->setText(std::string("Listed/Pending/Total"));
		}
	}
}

// static
void JCFloaterAreaSearch::toggle()
{
	if (sInstance)
	{
		if (sInstance->getVisible())
		{
			sInstance->setVisible(FALSE);
		}
		else
		{
			checkRegion();
			sInstance->setVisible(TRUE);
		}
	}
	else
	{
		sInstance = new JCFloaterAreaSearch();
		LLUICtrlFactory::getInstance()->buildFloater(sInstance, "floater_area_search.xml");
	}
}

// static
void JCFloaterAreaSearch::onDoubleClick(void *userdata)
{
	JCFloaterAreaSearch *self = (JCFloaterAreaSearch*)userdata;
 	LLScrollListItem *item = self->mResultList->getFirstSelected();
	if (!item) return;
	LLUUID object_id = item->getUUID();
	LLViewerObject* objectp = gObjectList.findObject(object_id);
	if (objectp)
	{
		LLTracker::trackLocation(objectp->getPositionGlobal(), sObjectDetails[object_id].name, "", LLTracker::LOCATION_ITEM);
		gAgent.lookAtObject(object_id, CAMERA_POSITION_OBJECT);
	}
}

// static
void JCFloaterAreaSearch::cancel(void* data)
{
	checkRegion();
	if (sInstance)
	{
		sInstance->close(TRUE);
	}
	sSearchedName = "";
	sSearchedDesc = "";
	sSearchedOwner = "";
	sSearchedGroup = "";
	sSearchingName = false;
	sSearchingDesc = false;
	sSearchingOwner = false;
	sSearchingGroup = false;
}

// static
void JCFloaterAreaSearch::search(void* data)
{
	checkRegion();
	results();
}

// static
void JCFloaterAreaSearch::onCommitLine(LLLineEditor* line, void* user_data)
{
	std::string name = line->getName();
	std::string text = line->getText();
	line->setText(text);

	if (name == "Name query chunk") {sSearchedName = text; sSearchingName = (text.length() > 3);}
	else if (name == "Description query chunk") {sSearchedDesc = text; sSearchingDesc = (text.length() > 3);}
	else if (name == "Owner query chunk") {sSearchedOwner = text; sSearchingOwner = (text.length() > 3);}
	else if (name == "Group query chunk") {sSearchedGroup = text; sSearchingGroup = (text.length() > 3);}

	if (text.length() > 3)
	{
		checkRegion();
		results();
	}
	else
	{

	}
}

// static
void JCFloaterAreaSearch::requestIfNeeded(LLViewerObject *objectp)
{
	LLUUID object_id = objectp->getID();
	if (sObjectDetails.count(object_id) == 0)
	{
		//llinfos << "not in list" << llendl;
		AObjectDetails* details = &sObjectDetails[object_id];
		details->ready = false;
		details->name = "";
		details->desc = "";
		details->owner_id = LLUUID::null;
		details->group_id = LLUUID::null;

		LLMessageSystem* msg = gMessageSystem;
		msg->newMessageFast(_PREHASH_RequestObjectPropertiesFamily);
		msg->nextBlockFast(_PREHASH_AgentData);
		msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
		msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
		msg->nextBlockFast(_PREHASH_ObjectData);
		msg->addU32Fast(_PREHASH_RequestFlags, 0 );
		msg->addUUIDFast(_PREHASH_ObjectID, object_id);
		gAgent.sendReliableMessage();
		//llinfos << "Sent data request for object " << object_id << llendl;
		if(sRequested < 0) 
			sRequested = 1;
		else
			sRequested++;
	}
}

// static
void JCFloaterAreaSearch::results()
{
	//sanity checks
	if (!sInstance) return;
	if (!(sInstance->getVisible())) return;
	if (sRequested > 0 && sInstance->mLastUpdateTimer.getElapsedTimeF32() < min_refresh_interval) return;

	//save our position and clear the list
	LLDynamicArray<LLUUID> selected = sInstance->mResultList->getSelectedIDs();
	S32 scrollpos = sInstance->mResultList->getScrollPos();
	sInstance->mResultList->deleteAllItems();

	//check through all of the objects in the region
	S32 i;
	S32 total = gObjectList.getNumObjects();
	LLViewerRegion* our_region = gAgent.getRegion();
	for (i = 0; i < total; i++)
	{
		LLViewerObject *objectp = gObjectList.getObject(i);

		//check if this is a valid and non-temporary root prim
		if (objectp &&
			objectp->getRegion() == our_region && !objectp->isAvatar() && objectp->isRoot() &&
			!objectp->flagTemporary() && !objectp->flagTemporaryOnRez() && !objectp->isAttachment())
		{
			LLUUID object_id = objectp->getID();

			//request information about this prim if necessary
			if (sObjectDetails.count(object_id) == 0)
			{
				requestIfNeeded(objectp);
			}
			else
			{
				AObjectDetails* details = &sObjectDetails[object_id];
				std::string object_owner;
				std::string object_group;
				bool haveOwner = gCacheName->getFullName(details->owner_id, object_owner);
				bool haveGroup = gCacheName->getGroupName(details->group_id, object_group);

				//check that this object is ready
				if (details->ready)
				{
					bool found = true;

					//check that all of the fields are either a match or not required
					if (found && sSearchingName) found = (details->name.find(sSearchedName) != -1);
					if (found && sSearchingDesc) found = (details->desc.find(sSearchedDesc) != -1);
					if (found && sSearchingOwner) found = (haveOwner && object_owner.find(sSearchedOwner) != -1);
					if (found && sSearchingGroup) found = (haveGroup && object_group.find(sSearchedGroup) != -1);

					if(found)
					{
						LLSD element;
						element["id"] = object_id;
						element["columns"][LIST_OBJECT_NAME]["column"] = "Name";
						element["columns"][LIST_OBJECT_NAME]["value"] = details->name;
						element["columns"][LIST_OBJECT_DESC]["column"] = "Description";
						element["columns"][LIST_OBJECT_DESC]["value"] = details->desc;
						element["columns"][LIST_OBJECT_OWNER]["column"] = "Owner";
						element["columns"][LIST_OBJECT_OWNER]["value"] = object_owner;
						element["columns"][LIST_OBJECT_GROUP]["column"] = "Group";
						element["columns"][LIST_OBJECT_GROUP]["value"] = object_group;
						sInstance->mResultList->addElement(element, ADD_BOTTOM);
					}
				}
			}
		}
	}

	sInstance->mResultList->sortItems();
	sInstance->mResultList->selectMultiple(selected);
	sInstance->mResultList->setScrollPos(scrollpos);
	sInstance->mCounterText->setText(llformat("%d listed/%d pending/%d total", sInstance->mResultList->getItemCount(), sRequested, sObjectDetails.size()));
	sInstance->mLastUpdateTimer.reset();
}

// static
void JCFloaterAreaSearch::callbackLoadOwnerName(const LLUUID& id, const std::string& first, const std::string& last, BOOL is_group, void* data)
{
	results();
}

// static
void JCFloaterAreaSearch::processObjectPropertiesFamily(LLMessageSystem* msg, void** user_data)
{
	checkRegion();

	LLUUID object_id;
	msg->getUUIDFast(_PREHASH_ObjectData, _PREHASH_ObjectID, object_id);

	AObjectDetails* details = &sObjectDetails[object_id];
	if (!details->ready) sRequested--;
	// We cache unknown objects (to avoid having to request them later)
	// and requested objects.
	
	msg->getUUIDFast(_PREHASH_ObjectData, _PREHASH_OwnerID, details->owner_id);
	msg->getUUIDFast(_PREHASH_ObjectData, _PREHASH_GroupID, details->group_id);
	msg->getStringFast(_PREHASH_ObjectData, _PREHASH_Name, details->name);
	msg->getStringFast(_PREHASH_ObjectData, _PREHASH_Description, details->desc);
	details->ready = true;
	gCacheName->get(details->owner_id, FALSE, callbackLoadOwnerName);
	gCacheName->get(details->group_id, TRUE, callbackLoadOwnerName);
}
