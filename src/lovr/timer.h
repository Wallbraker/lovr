#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

extern const luaL_Reg lovrTimer[];
int l_lovrTimerInit(lua_State* L);
int l_lovrTimerStep(lua_State* L);
