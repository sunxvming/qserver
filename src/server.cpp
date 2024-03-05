#include "misc.h"
#include "power.h"
#define SERVERVERSION "2024.03.01"
#if WIN

#include <winternl.h>

#else

#include <netdb.h>
#include <pwd.h>
#include <signal.h>
#include <sys/stat.h>

#endif

#if LINUX

#include <sys/wait.h>
#include <sys/prctl.h>
#include <time.h>
extern int getpgid( int );
extern int getsid( int );
extern int kill( int, int );

#ifdef __cplusplus
extern "C" {
#endif

extern void tzset();

#ifdef __cplusplus
}
#endif

extern void init_rsa();

#endif




static char runPath[8192+1] = "";

static int init( char* self )
{
	#if WIN

	::SetConsoleOutputCP( CP_UTF8 );
	::SetConsoleCP( CP_UTF8 );
	::GetModuleFileNameA( NULL, runPath, MAX_PATH );

	#elif LINUX

	printf( "INFO process %d\n", getpid( ) );
	printf( "INFO group %d\n", getpgid( 0 ) );
	printf( "INFO session %d\n", getsid( 0 ) );

	#ifdef SERVER_SECURITY

	if ( geteuid( ) != 0 )
	{
		logErr( "ERROR need root owner and setuid permission" );
		return -1;
	}

	char* dir = getcwd( 0, 0 );
	if ( !dir || !realpath( self, runPath ) )
	{
		perror( "ERROR getcwd" );
		return errno;
	}

	struct stat st;
	stat( dir, &st );
	printf( "INFO set user %d\n", st.st_uid );

	struct passwd* pwd = getpwuid( st.st_uid );
	if ( !pwd )
	{
		errno ? perror( "ERROR user error" ) : logErr( "ERROR user not found");
		return -1;
	}
	printf( "INFO set user %s\n", pwd->pw_name );

	if ( setregid( 0, st.st_gid ) )
	{
		perror( "ERROR setregid" );
		return -1;
	}

	if ( setreuid( 0, st.st_uid ) )
	{
		perror("ERROR setreuid");
		return -1;
	}

	char* home = pwd->pw_dir;
	printf( "INFO home %s\n", home );
	printf( "INFO run %s\n", runPath );
	printf( "INFO at %s\n", dir );

	int homen = strlen( home ) - ( home[strlen( home ) - 1] == '/' );
	if ( strstr( dir, home ) != dir || dir[homen] && dir[homen] != '/' )
	{
		home = dir;
		homen = strlen( dir );
	}

	#endif

	tzset( );

	time_t utc;
	struct tm dst;
	time( &utc );
	localtime_r( &utc, &dst );
	int ctz = timezone;
	if ( dst.tm_isdst )
	{
		time_t t0 = mktime( &dst );
		dst.tm_isdst = 0;
		ctz += t0 - mktime( &dst );
	}

	struct timeval timev;
	struct timezone tz;
	gettimeofday( &timev, &tz );
	if ( ctz != 60 * tz.tz_minuteswest )
	{
		logErr( "ERROR different system and kernel timezone" );
		return -1;
	}

	init_rsa( );

	#ifdef SERVER_SECURITY

	printf( "INFO init dns\n" );
	gethostbyname( "qdgaming.com" );

	printf( "INFO chroot %s\n", home );
	setreuid( geteuid( ), 0 );

	if ( chdir( dir ) )
	{
		perror( "ERROR chdir" );
		return errno;
	}

	if ( chroot( home ) )
	{
		perror( "ERROR chroot" );
		return errno;
	}
	if ( chdir( dir[homen] ? dir + homen : "/" ) )
	{
		perror( "ERROR chrootdir" );
		return errno;
	}
	setreuid( 0, getuid( ) );

	free( dir );

	#endif

	#else

	realpath( self, runPath );

	#endif

	return 0;
}

#if WIN


static int parentId( int id )
{
	static int (WINAPI *QueryProcess)( HANDLE p, int zero, int *s, int len, void *nil )
		= (int (__stdcall *) ( HANDLE, int, int *, int, void * )) ::GetProcAddress
			( ::GetModuleHandleA( "ntdll.dll" ), "NtQueryInformationProcess" );

	HANDLE h = ::OpenProcess( PROCESS_QUERY_INFORMATION, false, id );

	int s[6];
	(*QueryProcess)( h, ProcessBasicInformation, s, sizeof( s ), NULL );
	CloseHandle( h );

	return s[5];
}
// -0|1 process +0|2 process,parent
static int os_id( lua_State* lua )
{
	int p = toint( lua, 1 );
	if ( p == 0 )
		p = ::GetCurrentProcessId( );

	int pp = parentId( p );
	if ( pp <= 0 )
		return 0;

	pushi( lua, p );
	pushi( lua, pp );
	return 2;
}

