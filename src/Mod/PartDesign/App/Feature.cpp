/***************************************************************************
 *   Copyright (c) 2011 Juergen Riegel <FreeCAD@juergen-riegel.net>        *
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
# include <Standard_Failure.hxx>
# include <TopoDS_Solid.hxx>
# include <TopExp_Explorer.hxx>
# include <TopoDS.hxx>
# include <BRep_Tool.hxx>
# include <gp_Pnt.hxx>
# include <gp_Pln.hxx>
# include <BRepBuilderAPI_MakeFace.hxx>
#endif

// TODO Cleanup headers (2015-09-04, Fat-Zer)
#include <Base/Exception.h>
#include "App/Document.h"
#include <App/FeaturePythonPyImp.h>
#include "App/OriginFeature.h"
#include "Body.h"
#include "ShapeBinder.h"
#include "Feature.h"
#include "FeaturePy.h"
#include "Mod/Part/App/DatumFeature.h"

#include <Base/Console.h>
FC_LOG_LEVEL_INIT("PartDesign",true,true)


namespace PartDesign {


PROPERTY_SOURCE(PartDesign::Feature,Part::Feature)

Feature::Feature()
{
    ADD_PROPERTY(BaseFeature,(0));
    ADD_PROPERTY_TYPE(_Body,(0),"Base",(App::PropertyType)(
                App::Prop_ReadOnly|App::Prop_Hidden|App::Prop_Output|App::Prop_Transient),0);
    Placement.setStatus(App::Property::Hidden, true);
    BaseFeature.setStatus(App::Property::Hidden, true);
}

short Feature::mustExecute() const
{
    if (BaseFeature.isTouched())
        return 1;
    return Part::Feature::mustExecute();
}

bool Feature::allowMultiSolid() const {
    auto body = getFeatureBody();
    return body && !body->SingleSolid.getValue();
}

TopoShape Feature::getSolid(const TopoShape& shape)
{
    if (shape.isNull())
        Standard_Failure::Raise("Shape is null");
    int count = shape.countSubShapes(TopAbs_SOLID);
    if(count>1) {
        if(allowMultiSolid())
            return shape;
        throw Base::RuntimeError("Result has multiple solids.\n"
                "To allow multiple solids, please set 'SingleSolid' property of the body to false");
    }
    if(count)
        return shape.getSubTopoShape(TopAbs_SOLID,1);
    return TopoShape();
}

const gp_Pnt Feature::getPointFromFace(const TopoDS_Face& f)
{
    if (!f.Infinite()) {
        TopExp_Explorer exp;
        exp.Init(f, TopAbs_VERTEX);
        if (exp.More())
            return BRep_Tool::Pnt(TopoDS::Vertex(exp.Current()));
        // Else try the other method
    }

    // TODO: Other method, e.g. intersect X,Y,Z axis with the (unlimited?) face?
    // Or get a "corner" point if the face is limited?
    throw Base::NotImplementedError("getPointFromFace(): Not implemented yet for this case");
}

Part::Feature* Feature::getBaseObject(bool silent) const {
    App::DocumentObject* BaseLink = BaseFeature.getValue();
    Part::Feature* BaseObject = nullptr;
    const char *err = nullptr;

    if (BaseLink) {
        if (BaseLink->getTypeId().isDerivedFrom(Part::Feature::getClassTypeId())) {
            BaseObject = static_cast<Part::Feature*>(BaseLink);
        }
        if (!BaseObject) {
            err =  "No base feature linked";
        }
    } else {
        err = "Base property not set";
    }

    // If the function not in silent mode throw the exception describing the error
    if (!silent && err) {
        throw Base::RuntimeError(err);
    }

    return BaseObject;
}

TopoShape Feature::getBaseShape() const {
    const Part::Feature* BaseObject = getBaseObject();

    if (BaseObject->isDerivedFrom(PartDesign::ShapeBinder::getClassTypeId())||
        BaseObject->isDerivedFrom(PartDesign::SubShapeBinder::getClassTypeId()))
    {
        throw Base::ValueError("Base shape of shape binder cannot be used");
    }

    // auto shape = getTopoShape(BaseObject);
    auto shape = BaseObject->Shape.getShape();
    if(shape.isNull())
        throw Part::NullShapeException("Base feature's shape is invalid");
    if(!shape.hasSubShape(TopAbs_SOLID))
        throw Base::ValueError("Base feature's shape is not a solid");

    return shape;
}

const TopoDS_Shape& Feature::getBaseShapeOld() const {
    const Part::Feature* BaseObject = getBaseObject();

    if (BaseObject->isDerivedFrom(PartDesign::ShapeBinder::getClassTypeId())) {
        throw Base::ValueError("Base shape of shape binder cannot be used");
    }

    const TopoDS_Shape& result = BaseObject->Shape.getValue();
    if (result.IsNull())
        throw Part::NullShapeException("Base feature's shape is invalid");
    TopExp_Explorer xp (result, TopAbs_SOLID);
    if (!xp.More())
        throw Base::ValueError("Base feature's shape is not a solid");

    return result;
}


PyObject* Feature::getPyObject()
{
    if (PythonObject.is(Py::_None())){
        // ref counter is set to 1
        PythonObject = Py::Object(new FeaturePy(this),true);
    }
    return Py::new_reference_to(PythonObject);
}

bool Feature::isDatum(const App::DocumentObject* feature)
{
    return feature->getTypeId().isDerivedFrom(App::OriginFeature::getClassTypeId()) ||
           feature->getTypeId().isDerivedFrom(Part::Datum::getClassTypeId());
}

gp_Pln Feature::makePlnFromPlane(const App::DocumentObject* obj)
{
    const App::GeoFeature* plane = static_cast<const App::GeoFeature*>(obj);
    if (plane == NULL)
        throw Base::ValueError("Feature: Null object");

    Base::Vector3d pos = plane->Placement.getValue().getPosition();
    Base::Rotation rot = plane->Placement.getValue().getRotation();
    Base::Vector3d normal(0,0,1);
    rot.multVec(normal, normal);
    return gp_Pln(gp_Pnt(pos.x,pos.y,pos.z), gp_Dir(normal.x,normal.y,normal.z));
}

TopoDS_Shape Feature::makeShapeFromPlane(const App::DocumentObject* obj)
{
    BRepBuilderAPI_MakeFace builder(makePlnFromPlane(obj));
    if (!builder.IsDone())
        throw Base::CADKernelError("Feature: Could not create shape from base plane");

    return builder.Shape();
}

Body* Feature::getFeatureBody() const {

    auto body = Base::freecad_dynamic_cast<Body>(_Body.getValue());
    if(body)
        return body;

    auto list = getInList();
    for (auto in : list) {
        if(in->isDerivedFrom(Body::getClassTypeId()) && //is Body?
           static_cast<Body*>(in)->hasObject(this)) {    //is part of this Body?
               
               return static_cast<Body*>(in);
        }
    }
    
    return nullptr;
};

}//namespace PartDesign

namespace App {
/// @cond DOXERR
PROPERTY_SOURCE_TEMPLATE(PartDesign::FeaturePython, PartDesign::Feature)
template<> const char* PartDesign::FeaturePython::getViewProviderName(void) const {
    return "PartDesignGui::ViewProviderPython";
}
template<> PyObject* PartDesign::FeaturePython::getPyObject(void) {
    if (PythonObject.is(Py::_None())) {
        // ref counter is set to 1
        PythonObject = Py::Object(new FeaturePythonPyT<PartDesign::FeaturePy>(this),true);
    }
    return Py::new_reference_to(PythonObject);
}
/// @endcond

// explicit template instantiation
template class PartDesignExport FeaturePythonT<PartDesign::Feature>;
}

