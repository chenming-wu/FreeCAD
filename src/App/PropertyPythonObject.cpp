/***************************************************************************
 *   Copyright (c) 2009 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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

#include <boost/iostreams/copy.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include "PropertyPythonObject.h"
#include "DocumentObjectPy.h"
#include "Application.h"
#include "DocumentObject.h"
#include <Base/Base64.h>
#include <Base/Writer.h>
#include <Base/Reader.h>
#include <Base/Console.h>
#include <Base/Interpreter.h>
#include <iostream>
#include <boost/regex.hpp>

using namespace App;
using namespace Base;


TYPESYSTEM_SOURCE(App::PropertyPythonObject , App::Property);

PropertyPythonObject::PropertyPythonObject()
{
}

PropertyPythonObject::~PropertyPythonObject()
{
    // this is needed because the release of the pickled object may need the
    // GIL. Thus, we grab the GIL and replace the pickled with an empty object
    Base::PyGILStateLocker lock;
    this->object = Py::Object();
}

void PropertyPythonObject::setValue(Py::Object o)
{
    Base::PyGILStateLocker lock;
    aboutToSetValue();
    this->object = o;
    hasSetValue();
}

Py::Object PropertyPythonObject::getValue() const
{
    return object;
}

PyObject *PropertyPythonObject::getPyObject(void)
{
    return Py::new_reference_to(this->object);
}

void PropertyPythonObject::setPyObject(PyObject * obj)
{
    Base::PyGILStateLocker lock;
    aboutToSetValue();
    this->object = obj;
    hasSetValue();
}

std::string PropertyPythonObject::toString() const
{
    std::string repr;
    Base::PyGILStateLocker lock;
    try {
        Py::Module pickle(PyImport_ImportModule("json"),true);
        if (pickle.isNull())
            throw Py::Exception();
        static int indent = -1;
        if(indent<0) {
            ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath(
                    "User parameter:BaseApp/Preferences/Document");
            indent = hGrp->GetInt("JsonIndent",2);
        }
        Py::Callable method(pickle.getAttr(std::string("dumps")));
        Py::Object dump;
        if (this->object.hasAttr("__getstate__")) {
            Py::Tuple args;
            Py::Callable state(this->object.getAttr("__getstate__"));
            dump = state.apply(args);
        }
        else if (this->object.hasAttr("__dict__")) {
            dump = this->object.getAttr("__dict__");
        }
        else {
            dump = this->object;
        }

        Py::Tuple args(1);
        args.setItem(0, dump);
        Py::Dict kargs;
        kargs.setItem("indent",Py::Int(indent));
        Py::Object res = method.apply(args,kargs);
        Py::String str(res);
        repr = str.as_std_string("ascii");
    }
    catch (Py::Exception&) {
        Base::PyException e; // extract the Python error text
        e.ReportException();
    }

    return repr;
}

void PropertyPythonObject::fromString(const std::string& repr)
{
    Base::PyGILStateLocker lock;
    try {
        if (repr.empty())
            return;
        Py::Module pickle(PyImport_ImportModule("json"),true);
        if (pickle.isNull())
            throw Py::Exception();
        Py::Callable method(pickle.getAttr(std::string("loads")));
        Py::Tuple args(1);
        args.setItem(0, Py::String(repr));
        Py::Object res = method.apply(args);

        if (this->object.hasAttr("__setstate__")) {
            Py::Tuple args(1);
            args.setItem(0, res);
            Py::Callable state(this->object.getAttr("__setstate__"));
            state.apply(args);
        }
        else if (this->object.hasAttr("__dict__")) {
            this->object.setAttr("__dict__", res);
        }
        else {
            this->object = res;
        }
    }
    catch (Py::Exception&) {
        Base::PyException e; // extract the Python error text
        e.ReportException();
    }
}

void PropertyPythonObject::loadPickle(const std::string& str)
{
    // find the custom attributes and restore them
    Base::PyGILStateLocker lock;
    try {
        std::string buffer = str;
        boost::regex pickle("S'(\\w+)'.+S'(\\w+)'\\n");
        boost::match_results<std::string::const_iterator> what;
        std::string::const_iterator start, end;
        start = buffer.begin();
        end = buffer.end();
        while (boost::regex_search(start, end, what, pickle)) {
            std::string key = std::string(what[1].first, what[1].second);
            std::string val = std::string(what[2].first, what[2].second);
            this->object.setAttr(key, Py::String(val));
            buffer = std::string(what[2].second, end);
            start = buffer.begin();
            end = buffer.end();
        }
    }
    catch (Py::Exception&) {
        Base::PyException e; // extract the Python error text
        e.ReportException();
    }
}

std::string PropertyPythonObject::encodeValue(const std::string& str) const
{
    std::string tmp;
    for (std::string::const_iterator it = str.begin(); it != str.end(); ++it) {
        if (*it == '<')
            tmp += "&lt;";
        else if (*it == '"')
            tmp += "&quot;";
        else if (*it == '&')
            tmp += "&amp;";
        else if (*it == '>')
            tmp += "&gt";
        else if (*it == '\n')
            tmp += "\\n";
        else
            tmp += *it;
    }

    return tmp;
}

std::string PropertyPythonObject::decodeValue(const std::string& str) const
{
    std::string tmp;
    for (std::string::const_iterator it = str.begin(); it != str.end(); ++it) {
        if (*it == '\\') {
            ++it;
            if (it != str.end() && *it == 'n') {
                tmp += '\n';
            }
        }
        else
            tmp += *it;
    }

    return tmp;
}

void PropertyPythonObject::saveObject(Base::Writer &writer) const
{
    Base::PyGILStateLocker lock;
    try {
        PropertyContainer* parent = this->getContainer();
        if (parent->isDerivedFrom(Base::Type::fromName("App::DocumentObject"))) {
            if (this->object.hasAttr("__object__")) {
                writer.Stream() << " object=\"yes\"";
            }
        }
        if (parent->isDerivedFrom(Base::Type::fromName("Gui::ViewProvider"))) {
            if (this->object.hasAttr("__vobject__")) {
                writer.Stream() << " vobject=\"yes\"";
            }
        }
    }
    catch (Py::Exception& e) {
        e.clear();
    }
}

void PropertyPythonObject::restoreObject(Base::XMLReader &reader)
{
    Base::PyGILStateLocker lock;
    try {
        PropertyContainer* parent = this->getContainer();
        if (reader.hasAttribute("object")) {
            if (strcmp(reader.getAttribute("object"),"yes") == 0) {
                Py::Object obj = Py::asObject(parent->getPyObject());
                this->object.setAttr("__object__", obj);
            }
        }
        if (reader.hasAttribute("vobject")) {
            if (strcmp(reader.getAttribute("vobject"),"yes") == 0) {
                Py::Object obj = Py::asObject(parent->getPyObject());
                this->object.setAttr("__vobject__", obj);
            }
        }
    }
    catch (Py::Exception& e) {
        e.clear();
    }
    catch (const Base::Exception& e) {
        Base::Console().Error("%s\n",e.what());
    }
    catch (...) {
        Base::Console().Error("Critical error in PropertyPythonObject::restoreObject\n");
    }
}

void PropertyPythonObject::Save (Base::Writer &writer) const
{
    writer.Stream() << writer.ind() << "<Python ";
    Base::PyGILStateLocker lock;
    try {
        if (this->object.hasAttr("__module__") && this->object.hasAttr("__class__")) {
            Py::String mod(this->object.getAttr("__module__"));
            Py::Object cls(this->object.getAttr("__class__"));
            if (cls.hasAttr("__name__")) {
                Py::String name(cls.getAttr("__name__"));
                writer.Stream() << " module=\"" << (std::string)mod << "\""
                                << " class=\"" << (std::string)name << "\"";
            }
        }
        else {
            writer.Stream() << " json=\"yes\"";
        }
    }
    catch (Py::Exception&) {
        Base::PyException e; // extract the Python error text
        e.ReportException();
    }
    saveObject(writer);

    if (writer.getFileVersion() <= 1) {
        std::string json = this->toString();
        writer.Stream() << " value=\"" << base64_encode(json.c_str(),json.size())
                        << "\" encoded=\"yes\"/>\n";
    } else if(writer.isForceXML()) {
        std::string json = this->toString();
        if(json == "null")
            writer.Stream() << " value=\"null\"/>\n";
        else if(json.size()) {
            writer.Stream() << " cdata=\"1\">\n";
            writer.beginCharStream(false) << '\n' << json << '\n';
            writer.endCharStream() << '\n' << writer.ind() << "</Python>\n";
        } else 
            writer.Stream() << "/>\n";
    } else {
       writer.Stream() << " file=\"" 
           << writer.addFile(getFileName(".json"), this) << "\"/>\n";
    }
}

void PropertyPythonObject::Restore(Base::XMLReader &reader)
{
    reader.readElement("Python");

    bool load_json=false;
    bool load_pickle=false;

    std::string buffer;
    if(reader.hasAttribute("value")) {
        buffer = reader.getAttribute("value");
        if (reader.hasAttribute("encoded") &&
            strcmp(reader.getAttribute("encoded"),"yes") == 0) 
        {
            buffer = Base::base64_decode(buffer);
        }
        else {
            buffer = decodeValue(buffer);
        }
    }

    Base::PyGILStateLocker lock;
    try {
        static boost::regex pickle("^\\(i(\\w+)\\n(\\w+)\\n");
        boost::match_results<std::string::const_iterator> what;
        std::string::const_iterator start, end;
        start = buffer.begin();
        end = buffer.end();
        if (reader.hasAttribute("module") && reader.hasAttribute("class")) {
            Py::Module mod(PyImport_ImportModule(reader.getAttribute("module")),true);
            if (mod.isNull())
                throw Py::Exception();
            PyObject* cls = mod.getAttr(reader.getAttribute("class")).ptr();
            if (!cls) {
                std::stringstream s;
                s << "Module " << reader.getAttribute("module")
                    << " has no class " << reader.getAttribute("class");
                throw Py::AttributeError(s.str());
            }
#if PY_MAJOR_VERSION >= 3
            if (PyType_Check(cls)) 
#else
            if (PyClass_Check(cls)) 
            {
                this->object = PyInstance_NewRaw(cls, 0);
            }
            else if (PyType_Check(cls))
#endif
            {
                this->object = PyType_GenericAlloc((PyTypeObject*)cls, 0);
            }
            else {
                throw Py::TypeError("neither class nor type object");
            }
            load_json = true;
        }
        else if (boost::regex_search(start, end, what, pickle)) {
            std::string nam = std::string(what[1].first, what[1].second);
            std::string cls = std::string(what[2].first, what[2].second);
            Py::Module mod(PyImport_ImportModule(nam.c_str()),true);
            if (mod.isNull())
                throw Py::Exception();
#if PY_MAJOR_VERSION >= 3
            this->object = PyObject_CallObject(mod.getAttr(cls).ptr(), NULL);
#else
            this->object = PyInstance_NewRaw(mod.getAttr(cls).ptr(), 0);
#endif
            load_pickle = true;
            buffer = std::string(what[2].second, end);
        }
        else if (reader.hasAttribute("json")) {
            load_json = true;
        }
    }
    catch (Py::Exception&) {
        Base::PyException e; // extract the Python error text
        e.ReportException();
        this->object = Py::None();
    }

    aboutToSetValue();

    restoreObject(reader);

    if(reader.getAttributeAsInteger("cdata","")) {
        buffer = reader.readCharacters();
        reader.readEndElement("Python");
    } else if (reader.hasAttribute("file")) {
        std::string file(reader.getAttribute("file"));
        reader.addFile(file.c_str(),this);
    } 
    
    if(buffer.size()) {
        if (load_json)
            this->fromString(buffer);
        else if (load_pickle)
            this->loadPickle(buffer);
    }
    hasSetValue();

}

void PropertyPythonObject::SaveDocFile (Base::Writer &writer) const
{
    writer.Stream() << this->toString();
}

void PropertyPythonObject::RestoreDocFile(Base::Reader &reader)
{
    std::stringstream ss;
    reader >> ss.rdbuf();
    aboutToSetValue();
    this->fromString(ss.str());
    hasSetValue();
}

unsigned int PropertyPythonObject::getMemSize (void) const
{
    return sizeof(Py::Object);
}

Property *PropertyPythonObject::Copy(void) const
{
    PropertyPythonObject *p = new PropertyPythonObject();
    Base::PyGILStateLocker lock;
    p->object = this->object;
    return p;
}

void PropertyPythonObject::Paste(const Property &from)
{
    if (from.getTypeId() == PropertyPythonObject::getClassTypeId()) {
        Base::PyGILStateLocker lock;
        aboutToSetValue();
        this->object = static_cast<const PropertyPythonObject&>(from).object;
        hasSetValue();
    }
}
