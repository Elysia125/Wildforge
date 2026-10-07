#pragma once
#include "Utils/WildforgeAuthority.h"
#include "Utils/WildforgeLog.h"

#define WF_AUTHORITY_GUARD(RetVal)                                             \
  do {                                                                         \
    if (!IsAuthoritativeForActorComponent(this)) {                             \
      WFLOG_ERROR(                                                             \
          "%s 在非权威端被调用，已忽略；客户端请改用对应的 Server_* RPC。",    \
          *FString(__FUNCTION__));                                             \
      return RetVal;                                                           \
    }                                                                          \
  } while (false)