static int os_title( lua_State* lua )
{
	checkArg( lua, 1, "#1", LUA_TSTRING );
	const char* title = tostr( lua, 1 );
	TCHAR buff[256];
	unsigned int i = 0;
	for ( i = 0; i < strlen(title) && i < 256; i ++ )
		buff[i] = title[i];
	buff[i] = 0;
	pushb( lua, ::SetConsoleTitle( buff ) );
	return 1;
}

static int os_color( lua_State* lua )
{
	checkArg( lua, 1, "#1", LUA_TNUMBER );
	checkArg( lua, 2, "#2", LUA_TNUMBER );
	unsigned int fg = touint( lua, 1 );
	unsigned int bg = touint( lua, 2 );

	short attrib = ( ( fg & 0xFF000000 ) ? FOREGROUND_INTENSITY : 0 ) | ( ( fg & 0x00FF0000 ) ? FOREGROUND_RED : 0 ) | ( ( fg & 0x0000FF00 ) ? FOREGROUND_GREEN : 0 ) | ( ( fg & 0x000000FF ) ? FOREGROUND_BLUE : 0 );
	attrib |= ( ( bg & 0xFF000000 ) ? BACKGROUND_INTENSITY : 0 ) | ( ( bg & 0x00FF0000 ) ? BACKGROUND_RED : 0 ) | ( ( bg & 0x0000FF00 ) ? BACKGROUND_GREEN : 0 ) | ( ( bg & 0x000000FF ) ? BACKGROUND_BLUE : 0 );

	COORD c = { 0, 0 };
	DWORD out;
	pushb( lua, ::FillConsoleOutputAttribute( ::GetStdHandle( STD_OUTPUT_HANDLE ), attrib, 0x0FFFFFFF, c, &out ) && ::SetConsoleTextAttribute( ::GetStdHandle( STD_OUTPUT_HANDLE ), attrib ) );
	return 1;
}

// -1* directory,argument... +1 pid
static int os_launch( lua_State* lua )
{
	checkArg( lua, 1, "#1", LUA_TSTRING );
	int   L = gettop( lua );
	char  args[1002] = ". ";
	char* s = args + 2;

	for ( int i = 2; i <= L; i ++ )
	{
		s = (char*)memccpy( s, tobytes( lua, i, NULL, "" ), '\0', args + sizeof( args ) - s );
		if ( s == NULL )
			error( lua, "launch arguments too long" );
		s[-1] = i < L ? ' ' : '\0';
	}

	STARTUPINFOA si;
	memset( &si, 0, sizeof( si ) );
	si.cb = sizeof( si );

	PROCESS_INFORMATION pid;

	if ( ! ::CreateProcessA( runPath, args, NULL, NULL, false,
							 CREATE_NEW_CONSOLE, NULL, tostr( lua, 1 ), &si, &pid ) )
		return error( lua, "launch error %s", strerror( errno ) );

	logErr("INFO os.launch");
	::CloseHandle( pid.hProcess );
	::CloseHandle( pid.hThread );

	pushi( lua, pid.dwProcessId );
	return 1;
}
// -1 pid +0
static int os_kill( lua_State* lua )
{
	checkArg( lua, 1, "#1", LUA_TNUMBER );

	int id = toint( lua, 1 );
	if ( parentId( id ) != ::GetCurrentProcessId( ) )
		error(lua, "not child process");

	HANDLE h = ::OpenProcess( PROCESS_TERMINATE, false, id );
	if ( ! ::TerminateProcess( h, -1 ) )
		error( lua, "kill error %s", strerror( errno ) );

	::CloseHandle( h );

	return 0;
}

#else

// -0|1 process +0|4 process,group,session,selfParent|nil
static int os_id( lua_State* lua )
{
	int p = toint( lua, 1 );
	if ( p == 0 )
		p = getpid( );

	int g = getpgid( p );
	if ( g <= 0 )
		return 0;

	pushi( lua, p );
	pushi( lua, g );

	#if !ANDROID
	pushi( lua, getsid( p ) );
	#else
	pushi( lua, getpid( ) );
	#endif

	if ( p == getpid( ) )
	{
		pushi( lua, getppid( ) );
		return 4;
	}
	return 3;
}

static int os_title( lua_State* lua )
{
	return 0;
}

static int os_color( lua_State* lua )
{
	return 0;
}

static int socks[2];
static char arg[4 + 256 + 1000];
static int timediff = 0;

