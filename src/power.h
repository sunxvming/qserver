#ifndef POWER
#define POWER
#ifdef __cplusplus
extern "C"
{
#endif

void power_init( void* luaState );
void power_loop( void* luaState );

#ifdef __cplusplus
}
#endif
#endif
