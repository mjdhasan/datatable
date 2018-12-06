//------------------------------------------------------------------------------
// Copyright 2018 H2O.ai
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
//------------------------------------------------------------------------------
#include "frame/py_frame.h"
#include "python/_all.h"
#include "python/string.h"

namespace py {

// Sentinel value for __getitem__() mode
static robj GETITEM(reinterpret_cast<PyObject*>(-1));




oobj Frame::m__getitem__(robj item) {
  return _fast_getset(item, GETITEM);
}

void Frame::m__setitem__(robj item, robj value) {
  _fast_getset(item, value);
}


/**
 * "Fast" get/set only handles the case of the form `DT[i, j]` where
 * `i` is integer, and `j` is either an integer or a string. These cases
 * are special in that they return a scalar python value, instead of a
 * Frame object.
 * This case should also be handled first, to ensure that it has maximum
 * performance.
 */
oobj Frame::_fast_getset(robj item, robj value) {
  rtuple targs = item.to_rtuple_lax();
  if (targs && targs.size() == 2 && value == GETITEM) {
    robj arg0 = targs[0], arg1 = targs[1];
    bool a0int = arg0.is_int();
    bool a1int = arg1.is_int();
    if (a0int && (a1int || arg1.is_string())) {
      int64_t irow = arg0.to_int64_strict();
      int64_t nrows = static_cast<int64_t>(dt->nrows);
      int64_t ncols = static_cast<int64_t>(dt->ncols);
      if (irow < 0) irow += nrows;
      if (irow < 0 || irow >= nrows) {
        if (irow < 0) irow -= nrows;
        throw ValueError() << "Row `" << irow << "` is invalid for a frame "
            "with " << nrows << " row" << (nrows == 1? "" : "s");
      }
      size_t col_index;
      if (a1int) {
        int64_t icol = arg1.to_int64_strict();
        if (icol < 0) icol += ncols;
        if (icol < 0 || icol >= ncols) {
          if (icol < 0) icol -= ncols;
          throw ValueError() << "Column index `" << icol << "` is invalid "
              "for a frame with " << ncols << " column" <<
              (ncols == 1? "" : "s");
        }
        col_index = static_cast<size_t>(icol);
      } else {
        col_index = dt->xcolindex(arg1);
      }
      Column* col = dt->columns[col_index];
      return col->get_value_at_index(static_cast<size_t>(irow));
    }
  }
  return _main_getset(item, value);
}


oobj Frame::_main_getset(robj item, robj value) {
  return _fallback_getset(item, value);
}


oobj Frame::_fallback_getset(robj item, robj value) {
  odict kwargs;
  otuple args(5);
  args.set(0, py::robj(this));
  if (item.is_tuple()) {
    otuple argslist = item.to_otuple();
    size_t n = argslist.size();
    if (n >= 2) {
      args.set(1, argslist[0]);
      args.set(2, argslist[1]);
      if (n == 3) {
        args.set(3, argslist[2]);
      } else if (n >= 4) {
        throw ValueError() << "Selector " << item << " is not supported";
      }
    } else {
      throw ValueError() << "Invalid selector " << item;
    }
  } else {
    args.set(1, py::None());
    args.set(2, item);
    DeprecationWarning() << "Single-item selectors `DT[col]` are deprecated "
        "since 0.7.0; please use `DT[:, col]` instead. This message will "
        "become an error in version 0.8.0";
  }
  if (!args[3]) args.set(3, py::None());
  if (!args[4]) args.set(4, py::None());
  if (value == GETITEM) {}
  else if (value) {
    kwargs.set(ostring("mode"), ostring("update"));
    kwargs.set(ostring("replacement"), value);
  } else {
    kwargs.set(ostring("mode"), ostring("delete"));
  }
  return robj(py::fallback_makedatatable).call(args, kwargs);
}


}  // namespace py