// -1* directory,argument... +1 process
static int os_launch( lua_State* lua )
{
	struct timeval time;
	struct timezone tz;
	gettimeofday( &time, &tz );
	if ( timediff != -60 * tz.tz_minuteswest )
		error( lua, "Can't launch with different timezone %d and %d",
				timediff, -60 * tz.tz_minuteswest );

	checkArg( lua, 1, "#1", LUA_TSTRING );
	if ( tolen( lua, 1 ) > 255 )
		error( lua, "directory too long" );

	int L = gettop( lua );
	char args[4 + 256 + 1000];
	*(int*)args = getpid( );
	strcpy( args + 4, tostr( lua, 1 ) );

	char *s = args + 4 + 256;
	for ( int i = 2; i <= L; i ++ )
	{
		s = (char*)memccpy( s, tobytes( lua, i, NULL, "" ), '\0', args + sizeof( args ) - s );
		if ( s == NULL)
			error( lua, "launch arguments too long" );
	}
	*s = '\0';

	send( socks[1], args, sizeof( args ), 0 );

	int ids[2] = { 0, 0 };
	while ( recv( socks[1], ids, sizeof( ids ), MSG_PEEK ), ids[0] != getpid( ) )
		;

	recv( socks[1], ids, sizeof( ids ), 0 );
	if ( ids[1] < 0 )
		return error( lua, "launch error" );

	logErr( "INFO os.launch" );
	pushi( lua, ids[1] );
	return 1;
}

// -1 pid +0
static int os_kill( lua_State* lua )
{
	checkArg( lua, 1, "#1", LUA_TNUMBER );

	int id = toint( lua, 1 );
	if ( getpgid( id ) != getpgid( 0 ) )
		error( lua, "not child process");

	if ( kill( id, SIGKILL ) )
		error( lua, "kill error %s", strerror( errno ) );

	logErr( "INFO os.kill" );
	return 0;
}

#endif

static int os_crash( lua_State* lua )
{
	*( (long*) 0 ) = 0;
	return 0;
}






