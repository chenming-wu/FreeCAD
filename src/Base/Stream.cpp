/***************************************************************************
 *   Copyright (c) 2007 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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
# include <QBuffer>
# include <QByteArray>
# include <QDataStream>
# include <QIODevice>
# include <cstdlib>
# include <string>
# include <cstdio>
# include <cstring>
# include <climits>
#endif
#include <iomanip>
#include "Stream.h"
#include <CXX/Objects.hxx>

using namespace Base;

OutputStream::OutputStream(std::ostream &rout, bool binary)
    : _out(rout), _binary(binary)
{
    if(!_binary)
        _out << std::setprecision(std::numeric_limits<float>::digits10 + 1);
}

OutputStream& OutputStream::operator << (const char *s) {
    if(_binary) {
        uint32_t len = s?std::strlen(s):0;
        (*this) << len;
        _out.write(s,len);
        return *this;
    }

    // for text mode, we count the line so that we can deal with potential eol
    // conversion by external software.
    uint32_t count=0;
    for(const char *c=s;*c;++c) {
        if(*c == '\n')
            ++count;
    }
    // Stores the line count followed by a colon as the dimilter. We don't use
    // whitespace because the input stream may also start with whitespace.
    _out << count << ':';

    // Stores the text, and normalize the end of line to a single '\n'.
    for(char c=*s++; c; c=*s++) {
        if(c == '\r') {
            c = *s++;
            if(!c)
                break;
            if(c!='\n') {
                // We allow '\r' if it is not at the end of a line
                _out.put('\r');
            }
        }
        _out.put(c);
    }

    // Add an extra newline as the delimiter for the following data.
    _out.put('\n');
    return *this;
}

// --------------------------------------------------------

InputStream::InputStream(std::istream &rin, bool binary)
    : _in(rin), _binary(binary)
{
}

InputStream& InputStream::operator >> (std::string &s) {
    if(_binary) {
        uint32_t len;
        (*this) >> len;
        s.resize(len);
        _in.read(&s[0],len);
        return *this;
    }

    uint32_t count;
    char c;
    // The count is followed by a colon as the delimiter. The string is allowed
    // to start with any character.
    _in >> count >> c;

    _ss.str("");
    for(uint32_t i=0; i<count && _in ;++i) {
        for(;;) {
            if(!_in.get(c))
                break;
            // Normalize \r\n to \n
            if(c == '\r') {
                if(!_in.get(c))
                    break;
                if(c == '\n')
                    break;
                _ss.put('\r');
                _ss.put(c);
            } else {
                _ss.put(c);
                if(c == '\n')
                    break;
            }
        }
    }

    // Reading the last line
    while(_in.get(c)) {
        // Normalize \r\n to \n, but DO NOT insert '\n' into the extracted
        // line, because the last '\n' is inserted by us (See OutputStream
        // operator>>(const char*) above)
        if(c == '\r') {
            if(!_in.get(c))
                break;
            if(c == '\n')
                break;
            _ss.put('\r');
        } else if(c == '\n')
            break;

        _ss.put(c);
    }

    s = _ss.str();
    return *this;
}
// ----------------------------------------------------------------------

ByteArrayOStreambuf::ByteArrayOStreambuf(QByteArray& ba) : _buffer(new QBuffer(&ba))
{
    _buffer->open(QIODevice::WriteOnly);
}

ByteArrayOStreambuf::~ByteArrayOStreambuf()
{
    _buffer->close();
    delete _buffer;
}

std::streambuf::int_type
ByteArrayOStreambuf::overflow(std::streambuf::int_type c)
{
    if (c != EOF) {
        char z = c;
        if (_buffer->write (&z, 1) != 1) {
            return EOF;
        }
    }
    return c;
}

std::streamsize ByteArrayOStreambuf::xsputn (const char* s, std::streamsize num)
{
    return _buffer->write(s,num);
}

std::streambuf::pos_type
ByteArrayOStreambuf::seekoff(std::streambuf::off_type off,
                             std::ios_base::seekdir way,
                             std::ios_base::openmode /*mode*/)
{
    off_type endpos = 0;
    off_type curpos = _buffer->pos();
    switch (way) {
        case std::ios_base::beg:
            endpos = off;
            break;
        case std::ios_base::cur:
            endpos = curpos + off;
            break;
        case std::ios_base::end:
            endpos = _buffer->size();
            break;
        default:
            return pos_type(off_type(-1));
    }

    if (endpos != curpos) {
        if (!_buffer->seek(endpos))
            endpos = -1;
    }

    return pos_type(endpos);
}

