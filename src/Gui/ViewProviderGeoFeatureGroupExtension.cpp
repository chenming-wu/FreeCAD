/***************************************************************************
 *   Copyright (c) 2011 Juergen Riegel <FreeCAD@juergen-riegel.net>        *
 *   Copyright (c) 2015 Alexander Golubev (Fat-Zer) <fatzer2@gmail.com>    *
 *   Copyright (c) 2016 Stefan Tröger <stefantroeger@gmx.net>              *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/


#include "PreCompiled.h"

#ifndef _PreComp_
#endif

#include "ViewProviderGeoFeatureGroupExtension.h"
#include "Command.h"
#include "Application.h"
#include "Document.h"
#include <App/GeoFeatureGroupExtension.h>
#include "SoFCUnifiedSelection.h"

using namespace Gui;

EXTENSION_PROPERTY_SOURCE(Gui::ViewProviderGeoFeatureGroupExtension, Gui::ViewProviderGroupExtension)

ViewProviderGeoFeatureGroupExtension::ViewProviderGeoFeatureGroupExtension()
{
    initExtensionType(ViewProviderGeoFeatureGroupExtension::getExtensionClassTypeId());

    pcGroupChildren = new SoFCSelectionRoot;
    pcGroupChildren->ref();
}

ViewProviderGeoFeatureGroupExtension::~ViewProviderGeoFeatureGroupExtension()
{
    pcGroupChildren->unref();
    pcGroupChildren = 0;
}

void ViewProviderGeoFeatureGroupExtension::extensionClaimChildren3D(
        std::vector<App::DocumentObject*> &children) const 
{

    //all object in the group must be claimed in 3D, as we are a coordinate system for all of them
    auto* ext = getExtendedViewProvider()->getObject()->getExtensionByType<App::GeoFeatureGroupExtension>();
    if (ext) {
        const auto &objs = ext->Group.getValues();
        children.insert(children.end(),objs.begin(),objs.end());
    }
}

void ViewProviderGeoFeatureGroupExtension::extensionClaimChildren(
        std::vector<App::DocumentObject *> &children) const 
{
    buildClaimedChildren();
    auto* group = getExtendedViewProvider()->getObject()->getExtensionByType<App::GeoFeatureGroupExtension>();
    const auto &objs = group->_ClaimedChildren.getValues();
    children.insert(children.end(),objs.begin(),objs.end());
}

void ViewProviderGeoFeatureGroupExtension::extensionAttach(App::DocumentObject* pcObject)
{
    ViewProviderGroupExtension::extensionAttach(pcObject);
    getExtendedViewProvider()->addDisplayMaskMode(pcGroupChildren, "Group");
}

void ViewProviderGeoFeatureGroupExtension::extensionSetDisplayMode(const char* ModeName)
{
    if ( strcmp("Group",ModeName)==0 )
        getExtendedViewProvider()->setDisplayMaskMode("Group");

    ViewProviderGroupExtension::extensionSetDisplayMode( ModeName );
}

void ViewProviderGeoFeatureGroupExtension::extensionGetDisplayModes(std::vector<std::string> &StrList) const
{
    // get the modes of the father
    ViewProviderGroupExtension::extensionGetDisplayModes(StrList);

    // add your own modes
    StrList.push_back("Group");
}

void ViewProviderGeoFeatureGroupExtension::extensionUpdateData(const App::Property* prop)
{
    auto group = getExtendedViewProvider()->getObject()->getExtensionByType<App::GeoFeatureGroupExtension>();
    if(group) {
        if (prop == &group->Group) {
            buildClaimedChildren();
            plainGroupConns.clear();
            if(linkView) {
                for(auto obj : group->Group.getValues()) {
                    // check for plain group
                    if(!obj || !obj->getNameInDocument() 
                            || !obj->hasExtension(App::GroupExtension::getExtensionClassTypeId(),false))
                        continue;
                    plainGroupConns.push_back(obj->signalChanged.connect(boost::bind(
                                    &ViewProviderGeoFeatureGroupExtension::slotPlainGroupChanged,this,_1,_2)));
                }
            }
        } else if(prop == &group->placement()) 
            getExtendedViewProvider()->setTransformation ( group->placement().getValue().toMatrix() );
    }

    ViewProviderGroupExtension::extensionUpdateData ( prop );
}

void ViewProviderGeoFeatureGroupExtension::buildClaimedChildren() const {
    auto* group = getExtendedViewProvider()->getObject()->getExtensionByType<App::GeoFeatureGroupExtension>();
    if(!group)
        return;

    auto model = group->Group.getValues ();
    std::set<App::DocumentObject*> outSet; //< set of objects not to claim (childrens of childrens)

    // search for objects handled (claimed) by the features
    for (auto obj: model) {
        //stuff in another geofeaturegroup is not in the model anyway
        if (!obj || obj->hasExtension(App::GeoFeatureGroupExtension::getExtensionClassTypeId())) { continue; }

        Gui::ViewProvider* vp = Gui::Application::Instance->getViewProvider ( obj );
        if (!vp || vp == getExtendedViewProvider()) { continue; }

        auto children = vp->claimChildren();
        group->filterLinksByScope(obj,children);
        outSet.insert(children.begin(),children.end());
    }

    // remove the otherwise handled objects, preserving their order so the order in the TreeWidget is correct
    for(auto it=model.begin();it!=model.end();) {
        auto obj = *it;
        if(!obj || !obj->getNameInDocument() || outSet.count(obj))
            it = model.erase(it);
        else
            ++it;
    }
    if(model != group->_ClaimedChildren.getValues())
        group->_ClaimedChildren.setValues(model);
}

namespace Gui {
EXTENSION_PROPERTY_SOURCE_TEMPLATE(Gui::ViewProviderGeoFeatureGroupExtensionPython, Gui::ViewProviderGeoFeatureGroupExtension)

// explicit template instantiation
template class GuiExport ViewProviderExtensionPythonT<ViewProviderGeoFeatureGroupExtension>;
}