int main( int argn, char* args[], char* envs[] )
{
	int err = 0;
	bool  profile = false;
	long long threshold = 0;



	printf( "server version \"%s\"\n", SERVERVERSION );

	err = init( args[0] );
	if ( err )
		return err;

	char** as = args + 1;

	#if !WIN

	setpgid( 0, 0 );
	signal( SIGCHLD, SIG_IGN );

	if ( socketpair( AF_UNIX, SOCK_DGRAM, 0, socks ) )
	{
		perror( "ERROR socketpair" );
		return errno;
	}

	struct timeval t = { 0, 250000 };
	setsockopt( socks[0], SOL_SOCKET, SO_RCVTIMEO, &t, sizeof( t ) );

	int id = fork( );
	if ( id < 0 )
	{
		perror( "ERROR fork" );
		return errno;
	}
	else if ( id > 0 )
	{
		for ( as = NULL; ; ) // top
		{
			int status;
			if ( waitpid( 0, &status, WNOHANG ) < 0 && errno == ECHILD )
				return 0;

			if ( recv( socks[0], arg, sizeof( arg ), 0 ) < 0 && errno )
				continue;

			int ids[2] = { *(int*)arg, -1 };
			char* dir = getcwd( 0, 0 );
			if ( chdir( arg + 4 ) )
				perror( "ERROR os.launch chdir" );
			else if ( ids[1] = fork( ), ids[1] < 0 )
				perror( "ERROR os.launch fork" );
			else if ( ids[1] == 0 )
				break;

			if ( chdir( dir ) )
				perror( "ERROR os.launch chdir" );
			send( socks[0], ids, sizeof( ids ), 0 );
		}
	}

	char** ass = as;
	for ( char* a = arg + 4 + 256; ass ? (uintptr)( a = *ass ) : (uintptr)*a;
		ass ? (void)ass++ : (void)( a += strlen( a ) + 1 ) )
	{
		const char* eq = strchr( a, '=' );
		if ( eq == NULL )
			continue;

		if ( strncmp( a, "profile", eq - a ) == 0 )
		{
			if ( strncmp( eq + 1, "enable", 6 ) == 0 )
				profile = true;
		}
		else if ( strncmp( a, "pthreshold", eq - a ) == 0 )
		{
			threshold = atoi( eq + 1 );
		}
		else if ( strncmp( a, "timezone", eq - a ) == 0 )
		{
			timediff = ( eq[1] == '+' ? -60 : 60 ) * atoi( eq + 2 );
		}
	}

	if ( timediff == 0 )
	{
		// child
		struct timeval time;
		struct timezone tz;
		gettimeofday( &time, &tz );
		timediff = -60 * tz.tz_minuteswest;
	}

	#else

	char** ass = as;
	for ( char* a; a = *ass; ass ++ )
	{
		const char* eq = strchr( a, '=' );
		if ( eq == NULL )
			continue;

		if ( strncmp( a, "profile", eq - a ) == 0 )
		{
			if ( strncmp( eq + 1, "enable", 6 ) == 0 )
				profile = true;
		}
		else if ( strncmp( a, "pthreshold", eq - a ) == 0 )
		{
			threshold = atoi( eq + 1 );
		}
	}

	#endif //!WIN

	#if LINUX

	if ( prctl( PR_SET_PDEATHSIG, SIGKILL ) )
		perror( "ERROR prctl" );

	#endif


	lua_State* lua = lua_open();
	luaL_openlibs(lua);

	// add func to os 
	int L = gettop( lua ) + 1;
	lua_getglobal( lua, "os" ); // L orig_os

	power_init( lua );

	lua_getglobal( lua, "os" ); // L+1 os

	// -1:stack top to bottom   1:stack bottom to top
	rawgetn( lua, L, "rename" ); // get orig_os.rename,   now top is rename, index is -1.   -2 is os
	rawsetn( lua, -2, "rename" );  // os.rename=orig_os.rename        

	rawgetn( lua, L, "remove" ); // get orig_os.remove
	rawsetn( lua, -2, "remove" ); // os.remove=orig_os.remove

	rawgetn( lua, L, "exit" ); // get orig_os.exit
	rawsetn( lua, -2, "exit" ); // os.exit=orig_os.exit

	pushc( lua, os_id );
	rawsetn( lua, -2, "id" ); // os.id

	pushc( lua, os_title );
	rawsetn( lua, -2, "title" ); // os.title

	pushc( lua, os_color );
	rawsetn( lua, -2, "color" ); // os.color

	pushc( lua, os_launch );
	rawsetn( lua, -2, "launch" ); // os.launch

	pushc( lua, os_kill );
	rawsetn( lua, -2, "kill" );	// os.kill

	pushc( lua, os_crash );
	rawsetn( lua, -2, "crash" ); // os.crash

	
	#if LINUX

	getmeta( lua, global );

	pushi( lua, socks[0] );

	rawseti( lua, -2, M_netlaunch ); // M_netlaunch=sock[0] / sock
	pop( lua, 1 );

	#endif

	newtable( lua );
	rawsetnv( lua, -2, "info", -1 );  // os.info={} L+2 os.info

	#if LINUX
	pushs( lua, "linux" );
	#elif MAC
	pushs( lua, "mac" );
	#else
	pushs( lua, "windows" );
	#endif

	rawsetn( lua, L + 2, "system" ); // os.info.system = topstr

	pushs( lua, SERVERVERSION );
	rawsetn( lua, L + 2, "version" ); // os.info.version = topstr


	for ( char** env = envs; *env; env ++ )
	{
		const char* eq = strchr( *env, '=' );
		pushsl( lua, *env, eq - *env );
		pushs( lua, eq + 1 );
		rawset( lua, L + 2 ); // { os.info.key=value }
	}

	pushs( lua, runPath );
	rawsetiv( lua, L + 2, 0, -1 ); // { os.info.0=runPath }
	pushi( lua, 0 );
	rawset( lua, L + 2 ); // { os.info.runPath=0 }


	// set program param to os.info 
	int I = 1;
	#if WIN
	for ( char* a; a = *as; as ++ )
	#else
	for ( char* a = arg + 4 + 256; as ? (uintptr)( a = *as ) : (uintptr)*a;
		as ? (void)as++ : (void)( a += strlen( a ) + 1 ) )
	#endif
	{
		const char* eq = strchr( a, '=' );
		if ( eq )
		{
			pushsl( lua, a, eq - a );
			pushs( lua, eq + 1);
			rawset( lua, L + 2 ); // { os.info.key=value }
		}
		else
		{
			pushs( lua, a );
			rawsetiv( lua, L + 2, I, -1 ); // { os.info.I=value }  position param, 0 is runPath ... 
			pushi( lua, I ++ );
			rawset( lua, L + 2 ); // { os.info.value=I }
		}
	}


	settop(lua, L-1);
	getmetai( lua, global, M_onerror ); // L M_onerror   从全局表 global 中获取键为 M_onerror，并将其压入 Lua 栈顶


	if ( ( err = luaL_loadfile( lua, "launch.lua" ) || pcall( lua, 0, 0, L ) ) )
		logErr( isstr( lua, -1 ) ? tostr( lua, -1 ) : tonamex( lua, -1 ) );

	for ( ; ; )
	{
		if ( err )
			usleep( 250000 );
		else
			power_loop( lua );

		#if !WIN

		if ( getppid( ) == 1 )
			return -1;

		#endif
	}


	return err;
}