std::streambuf::pos_type
ByteArrayOStreambuf::seekpos(std::streambuf::pos_type pos,
                             std::ios_base::openmode /*mode*/)
{
    return seekoff(pos, std::ios_base::beg);
}

// ----------------------------------------------------------------------

ByteArrayIStreambuf::ByteArrayIStreambuf(const QByteArray& data) : _buffer(data)
{
    _beg = 0;
    _end = data.size();
    _cur = 0;
}

ByteArrayIStreambuf::~ByteArrayIStreambuf()
{
}

ByteArrayIStreambuf::int_type ByteArrayIStreambuf::underflow()
{
    if (_cur == _end)
        return traits_type::eof();

    return static_cast<ByteArrayIStreambuf::int_type>(_buffer[_cur]) & 0x000000ff;
}

ByteArrayIStreambuf::int_type ByteArrayIStreambuf::uflow()
{
    if (_cur == _end)
        return traits_type::eof();

    return static_cast<ByteArrayIStreambuf::int_type>(_buffer[_cur++]) & 0x000000ff;
}

ByteArrayIStreambuf::int_type ByteArrayIStreambuf::pbackfail(int_type ch)
{
    if (_cur == _beg || (ch != traits_type::eof() && ch != _buffer[_cur-1]))
        return traits_type::eof();

    return static_cast<ByteArrayIStreambuf::int_type>(_buffer[--_cur]) & 0x000000ff;
}

std::streamsize ByteArrayIStreambuf::showmanyc()
{
    return _end - _cur;
}

std::streambuf::pos_type
ByteArrayIStreambuf::seekoff(std::streambuf::off_type off,
                             std::ios_base::seekdir way,
                             std::ios_base::openmode /*mode*/ )
{
    int p_pos=-1;
    if (way == std::ios_base::beg)
        p_pos = _beg;
    else if (way == std::ios_base::end)
        p_pos = _end;
    else if (way == std::ios_base::cur)
        p_pos = _cur;

    if (p_pos > _end)
        return traits_type::eof();

    if (((p_pos + off) > _end) || ((p_pos + off) < _beg))
        return traits_type::eof();

    _cur = p_pos+ off;

    return ((p_pos+off) - _beg);
}

std::streambuf::pos_type
ByteArrayIStreambuf::seekpos(std::streambuf::pos_type pos,
                             std::ios_base::openmode /*mode*/)
{
    return seekoff(pos, std::ios_base::beg);
}

// ----------------------------------------------------------------------

IODeviceOStreambuf::IODeviceOStreambuf(QIODevice* dev) : device(dev)
{
}

IODeviceOStreambuf::~IODeviceOStreambuf()
{
}

std::streambuf::int_type
IODeviceOStreambuf::overflow(std::streambuf::int_type c)
{
    if (c != EOF) {
        char z = c;
        if (device->write (&z, 1) != 1) {
            return EOF;
        }
    }
    return c;
}

std::streamsize IODeviceOStreambuf::xsputn (const char* s, std::streamsize num)
{
    return device->write(s,num);
}

std::streambuf::pos_type
IODeviceOStreambuf::seekoff(std::streambuf::off_type off,
                            std::ios_base::seekdir way,
                            std::ios_base::openmode /*mode*/)
{
    off_type endpos = 0;
    off_type curpos = device->pos();
    switch (way) {
        case std::ios_base::beg:
            endpos = off;
            break;
        case std::ios_base::cur:
            endpos = curpos + off;
            break;
        case std::ios_base::end:
            endpos = device->size();
            break;
        default:
            return pos_type(off_type(-1));
    }

    if (endpos != curpos) {
        if (!device->seek(endpos))
            endpos = -1;
    }

    return pos_type(endpos);
}

std::streambuf::pos_type
IODeviceOStreambuf::seekpos(std::streambuf::pos_type pos,
                            std::ios_base::openmode /*mode*/)
{
    return seekoff(pos, std::ios_base::beg);
}

// ----------------------------------------------------------------------

IODeviceIStreambuf::IODeviceIStreambuf(QIODevice* dev) : device(dev)
{
    setg (buffer+pbSize,     // beginning of putback area
          buffer+pbSize,     // read position
          buffer+pbSize);    // end position
}

IODeviceIStreambuf::~IODeviceIStreambuf()
{
}

