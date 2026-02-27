// kvgerror.cpp — Error reporting
// C++20

#include "kosmvg.hpp"
#include "kvgutils.hpp"

namespace kvg {

static thread_local Error t_last_error = Error::None;

Error last_error() {
    return t_last_error;
}

const char* error_string(Error e) {
    switch (e) {
    case Error::None:              return "no error";
    case Error::OutOfMemory:       return "out of memory";
    case Error::InvalidArgument:   return "invalid argument";
    case Error::UnsupportedFormat: return "unsupported format";
    case Error::FileNotFound:      return "file not found";
    case Error::FileIOError:       return "file I/O error";
    case Error::FontParseError:    return "font parse error";
    case Error::ImageDecodeError:  return "image decode error";
    case Error::PathParseError:    return "path parse error";
    }
    return "unknown error";
}

namespace detail {

void set_last_error(Error e) {
    t_last_error = e;
}

void clear_last_error() {
    t_last_error = Error::None;
}

} // namespace detail

} // namespace kvg
