/***************************************************************************
 *   Copyright (c) Jürgen Riegel          (juergen.riegel@web.de) 2006     *
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



#ifndef APP_FEATUREPYTHON_H
#define APP_FEATUREPYTHON_H


#include <Base/Exception.h>
#include <Base/Writer.h>
#include <App/GeoFeature.h>
#include <App/DynamicProperty.h>
#include <App/PropertyPythonObject.h>
#include <App/PropertyGeo.h>

namespace App
{

class Property;

// Helper class to hide implementation details
class AppExport FeaturePythonImp
{
public:
    FeaturePythonImp(App::DocumentObject*);
    ~FeaturePythonImp();

    bool execute();
    bool mustExecute() const;
    void onBeforeChange(const Property* prop);
    bool onBeforeChangeLabel(std::string &newLabel);
    void onChanged(const Property* prop);
    void onDocumentRestored();
    std::string getViewProviderName();
    PyObject *getPyObject(void);

    bool getSubObject(App::DocumentObject *&ret, const char *subname, PyObject **pyObj, 
            Base::Matrix4D *mat, bool transform, int depth) const;

    bool getSubObjects(std::vector<std::string> &ret, int reason) const;

    bool getLinkedObject(App::DocumentObject *&ret, bool recurse, 
            Base::Matrix4D *mat, bool transform, int depth) const;

    int canLinkProperties() const;

    int allowDuplicateLabel() const;

    bool redirectSubName(std::ostringstream &ss,
            App::DocumentObject *topParent, App::DocumentObject *child) const;

    int canLoadPartial() const;

    /// return true to activate tree view group object handling
    int hasChildElement() const;
    /// Get sub-element visibility
    int isElementVisible(const char *) const;
    /// Set sub-element visibility
    int setElementVisible(const char *, bool);

private:
    App::DocumentObject* object;
    bool has__object__;

#define FC_PY_FEATURE_PYTHON \
    FC_PY_ELEMENT(execute)\
    FC_PY_ELEMENT(mustExecute)\
    FC_PY_ELEMENT(onBeforeChange)\
    FC_PY_ELEMENT(onBeforeChangeLabel)\
    FC_PY_ELEMENT(onChanged)\
    FC_PY_ELEMENT(onDocumentRestored)\
    FC_PY_ELEMENT(getViewProviderName)\
    FC_PY_ELEMENT(getSubObject)\
    FC_PY_ELEMENT(getSubObjects)\
    FC_PY_ELEMENT(getLinkedObject)\
    FC_PY_ELEMENT(canLinkProperties)\
    FC_PY_ELEMENT(allowDuplicateLabel)\
    FC_PY_ELEMENT(redirectSubName)\
    FC_PY_ELEMENT(canLoadPartial)\
    FC_PY_ELEMENT(hasChildElement)\
    FC_PY_ELEMENT(isElementVisible)\
    FC_PY_ELEMENT(setElementVisible)

#define FC_PY_ELEMENT_DEFINE(_name) \
    Py::Object py_##_name;

#define FC_PY_ELEMENT_INIT(_name) \
    FC_PY_GetCallable(pyobj,#_name,py_##_name);\
    if(!py_##_name.isNone()) {\
        PyObject *pyRecursive = PyObject_GetAttrString(pyobj, \
                "__allow_recursive_" #_name);\
        if(!pyRecursive) {\
            PyErr_Clear();\
            _Flags.set(FlagAllowRecursive_##_name, false);\
        }else{\
            _Flags.set(FlagAllowRecursive_##_name, PyObject_IsTrue(pyRecursive));\
            Py_DECREF(pyRecursive);\
        }\
    }

#define FC_PY_ELEMENT_FLAG(_name) \
    FlagCalling_##_name,\
    FlagAllowRecursive_##_name,

#define _FC_PY_CALL_CHECK(_name,_ret) \
    if((!_Flags.test(FlagAllowRecursive_##_name) \
                && _Flags.test(FlagCalling_##_name)) \
        || py_##_name.isNone()) \
    {\
        _ret;\
    }\
    Base::BitsetLocker<Flags> guard(_Flags, FlagCalling_##_name);

#undef FC_PY_ELEMENT
#define FC_PY_ELEMENT(_name) FC_PY_ELEMENT_DEFINE(_name)

    FC_PY_FEATURE_PYTHON

#undef FC_PY_ELEMENT
#define FC_PY_ELEMENT(_name) FC_PY_ELEMENT_FLAG(_name)

    enum Flag {
        FC_PY_FEATURE_PYTHON
        FlagMax,
    };

    typedef std::bitset<FlagMax> Flags;
    mutable Flags _Flags;

public:
    void init(PyObject *pyobj);
};

/**
 * Generic Python feature class which allows to behave every DocumentObject
 * derived class as Python feature -- simply by subclassing.
 * @author Werner Mayer
 */
template <class FeatureT>
class FeaturePythonT : public FeatureT
{
    PROPERTY_HEADER(App::FeaturePythonT<FeatureT>);

public:
    FeaturePythonT() {
        ADD_PROPERTY(Proxy,(Py::Object()));
        // cannot move this to the initializer list to avoid warning
        imp = new FeaturePythonImp(this);
    }
    virtual ~FeaturePythonT() {
        delete imp;
    }