std::streambuf::int_type
IODeviceIStreambuf::underflow()
{
#ifndef _MSC_VER
using std::memcpy;
#endif

    // is read position before end of buffer?
    if (gptr() < egptr()) {
        return *gptr();
    }

    /* process size of putback area
     * - use number of characters read
     * - but at most size of putback area
     */
    int numPutback;
    numPutback = gptr() - eback();
    if (numPutback > pbSize) {
        numPutback = pbSize;
    }

    /* copy up to pbSize characters previously read into
     * the putback area
     */
    memcpy (buffer+(pbSize-numPutback), gptr()-numPutback,
            numPutback);

    // read at most bufSize new characters
    int num;
    num = device->read(buffer+pbSize, bufSize);
    if (num <= 0) {
        // ERROR or EOF
        return EOF;
    }

    // reset buffer pointers
    setg (buffer+(pbSize-numPutback),   // beginning of putback area
          buffer+pbSize,                // read position
          buffer+pbSize+num);           // end of buffer

    // return next character
    return *gptr();
}

std::streambuf::pos_type
IODeviceIStreambuf::seekoff(std::streambuf::off_type off,
                            std::ios_base::seekdir way,
                            std::ios_base::openmode /*mode*/)
{
    off_type endpos = 0;
    off_type curpos = device->pos();
    switch (way) {
        case std::ios_base::beg:
            endpos = off;
            break;
        case std::ios_base::cur:
            endpos = curpos + off;
            break;
        case std::ios_base::end:
            endpos = device->size();
            break;
        default:
            return pos_type(off_type(-1));
    }

    if (endpos != curpos) {
        if (!device->seek(endpos))
            endpos = -1;
    }

    return pos_type(endpos);
}

std::streambuf::pos_type
IODeviceIStreambuf::seekpos(std::streambuf::pos_type pos,
                            std::ios_base::openmode /*mode*/)
{
    return seekoff(pos, std::ios_base::beg);
}

// ---------------------------------------------------------

#define PYSTREAM_BUFFERED

// http://www.mr-edd.co.uk/blog/beginners_guide_streambuf
// http://www.icce.rug.nl/documents/cplusplus/cplusplus24.html
PyStreambuf::PyStreambuf(PyObject* o, std::size_t buf_size, std::size_t put_back)
    : inp(o)
    , put_back(std::max(put_back, std::size_t(1)))
    , buffer(std::max(buf_size, put_back) + put_back)
{
    Py_INCREF(inp);
    char *end = &buffer.front() + buffer.size();
    setg(end, end, end);
#ifdef PYSTREAM_BUFFERED
    char *base = &buffer.front();
    setp(base, base + buffer.size());
#endif
}

PyStreambuf::~PyStreambuf()
{
    sync();
    Py_DECREF(inp);
}

PyStreambuf::int_type PyStreambuf::underflow()
{
    if (gptr() < egptr()) {
        return traits_type::to_int_type(*gptr());
    }

    char *base = &buffer.front();
    char *start = base;

    if (eback() == base) { // true when this isn't the first fill
        std::memmove(base, egptr() - put_back, put_back);
        start += put_back;
    }

    std::size_t n;
    Py::Tuple arg(1);
    long len = static_cast<long>(buffer.size() - (start - base));
    arg.setItem(0, Py::Long(len));
    Py::Callable meth(Py::Object(inp).getAttr("read"));

    try {
        Py::String res(meth.apply(arg));
        std::string c = static_cast<std::string>(res);
        n = c.size();
        if (n == 0) {
            return traits_type::eof();
        }

        std::memcpy(start, &(c[0]), c.size());
    }
    catch (Py::Exception& e) {
        e.clear();
        return traits_type::eof();
    }

    setg(base, start, start + n);
    return traits_type::to_int_type(*gptr());
}

PyStreambuf::int_type
PyStreambuf::overflow(PyStreambuf::int_type ch)
{
#ifdef PYSTREAM_BUFFERED
    sync();
    if (ch != traits_type::eof()) {
        *pptr() = ch;
        pbump(1);
        return ch;
    }

    return traits_type::eof();
#else
    if (ch != EOF) {
        char z = ch;

        try {
            Py::Tuple arg(1);
            arg.setItem(0, Py::Char(z));
            Py::Callable meth(Py::Object(inp).getAttr("write"));
            meth.apply(arg);
        }
        catch(Py::Exception& e) {
            e.clear();
            return EOF;
        }
    }

    return ch;
#endif
}

