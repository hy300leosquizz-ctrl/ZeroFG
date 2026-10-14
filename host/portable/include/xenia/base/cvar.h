// Copyright 2026 ZeroFG authors. SPDX-License-Identifier: Apache-2.0
//
// The portable host kit's stand-in for Xenia's cvars: plain globals in the
// cvars namespace. The host sets them before it creates the presenter (the kit
// defines the ones the presenter declares, in src/host.cc).
#ifndef ZEROFG_PORTABLE_XENIA_BASE_CVAR_H_
#define ZEROFG_PORTABLE_XENIA_BASE_CVAR_H_

#include <cstdint>
#include <string>

#define DECLARE_bool(name) \
  namespace cvars {        \
  extern bool name;        \
  }
#define DECLARE_string(name) \
  namespace cvars {          \
  extern std::string name;   \
  }
#define DECLARE_uint32(name) \
  namespace cvars {          \
  extern uint32_t name;      \
  }
#define DECLARE_int32(name) \
  namespace cvars {         \
  extern int32_t name;      \
  }
#define DECLARE_double(name) \
  namespace cvars {          \
  extern double name;        \
  }

#define DEFINE_bool(name, default_value, description, category) \
  namespace cvars {                                              \
  bool name = default_value;                                     \
  }
#define DEFINE_string(name, default_value, description, category) \
  namespace cvars {                                                \
  std::string name = default_value;                                \
  }
#define DEFINE_uint32(name, default_value, description, category) \
  namespace cvars {                                                \
  uint32_t name = default_value;                                   \
  }
#define DEFINE_int32(name, default_value, description, category) \
  namespace cvars {                                               \
  int32_t name = default_value;                                   \
  }
#define DEFINE_double(name, default_value, description, category) \
  namespace cvars {                                                \
  double name = default_value;                                     \
  }

#endif  // ZEROFG_PORTABLE_XENIA_BASE_CVAR_H_