    /** @name methods override DocumentObject */
    //@{
    short mustExecute() const {
        if (this->isTouched())
            return 1;
        auto ret = FeatureT::mustExecute();
        if(ret) return ret;
        return imp->mustExecute()?1:0;
    }
    /// recalculate the Feature
    virtual DocumentObjectExecReturn *execute(void) {
        try {
            bool handled = imp->execute();
            if (!handled)
                return FeatureT::execute();
        }
        catch (const Base::Exception& e) {
            return new App::DocumentObjectExecReturn(e.what());
        }
        return DocumentObject::StdReturn;
    }
    virtual const char* getViewProviderNameOverride(void) const override{
        viewProviderName = imp->getViewProviderName();
        if(viewProviderName.size())
            return viewProviderName.c_str();
        return FeatureT::getViewProviderNameOverride();
    }
    /// returns the type name of the ViewProvider
    virtual const char* getViewProviderName(void) const {
        return FeatureT::getViewProviderName();
        //return "Gui::ViewProviderPythonFeature";
    }

    virtual App::DocumentObject *getSubObject(const char *subname, PyObject **pyObj, 
            Base::Matrix4D *mat, bool transform, int depth) const override 
    {
        App::DocumentObject *ret = 0;
        if(imp->getSubObject(ret,subname,pyObj,mat,transform,depth))
            return ret;
        return FeatureT::getSubObject(subname,pyObj,mat,transform,depth);
    }

    virtual std::vector<std::string> getSubObjects(int reason=0) const override {
        std::vector<std::string> ret;
        if(imp->getSubObjects(ret,reason))
            return ret;
        return FeatureT::getSubObjects(reason);
    }

    virtual App::DocumentObject *getLinkedObject(bool recurse, 
            Base::Matrix4D *mat, bool transform, int depth) const override
    {
        App::DocumentObject *ret = 0;
        if(imp->getLinkedObject(ret,recurse,mat,transform,depth))
            return ret;
        return FeatureT::getLinkedObject(recurse,mat,transform,depth);
    }

    /// return true to activate tree view group object handling
    virtual bool hasChildElement() const override {
        int ret = imp->hasChildElement();
        if(ret<0) 
            return FeatureT::hasChildElement();
        return ret?true:false;
    }
    /// Get sub-element visibility
    virtual int isElementVisible(const char *element) const override {
        int ret = imp->isElementVisible(element);
        if(ret == -2)
            return FeatureT::isElementVisible(element);
        return ret;
    }
    /// Set sub-element visibility
    virtual int setElementVisible(const char *element, bool visible) override {
        int ret = imp->setElementVisible(element,visible);
        if(ret == -2)
            return FeatureT::setElementVisible(element,visible);
        return ret;
    }

    virtual bool canLinkProperties() const override {
        int ret = imp->canLinkProperties();
        if(ret < 0)
            return FeatureT::canLinkProperties();
        return ret?true:false;
    }

    virtual bool allowDuplicateLabel() const override {
        int ret = imp->allowDuplicateLabel();
        if(ret < 0)
            return FeatureT::allowDuplicateLabel();
        return ret?true:false;
    }

    virtual bool redirectSubName(std::ostringstream &ss,
            App::DocumentObject *topParent, App::DocumentObject *child) const override 
    {
        return imp->redirectSubName(ss,topParent,child) ||
            FeatureT::redirectSubName(ss,topParent,child);
    }

    virtual int canLoadPartial() const override {
        int ret = imp->canLoadPartial();
        if(ret>=0)
            return ret;
        return FeatureT::canLoadPartial();
    }

    PyObject *getPyObject(void) {
        if (FeatureT::PythonObject.is(Py::_None())) {
            // ref counter is set to 1
            FeatureT::PythonObject = Py::Object(imp->getPyObject(),true);
        }
        return Py::new_reference_to(FeatureT::PythonObject);
    }
    void setPyObject(PyObject *obj) {
        if (obj)
            FeatureT::PythonObject = obj;
        else
            FeatureT::PythonObject = Py::None();
    }

protected:
    virtual void onBeforeChange(const Property* prop) {
        FeatureT::onBeforeChange(prop);
        imp->onBeforeChange(prop);
    }
    virtual void onBeforeChangeLabel(std::string &newLabel) override{
        if(!imp->onBeforeChangeLabel(newLabel))
            FeatureT::onBeforeChangeLabel(newLabel);
    }
    virtual void onChanged(const Property* prop) {
        if(prop == &Proxy)
            imp->init(Proxy.getValue().ptr());
        imp->onChanged(prop);
        FeatureT::onChanged(prop);
    }
    virtual void onDocumentRestored() {
        imp->onDocumentRestored();
        FeatureT::onDocumentRestored();
    }

private:
    FeaturePythonImp* imp;
    PropertyPythonObject Proxy;
    mutable std::string viewProviderName;
};

// Special Feature-Python classes
typedef FeaturePythonT<DocumentObject> FeaturePython;
typedef FeaturePythonT<GeoFeature    > GeometryPython;

} //namespace App

#endif // APP_FEATUREPYTHON_H