int PyStreambuf::sync()
{
#ifdef PYSTREAM_BUFFERED
    if (pptr() > pbase()) {
        flushBuffer();
    }
    return 0;
#else
    return std::streambuf::sync();
#endif
}

bool PyStreambuf::flushBuffer()
{
    std::ptrdiff_t n = pptr() - pbase();
    pbump(-n);

    try {
        Py::Tuple arg(1);
        arg.setItem(0, Py::String(pbase(), n));
        Py::Callable meth(Py::Object(inp).getAttr("write"));
        meth.apply(arg);
        return true;
    }
    catch(Py::Exception& e) {
        e.clear();
        return false;
    }
}

std::streamsize PyStreambuf::xsputn (const char* s, std::streamsize num)
{
#ifdef PYSTREAM_BUFFERED
    return std::streambuf::xsputn(s, num);
#else
    try {
        Py::Tuple arg(1);
        arg.setItem(0, Py::String(s, num));
        Py::Callable meth(Py::Object(inp).getAttr("write"));
        meth.apply(arg);
    }
    catch(Py::Exception& e) {
        e.clear();
        return 0;
    }

    return num;
#endif
}

PyStreambuf::pos_type
PyStreambuf::seekoff(PyStreambuf::off_type offset, PyStreambuf::seekdir dir, PyStreambuf::openmode /*mode*/)
{
    int whence = 0;
    switch (dir) {
    case std::ios_base::beg:
        whence = 0;
        break;
    case std::ios_base::cur:
        whence = 1;
        break;
    case std::ios_base::end:
        whence = 2;
        break;
    default:
        return pos_type(off_type(-1));
    }

    try {
        Py::Tuple arg(2);
        arg.setItem(0, Py::Long(static_cast<long>(offset)));
        arg.setItem(1, Py::Long(whence));
        Py::Callable seek(Py::Object(inp).getAttr("seek"));
        seek.apply(arg);

        // get current position
        Py::Tuple arg2;
        Py::Callable tell(Py::Object(inp).getAttr("tell"));
        Py::Long pos(tell.apply(arg2));
        long cur_pos = static_cast<long>(pos);
        return static_cast<pos_type>(cur_pos);
    }
    catch(Py::Exception& e) {
        e.clear();
        return pos_type(off_type(-1));
    }
}

PyStreambuf::pos_type
PyStreambuf::seekpos(PyStreambuf::pos_type offset, PyStreambuf::openmode mode)
{
    return seekoff(offset, std::ios::beg, mode);
}

// ---------------------------------------------------------

Streambuf::Streambuf(const std::string& data)
{
    _beg = data.begin();
    _end = data.end();
    _cur = _beg;
}

Streambuf::~Streambuf()
{
}

Streambuf::int_type Streambuf::underflow()
{
    if (_cur == _end)
        return traits_type::eof();

    return static_cast<Streambuf::int_type>(*_cur) & 0x000000ff;
}

Streambuf::int_type Streambuf::uflow()
{
    if (_cur == _end)
        return traits_type::eof();

    return static_cast<Streambuf::int_type>(*_cur++) & 0x000000ff;
}

Streambuf::int_type Streambuf::pbackfail( int_type ch )
{
    if (_cur == _beg || (ch != traits_type::eof() && ch != _cur[-1]))
        return traits_type::eof();

    return static_cast<Streambuf::int_type>(*--_cur) & 0x000000ff;
}

std::streamsize Streambuf::showmanyc()
{
    return _end - _cur;
}

std::streambuf::pos_type
Streambuf::seekoff(std::streambuf::off_type off,
                   std::ios_base::seekdir way,
                   std::ios_base::openmode /*mode*/ )
{
    std::string::const_iterator p_pos;
    if (way == std::ios_base::beg)
        p_pos = _beg;
    else if (way == std::ios_base::end)
        p_pos = _end;
    else if (way == std::ios_base::cur)
        p_pos = _cur;

    if (p_pos > _end)
        return traits_type::eof();

    if (((p_pos + off) > _end) || ((p_pos + off) < _beg))
        return traits_type::eof();

    _cur = p_pos+ off;

    return ((p_pos+off) - _beg);
}

std::streambuf::pos_type
Streambuf::seekpos(std::streambuf::pos_type pos,
                   std::ios_base::openmode /*mode*/)
{
    return seekoff(pos, std::ios_base::beg);
}
